
```cpp
Status NewOlapScanner::init() {
    ...
    _tablet_reader = std::make_unique<BlockReader>();
}

Status NewOlapScanner::open(RuntimeState* state) {
    RETURN_IF_ERROR(VScanner::open(state));

    auto res = _tablet_reader->init(_tablet_reader_params);
    ...
}

Status TabletReader::init(const ReaderParams& read_params) {
    _predicate_arena = std::make_unique<vectorized::Arena>();

    Status res = _init_params(read_params);
    ...
}

Status TabletReader::_init_params(const ReaderParams& read_params) {
    ...
    Status res = _init_delete_condition(read_params);
    ...
}

Status TabletReader::_init_delete_condition(const ReaderParams& read_params) {
    ...
    return _delete_handler.init(_tablet_schema, read_params.delete_predicates,
                                read_params.version.second, enable_sub_pred_v2);    
}
```

----
```cpp
template <typename Derived>
Status ScanLocalState<Derived>::open(RuntimeState* state) {
    ...
    auto status = _eos ? Status::OK() : _prepare_scanners();
    if (_scanner_ctx) {
        DCHECK(!_eos && _num_scanners->value() > 0);
        RETURN_IF_ERROR(_scanner_ctx->init());
    }
}

template <typename Derived>
Status ScanLocalState<Derived>::_prepare_scanners() {
    // 创建的是 Scanner 对象
    std::list<vectorized::VScannerSPtr> scanners;
    RETURN_IF_ERROR(_init_scanners(&scanners));
    
    // 创建的是 ScannerDelegate 对象
    for (auto it = scanners.begin(); it != scanners.end(); ++it) {
        _scanners.emplace_back(std::make_shared<vectorized::ScannerDelegate>(*it));
    }
    if (scanners.empty()) {
        _eos = true;
        _scan_dependency->set_always_ready();
    } else {
        for (auto& scanner : scanners) {
            scanner->set_query_statistics(_query_statistics.get());
        }
        COUNTER_SET(_num_scanners, static_cast<int64_t>(scanners.size()));
        RETURN_IF_ERROR(_start_scanners(_scanners));
    }
    return Status::OK();
}

Status OlapScanLocalState::_init_scanners(std::list<vectorized::VScannerSPtr>* scanners) {
    // 根据策略去创建scanner
    ...
}

template <typename Derived>
Status ScanLocalState<Derived>::_start_scanners(
    const std::list<std::shared_ptr<vectorized::ScannerDelegate>>& scanners) {
        _scanner_ctx = vectorized::ScannerContext::create_shared(
            ..., scanners, ... );
    ...
}
```

`Scan Node` 所在的 Pipeline 会有 ParallelPipelineTaskNum 个 PipelineTask
每个 PipelineTask 会根据策略创建 X 个 Scanner 对象
每个 PipelineTask 在 open 的时候会创建一个 ScannerContext，这个 ScannerContext 管理这个 PipelineTask 创建的 Scanner 对象。
每个 PipelineTask 创建的 Scanner 对象数量与 Tablet 数量有关，具体来说是一个策略问题。

ScanNodeOperator会创建 parallel_pipeline_task 个 pipeline task，每个 pipeline task 会创建 X 个 scanner，X 等于多少是一个策略问题，总的 scanner 数量是 parallel_pipeline_task * X，策略的原则是这些所有的 scanner 去读所有涉及到的 tablet，互不重叠。PipelineTask A 只会从自己创建的那 X 个scanner中消费block，所以各个pipeline task 之间的数据也是不重叠的。
```cpp
template <typename Derived>
Status ScanLocalState<Derived>::open(RuntimeState* state) {
    ...
    auto status = _eos ? Status::OK() : _prepare_scanners();
    RETURN_IF_ERROR(status);
    if (_scanner_ctx) {
        RETURN_IF_ERROR(_scanner_ctx->init());
    }
}

Status ScannerContext::init() {
    ...
    for (int i = 0; i < _max_thread_num; ++i) {
        std::weak_ptr<ScannerDelegate> next_scanner;
        if (_scanners.try_dequeue(next_scanner)) {
            this->submit_scan_task(std::make_shared<ScanTask>(next_scanner));
            _num_running_scanners++;
        }
    }
}

void ScannerContext::submit_scan_task(std::shared_ptr<ScanTask> scan_task) {
    _scanner_sched_counter->update(1);
    _num_scheduled_scanners++;
    _scanner_scheduler->submit(shared_from_this(), scan_task);
}
```
`_max_thread_num` 是根据策略计算出来的一个值，普通情况下可以假设这里的值等于 `Scanner` 对象的数量。

那么对于每个 Scanner，我们会创建一个 `ScannerTask`，然后提交给 `_scanner_scheduler`

----

```cpp
void ScannerScheduler::submit(std::shared_ptr<ScannerContext> ctx,
                              std::shared_ptr<ScanTask> scan_task) {
    
}
```
----
PipelinTask 在 open 阶段调用所有 operator 的 open 函数，对于 OlapScanOperator 来说，就是完成上面的 Scanner 相关数据结构的创建，并且把 ScanTask 提交到 ScannerScheduler 中。
然后 PipelineTask 开始调用 get block
```cpp
template <typename LocalStateType>
Status ScanOperatorX<LocalStateType>::get_block(RuntimeState* state, vectorized::Block* block,
                                                bool* eos) {
    ...
    RETURN_IF_ERROR(local_state._scanner_ctx->get_block_from_queue(state, block, eos, 0));
    ...
}

Status ScannerContext::get_block_from_queue(RuntimeState* state, vectorized::Block* block,
                                            bool* eos, int id) {
    ...
    if (!_blocks_queue.empty() && !done()) {
        
    }
}
```