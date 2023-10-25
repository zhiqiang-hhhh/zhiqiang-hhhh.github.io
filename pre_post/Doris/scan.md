
<!-- @import "[TOC]" {cmd="toc" depthFrom=1 depthTo=6 orderedList=false} -->


<!-- code_chunk_output -->

- [Build](#build)
  - [Create ExecNode Tree](#create-execnode-tree)
  - [ExecNode Prepare](#execnode-prepare)
  - [Build PipelineTasks](#build-pipelinetasks)
- [Execution](#execution)
  - [PipelineTask open](#pipelinetask-open)
    - [_prepare_scanners](#_prepare_scanners)
    - [_init_scanners](#_init_scanners)
  - [Scanner](#scanner)
    - [VScanNode::get_next](#vscannodeget_next)

<!-- /code_chunk_output -->
## Build
每个 Fragment 都是由 PlanNode 组成的。
Build 阶段 主要完成 PlanNode -> ExecNode -> Operator -> PipelineTask 构建的工作。

### Create ExecNode Tree

从 PlanNode 到 ExecNode 的构建主要在`ExecNode::create_tree_helper`：
```cpp
Status ExecNode::create_tree_helper(RuntimeState* state, ObjectPool* pool,
                                    const std::vector<TPlanNode>& thrift_plan_nodes,
                                    const DescriptorTbl& descs, ExecNode* parent, int* node_idx,
                                    ExecNode** root)
{
    // Step 1 Create current ExecNode according to current thrift plan node.
    ExecNode* cur_exec_node = nullptr;
    RETURN_IF_ERROR(create_node(state, pool, cur_plan_node, descs, &cur_exec_node));
    ...
    // Step 2
    // Create child ExecNode tree of current node in a recursive manner.
    for (int i = 0; i < num_children; i++) {
        ++*node_idx;
        RETURN_IF_ERROR(create_tree_helper(state, pool, thrift_plan_nodes, descs, cur_exec_node,
                                           node_idx, nullptr));

        // we are expecting a child, but have used all nodes
        // this means we have been given a bad tree and must fail
        if (*node_idx >= thrift_plan_nodes.size()) {
            // TODO: print thrift msg
            return Status::InternalError("Failed to reconstruct plan tree from thrift.");
        }
    }
    ...
    // Step 3 Init myself after sub ExecNode tree is created and initialized
    RETURN_IF_ERROR(cur_exec_node->init(cur_plan_node, state));
}
```
其中 Step 1 主要完成的是构造函数的调用
```cpp
Status ExecNode::create_node(RuntimeState* state, ObjectPool* pool, const TPlanNode& tnode,
                             const DescriptorTbl& descs, ExecNode** node) {
  ...
  case TPlanNodeType::OLAP_SCAN_NODE:
        *node = pool->add(new vectorized::NewOlapScanNode(pool, tnode, descs));
        return Status::OK();
}

```
每个Node还可能需要执行表达式，比如 `SELECT * FROM table where a=b;` 这里的 `a=b` 会对应 Plan 中的 PREDICATES，本质是一个表达式，这个表达式是在 `ExecNode::init` 中构造的
```cpp
Status ExecNode::init(const TPlanNode& tnode, RuntimeState* state) {
    ...
    if (tnode.__isset.vconjunct) {
        vectorized::VExprContextSPtr context;
        RETURN_IF_ERROR(vectorized::VExpr::create_expr_tree(tnode.vconjunct, context));
        _conjuncts.emplace_back(context);
    }

    // create the projections expr
    if (tnode.__isset.projections) {
        DCHECK(tnode.__isset.output_tuple_id);
        RETURN_IF_ERROR(vectorized::VExpr::create_expr_trees(tnode.projections, _projections));
    }

    return Status::OK();
}
```
表达式的结构也是一棵树，具体怎么构造整个树的其实也是一个多叉树的遍历过程，就不列出代码了，只看一下各个节点是怎么构造的:
```cpp
Status VExpr::create_expr(const TExprNode& expr_node, VExprSPtr& expr) {
    ...
    case TExprNodeType::ARITHMETIC_EXPR:
        case TExprNodeType::BINARY_PRED:
        case TExprNodeType::FUNCTION_CALL:
        case TExprNodeType::COMPUTE_FUNCTION_CALL: {
            expr = VectorizedFnCall::create_shared(expr_node);
            break;
        }
    ...
}
```

### ExecNode Prepare
ExecNode 的构造阶段只是调用了一些构造函数，具体来说，对于每个 ExecNode 中的表达式，在构造阶段我们只是进行了如下第3行的赋值操作：
```cpp {.line-numbers}
class VectorizedFnCall : public VExpr {
public:
    VectorizedFnCall::VectorizedFnCall(const TExprNode& node) : VExpr(node) {}
    ...
protected:
    FunctionBasePtr _function;
    bool _can_fast_execute = false;
    std::string _expr_name;
    std::string _function_name;
}
```
具体每个需要执行的函数是什么都是在 ExecNode 的 prepare 阶段完成的：
```cpp
Status ExecNode::prepare(RuntimeState* state) {
    // Step 1. Profile 相关的数据结构
    ...
    // Step 2. conjunction prepare
    for (auto& conjunct : _conjuncts) {
        RETURN_IF_ERROR(conjunct->prepare(state, intermediate_row_desc()));
    }
    // Step 3. projections
    RETURN_IF_ERROR(vectorized::VExpr::prepare(_projections, state, intermediate_row_desc()));

    // Step 4. child exec node prepare.
    for (int i = 0; i < _children.size(); ++i) {
        RETURN_IF_ERROR(_children[i]->prepare(state));
    }
    return Status::OK();
}
```
conjunction 与 projection 中都是有可能有表达式的，这里只以 conjunction 为例子
```cpp
Status VExprContext::prepare(RuntimeState* state, const RowDescriptor& row_desc) {
    _prepared = true;
    Status st;
    RETURN_IF_CATCH_EXCEPTION({ st = _root->prepare(state, row_desc, this); });
    return st;
}
```
这里的 _root 实际上是 `VExpr`，所以会进行虚函数调用。以 `VectorizedFnCall` 为例:
```cpp
Status VectorizedFnCall::prepare(RuntimeState* state, const RowDescriptor& desc,
                                 VExprContext* context) {
    // Step 1. prepare child node
    RETURN_IF_ERROR_OR_PREPARED(VExpr::prepare(state, desc, context));

    // Step 2. prepare myself
    // Step 2.1 参数列表，这里决定了 FnCall 接收的参数
     ColumnsWithTypeAndName argument_template;
    argument_template.reserve(_children.size());
    for (auto child : _children) {
        argument_template.emplace_back(nullptr, child->data_type(), child->expr_name());
    }

    // Step 2.2 根据 funtion 的类型去构造 _function 对象
    // 常见的 numbers/random 函数都是这一类
    ...
    _function = SimpleFunctionFactory::instance().get_function(
                _fn.name.function_name, argument_template, _data_type, state->be_exec_version());
    ...
    // Step 2.3 收尾工作
    VExpr::register_function_context(state, context);
    _function_name = _fn.name.function_name;
    _can_fast_execute = _function->can_fast_execute()              
}
```




到这里可以认为整个物理执行计划已经完成了在BE的内存中的构建工作，下一步需要把物理执行计划转为一个个执行单元，即 PipelineTask

### Build PipelineTasks

_build_pipelines 这个函数会被递归调用，递归的路径与 ExecNodeTree 一样，构造 PipelineTree
```cpp {.line-numbers}
// 对于每个 ExecNode，会创建一个 Operator，然后构造一个 OperatorTree
PipelineFragmentContext::_build_pipelines(ExecNode*, PipelinePtr cur_pipe)
{
    ...
    case TPlanNodeType::OLAP_SCAN_NODE:
    {
        OperatorBuilderPtr operator_t = std::make_shared<ScanOperatorBuilder>(node->id(), node);
        RETURN_IF_ERROR(cur_pipe->add_operator(operator_t));
        break;
    }
    ...
}
```
PipelineTree 构建完成后，函数 _build_pipeline_tasks 构造 PipelineTask
```cpp {.line-numbers}
PipelineFragmentContext::_build_pipeline_tasks {
    ...
    for (PipelinePtr& pipeline : _pipelines) {
        // if sink
        auto sink = pipeline->sink()->build_operator();
        static_cast<void>(sink->init(request.fragment.output_sink));

        RETURN_IF_ERROR(pipeline->build_operators());
        auto task = std::make_unique<PipelineTask>(pipeline, _total_tasks++, _runtime_state.get(),
                                                   sink, this, pipeline->pipeline_profile());
        static_cast<void>(sink->set_child(task->get_root()));
        _tasks.emplace_back(std::move(task));
    }
    ...
}

Status Pipeline::build_operators() {
    OperatorPtr pre;
    for (auto& operator_t : _operator_builders) {
        auto o = operator_t->build_operator();
        if (pre) {
            static_cast<void>(o->set_child(pre));
        }
        _operators.emplace_back(o);
        pre = std::move(o);
    }
    return Status::OK();
}
```    
具体对于 ScanOperatorBuilder 来说，其 build_operators 的实现如下：
```cpp
#define OPERATOR_CODE_GENERATOR(NAME, SUBCLASS)                       \
    NAME##Builder::NAME##Builder(int32_t id, ExecNode* exec_node)     \
            : OperatorBuilder(id, #NAME, exec_node) {}                \
                                                                      \
    OperatorPtr NAME##Builder::build_operator() {                     \
        return std::make_shared<NAME>(this, _node);                   \
    }                                                                 \
                                                                      \
    NAME::NAME(OperatorBuilderBase* operator_builder, ExecNode* node) \
            : SUBCLASS(operator_builder, node) {};

OPERATOR_CODE_GENERATOR(ScanOperator, SourceOperator)

//////// 宏替换结果：

ScanOperatorBuilder::ScanOperatorBuilder(int32_t id, ExecNode* exec_node)
        : OperatorBuilder(id, "ScanOperator", exec_node) {}

OperatorPtr ScanOperatorBuilder::build_operator() {
    return std::make_shared<ScanOperator>(this, _node);
}

ScanOperator::ScanOperator(OperatorBuilderBase* operator_builder, ExecNode* node)
        : SourceOperator(operator_builder, node) {}
```
所以 Pipeline::build_operators 执行到第 24 行时，会变成 `PipeLine._operators.emplace_back(ScanOperator)`

**到这一步完成了从 ExecNode 到 Operator 的转换**，可以认为他们两个是多对一应的关系：
`*_SCAN_NODE -> ScanOperator`，多种类型的 _SCAN_NODE 可能会产生相同类型的 ScanOperator

继续看 _build_pipeline_tasks，第 8 行 build operators 完成之后，开始 build pipeline task
```cpp {.line-numbers}
PipelineFragmentContext::_build_pipeline_tasks {
    ...
    for (PipelinePtr& pipeline : _pipelines) {
        ...
        auto task = std::make_unique<PipelineTask>(pipeline, _total_tasks++, _runtime_state.get(),
                                                   sink, this, pipeline->pipeline_profile());
        static_cast<void>(sink->set_child(task->get_root()));
        _tasks.emplace_back(std::move(task));
        ...
    }

    for (auto& task : _tasks) {
        RETURN_IF_ERROR(task->prepare(_runtime_state.get()));
    }
    ...
}


PipelineTask::PipelineTask(PipelinePtr& pipeline, uint32_t index, RuntimeState* state,
                           OperatorPtr& sink, PipelineFragmentContext* fragment_context,
                           RuntimeProfile* parent_profile)
        : _index(index),
          _pipeline(pipeline),
          _prepared(false),
          _opened(false),
          _state(state),
          _cur_state(PipelineTaskState::NOT_READY),
          _data_state(SourceState::DEPEND_ON_SOURCE),
          _fragment_context(fragment_context),
          _parent_profile(parent_profile),
          _operators(pipeline->_operators),
          _source(_operators.front()),
          _root(_operators.back()),
          _sink(sink) {
    _pipeline_task_watcher.start();
}
```
第 5 行 set sink 设置的是 operators 的最后一个元素，正好就是当前 fragment 的 sinknode（多叉树后序遍历）对应的 Operator。
第 3 行的 for loop 结束之后，所有的 PipelineTask 就完成了构建，第 12 行开始执行每个 task 的 prepare 函数
```cpp
Status PipelineTask::prepare(RuntimeState* state) {
    ...
    _init_profile();
    ...
    RETURN_IF_ERROR(_sink->prepare(state));
    for (auto& o : _operators) {
        RETURN_IF_ERROR(o->prepare(state));
    }
    ...
    _block = _fragment_context->is_group_commit() ? doris::vectorized::FutureBlock::create_unique()
                                                  : doris::vectorized::Block::create_unique();
    ...
    set_state(PipelineTaskState::RUNNABLE);
    _prepared = true;
    return Status::OK();
}
```
我们专注看一下 ScanOperator 的 prepare 过程:
```cpp
class ScanOperator : public SourceOperator<ScanOperatorBuilder> {...}

template <typename OperatorBuilderType>
class SourceOperator : public StreamingOperator<OperatorBuilderType> {...}

/**
 * All operators inherited from Operator will hold a ExecNode inside.
 */
template <typename OperatorBuilderType>
class StreamingOperator : public OperatorBase {
    using NodeType =
            std::remove_pointer_t<decltype(std::declval<OperatorBuilderType>().exec_node())>;

    StreamingOperator(OperatorBuilderBase* builder, ExecNode* node)
            : OperatorBase(builder), _node(reinterpret_cast<NodeType*>(node)) {}
    ...
    Status prepare(RuntimeState* state) override {
        _node->increase_ref();
        _use_projection = _node->has_output_row_descriptor();
        return Status::OK();
    }
}
```
做了一些必要的杂事。

回到 _build_pipeline_tasks 函数，该函数执行完成后，PipelineFragmentContext::prepare 的主要工作就完成了。

```cpp
Status PipelineFragmentContext::prepare(const doris::TPipelineFragmentParams& request,
                                        const size_t idx)
{
    ...
    _build_pipelines(_root_plan, _root_pipeline);
    if (_sink) {
        RETURN_IF_ERROR(_create_sink(request.local_params[idx].sender_id,
                                     request.fragment.output_sink, _runtime_state.get()));
    }
    _build_pipeline_tasks(request);
    _prepared = true;
    return Status::OK();
}
```
完成 instance 的 prepare 过程之后，第 9 行 开始把创建的所有 PipelineTask 添加到 Schedule 队列中。
```cpp {.line-numbers}
Status FragmentMgr::exec_plan_fragment(const TPipelineFragmentParams& params,
                                       const FinishCallback& cb)
{
    ...
    pre_and_submit = [&](int i) {
        ...
        auto prepare_st = context->prepare(params, i);
        ...
        return context->submit();
    }

    int target_size = params.local_params.size();
    for (size_t i = 0; i < target_size; ++i>) {
        _thread_pool->submit_func([&, i]() {
                        prepare_status[i] = pre_and_submit(i);
                        std::unique_lock<std::mutex> lock(m);
                        prepare_done++;
                        if (prepare_done == target_size) {
                            cv.notify_one();
                        }});
    }
}
```

```cpp
Status PipelineFragmentContext::submit() {
    ...
    _submitted = true;

    int submit_tasks = 0;
    Status st;
    auto* scheduler = _exec_env->pipeline_task_scheduler();
    if (_query_ctx->get_task_scheduler()) {
        scheduler = _query_ctx->get_task_scheduler();
    } else if (_task_group_entity) {
        scheduler = _exec_env->pipeline_task_group_scheduler();
    }
    for (auto& task : _tasks) {
        st = scheduler->schedule_task(task.get());
        ...
        submit_tasks++;
    }
    ...
    return st;
}
```
当这个 instance 所有的 PipelineTask 都提交成功后，BE 将会向 FE 返回 rpc response。到这里完成了执行一个 query 的第一个阶段，第二个阶段则开始 query 的真正执行。

## Execution

PipelineTask 构建完成后，会放进线程池中等待 task_scheduler 调度执行
```cpp
PipelineTask::execute() {
    ...
    if (!_opened) {
        return _open();
    }

    auto* block = _block.get();
    _root->get_block(_state, block, _data_state)

}
```
上述代码精简比较多。先看一下整体执行逻辑。
### PipelineTask open
```cpp
Status PipelineTask::_open() {
    ...
    for (auto& o : _operators) {
        RETURN_IF_ERROR(o->open(_state));
    }
    if (_sink) {
        RETURN_IF_ERROR(_sink->open(_state));
    }
    _opened = true;
    return Status::OK();
}
```
对于 scan 的话，执行的是 `ScanOperator::open()`。ScanOperator 继承自 SourceOperator，SourceOperatpr 继承自 StreamingOperator

```cpp {.line-numbers}
class ScanOperator : SourceOperator<ScanOperator> {...}
class SourceOperator<ScanOperator> : public StreamingOperator<OperatorBuilderType> {...}

class StreamingOperator : public OperatorBase {
public:
    using NodeType =
            std::remove_pointer_t<decltype(std::declval<OperatorBuilderType>().exec_node())>;

    StreamingOperator(OperatorBuilderBase* builder, ExecNode* node)
            : OperatorBase(builder), _node(reinterpret_cast<NodeType*>(node)) {}

    ...
    Status open(RuntimeState* state) override {
        RETURN_IF_ERROR(_node->alloc_resource(state));
        return Status::OK();
    }
    ...
}

Status ExecNode::alloc_resource(doris::RuntimeState* state) {
    for (auto& conjunct : _conjuncts) {
        RETURN_IF_ERROR(conjunct->open(state));
    }
    RETURN_IF_ERROR(vectorized::VExpr::open(_projections, state));
    return Status::OK();
}
```
这里第 14 行的是虚函数调用，对于 ScanNode 来说首先执行的是 VScanNode::alloc_resource
#### _prepare_scanners
scan 这里通常需要进行三个工作，一是从磁盘读 block，二是根据 where 条件进行数据的过滤，三是做 projections。
其中第一部分主要由 scanner 完成，第二部分可能会将一部分表达式下推到 scanner 执行，以避免读取过多的数据，另一部分则在 ScanNode 中进行。
第三部分理论上也是可以下推到 scanner 完成的。
```cpp {.line-numbers}
Status VScanNode::alloc_resource(RuntimeState* state)
{
    ...
    RETURN_IF_ERROR(_process_conjuncts());
    ...
    if (_is_pipeline_scan) {
        ...
        auto status =
                    !_eos ? _prepare_scanners(state->query_parallel_instance_num()) : Status::OK();
        if (_scanner_ctx) {
            DCHECK(!_eos && _num_scanners->value() > 0);
            RETURN_IF_ERROR(_scanner_ctx->init());
            RETURN_IF_ERROR(
                    _state->exec_env()->scanner_scheduler()->submit(_scanner_ctx.get()));
        }
    }
}

Status VScanNode::_prepare_scanners(const int query_parallel_instance_num) {
    std::list<VScannerSPtr> scanners;
    RETURN_IF_ERROR(_init_scanners(&scanners));
    if (scanners.empty()) {
        _eos = true;
    } else {
        COUNTER_SET(_num_scanners, static_cast<int64_t>(scanners.size()));
        RETURN_IF_ERROR(_start_scanners(scanners, query_parallel_instance_num));
    }
    return Status::OK();
}
```
上面第 4 行的 _process_conjuncts 就是决定哪些表达式可以下推到 scanner。

#### _init_scanners
```cpp {.line-numbers}
Status NewOlapScanNode::_init_scanners(std::list<VScannerSPtr>* scanners) {
    ...
    if (!_olap_scan_node.output_column_unique_ids.empty()) {
        for (auto uid : _olap_scan_node.output_column_unique_ids) {
            _maybe_read_column_ids.emplace(uid);
        }
    }

    ...
    auto build_new_scanner = [&](const TPaloScanRange& scan_range,
                                    const std::vector<OlapScanRange*>& key_ranges) {
        std::shared_ptr<NewOlapScanner> scanner = NewOlapScanner::create_shared(
                _state, this, _limit_per_scanner, _olap_scan_node.is_preaggregation, scan_range,
                key_ranges, _scanner_profile.get());

        RETURN_IF_ERROR(scanner->prepare(_state, _conjuncts));
        scanner->set_compound_filters(_compound_filters);
        scanners->push_back(scanner);
        return Status::OK();
    };
    
    for (auto& scan_range : _scan_ranges) {
        std::vector<std::unique_ptr<doris::OlapScanRange>>* ranges = &_cond_ranges;
        ...
        int num_ranges = ranges->size();
        for (int i = 0; i < num_ranges;) {
            std::vector<doris::OlapScanRange*> scanner_ranges;
            scanner_ranges.push_back((*ranges)[i].get());
            ++i;
            for (int j = 1; i < num_ranges && j < ranges_per_scanner &&
                            (*ranges)[i]->end_include == (*ranges)[i - 1]->end_include;
                    ++j, ++i) {
                scanner_ranges.push_back((*ranges)[i].get());
            }
            RETURN_IF_ERROR(build_new_scanner(*scan_range, scanner_ranges));
        }
    }
    return Status::OK();
}
```
_init_scanner 函数主要完成两件事：
1. 根据 tablet 的类型以及数据分布，决定创建多少个 scan range,（计算过程还有其他参数）
2. 对于每个scan range，创建一个 scanner

```cpp
Status VScanNode::_start_scanners(const std::list<VScannerSPtr>& scanners,
                                  const int query_parallel_instance_num) {
    if (_is_pipeline_scan) {
        int max_queue_size = _shared_scan_opt ? std::max(query_parallel_instance_num, 1) : 1;
        _scanner_ctx = pipeline::PipScannerContext::create_shared(
                _state, this, _output_tuple_desc, scanners, limit(), _state->scan_queue_mem_limit(),
                _col_distribute_ids, max_queue_size);
    } else {
        _scanner_ctx = ScannerContext::create_shared(_state, this, _output_tuple_desc, scanners,
                                                     limit(), _state->scan_queue_mem_limit());
    }
    return Status::OK();
}
```
__start_scanners 完成后就会把 scanner 添加到 scanner scheduler 中

### Scanner 
Doris 在 BE 单个进程内部的执行模式是基于 pull 模型的，即上层的 Node 通过调用下层 Node 的类似 getNext 方法获取一个 batch 的数据。如果把 ScanNode 与 Scanner 视为一个整体，那么这个整体也是这个 pull 模型的一部分。但是如果把 ScanNode 与 Scanner 分开看（分别在不同的线程中执行），那么 ScanNode 与 Scanner 之间则是 push 模型：Scanner 将处理之后的数据 push 到 ScanNode。
#### VScanNode::get_next
```cpp {.line-numbers}
Status VScanNode::get_next(RuntimeState* state, vectorized::Block* block, bool* eos) {
    ...

    _scanner_ctx->get_block_from_queue(state, &scan_block, eos, _context_queue_id);
    
    // get scanner's block memory
    block->swap(*scan_block);
    _scanner_ctx->return_free_block(std::move(scan_block));
    ...
    return Status::OK();
}

Status ScannerContext::get_block_from_queue(RuntimeState* state, vectorized::BlockUPtr* block,
                                            bool* eos, int id, bool wait) {
    std::unique_lock l(_transfer_lock);
    ...
    // Wait for block from queue
    if (wait) {
        // scanner batch wait time
        SCOPED_TIMER(_scanner_wait_batch_timer);
        while (!(!_blocks_queue.empty() || _is_finished || !status().ok() ||
                 state->is_cancelled())) {
            _blocks_queue_added_cv.wait(l);
        }
    }
    ...

    *block = std::move(_blocks_queue.front());
    _blocks_queue.pop_front();

    RETURN_IF_ERROR(validate_block_schema((*block).get()));

    auto block_bytes = (*block)->allocated_bytes();
    _cur_bytes_in_queue -= block_bytes;
    return Status::OK();
}
```
上述代码中精简掉了很多状态检查逻辑。总的来说，就是当 VScanNode::get_next 被调用时，它会阻塞在 ScannerContext::get_block_from_queue 中，等待 Scanner 给 _blocks_queue 中添加 block。
Scanner 每次被调度执行时都会执行到 ScannerScheduler::_scanner_scan
```cpp {.line-numbers}
void ScannerScheduler::_scanner_scan(ScannerScheduler* scheduler, ScannerContext* ctx,
                                     VScannerSPtr scanner) {
    ...
    if (!scanner->is_init()) {
        status = scanner->init();
        ...
    }
    if (!eos && !scanner->is_open()) {
        status = scanner->open(state);
        ...
    }
    ...
    std::vector<vectorized::BlockUPtr> blocks;
    int64_t raw_rows_read = scanner->get_rows_read();
    ...
    while (!eos && raw_bytes_read < raw_bytes_threshold &&
           (raw_rows_read < raw_rows_threshold || num_rows_in_block < state->batch_size())) {
        ...
        status = scanner->get_block(state, block.get(), &eos);
        raw_bytes_read += block->allocated_bytes();
        num_rows_in_block += block->rows();
        ...
        blocks.push_back(std::move(block));
    }
    ...
    ctx->append_blocks_to_queue(blocks);
    ctx->push_back_scanner_and_reschedule(scanner);
}
```
当 Scanner::get_block 执行时，会进行之前提到的读数据，以及根据表达式过滤数据的工作
```cpp {.line-numbers}
Status VScanner::get_block(RuntimeState* state, Block* block, bool* eof) {
    ...
    {
        do {
            // if step 2 filter all rows of block, and block will be reused to get next rows,
            // must clear row_same_bit of block, or will get wrong row_same_bit.size() which not equal block.rows()
            block->clear_same_bit();
            // 1. Get input block from scanner
            {
                // get block time
                auto timer = _parent ? _parent->_scan_timer : _local_state->_scan_timer;
                SCOPED_TIMER(timer);
                RETURN_IF_ERROR(_get_block_impl(state, block, eof));
                if (*eof) {
                    DCHECK(block->rows() == 0);
                    break;
                }
                _num_rows_read += block->rows();
            }

            // 2. Filter the output block finally.
            {
                auto timer = _parent ? _parent->_filter_timer : _local_state->_filter_timer;
                SCOPED_TIMER(timer);
                RETURN_IF_ERROR(_filter_output_block(block));
            }
            // record rows return (after filter) for _limit check
            _num_rows_return += block->rows();
        } while (!state->is_cancelled() && block->rows() == 0 && !(*eof) &&
                 _num_rows_read < rows_read_threshold);
    }
    ...
}
```
VScanner::get_block 主要完成两部，一步是从 tablet 读数据，第二步是按照表达式过滤数据。
```cpp {.line-numbers}
Status NewOlapScanner::_get_block_impl(RuntimeState* state, Block* block, bool* eof) {
    // Read one block from block reader
    // ATTN: Here we need to let the _get_block_impl method guarantee the semantics of the interface,
    // that is, eof can be set to true only when the returned block is empty.
    RETURN_IF_ERROR(_tablet_reader->next_block_with_aggregation(block, eof));
    if (!_profile_updated) {
        _profile_updated = _tablet_reader->update_profile(_profile);
    }
    if (block->rows() > 0) {
        *eof = false;
    }
    _update_realtime_counters();
    return Status::OK();
}

Status VScanner::_filter_output_block(Block* block) {
    if (block->has(BeConsts::BLOCK_TEMP_COLUMN_SCANNER_FILTERED)) {
        // scanner filter_block is already done (only by _topn_next currently), just skip it
        return Status::OK();
    }
    auto old_rows = block->rows();
    Status st = VExprContext::filter_block(_conjuncts, block, block->columns());
    _counter.num_rows_unselected += old_rows - block->rows();
    auto all_column_names = block->get_names();
    for (auto& name : all_column_names) {
        if (name.rfind(BeConsts::BLOCK_TEMP_COLUMN_PREFIX, 0) == 0) {
            block->erase(name);
        }
    }
    return st;
}
```
对于 OLAP table，next_block_with_aggregation 方法到最后会执行到 `VerticalBlockReader::_next_block_func`，根据不同的 KEY 类型，该函数有不同的实现
```cpp
Status VerticalBlockReader::next_block_with_aggregation(Block* block, bool* eof) {
    auto res = (this->*_next_block_func)(block, eof);
    ...
    return res;
}

Status VerticalBlockReader::init(const ReaderParams& read_params) {
    ...
    TabletReader::init(read_params);

    auto status = _init_collect_iter(read_params);
    ...
    switch (tablet()->keys_type()) {
    case KeysType::DUP_KEYS:
        _next_block_func = &VerticalBlockReader::_direct_next_block;
        break;
    case KeysType::UNIQUE_KEYS:
        _next_block_func = &VerticalBlockReader::_unique_key_next_block;
        if (_filter_delete) {
            _delete_filter_column = ColumnUInt8::create();
        }
        break;
    case KeysType::AGG_KEYS:
        _next_block_func = &VerticalBlockReader::_agg_key_next_block;
        if (!read_params.is_key_column_group) {
            _init_agg_state(read_params);
        }
        break;
    ...
    return Status::OK();
    }
}
```
