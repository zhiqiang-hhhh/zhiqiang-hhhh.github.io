Fe 上 Coordinator 会构造一个 receiver
```java
public class StmtExecutor {
    ...
    private void sendResult(...) {
        ...
        // 读 PlanFragment 写 TPipelineFragmentParams
        // 写 reveiver
        // 读 TPipelineFragmentParams 写 rpc 
        coord.exec();   
        ...
        while(true) {
            ...
            batch = coord.getNext()
            ...
        }
    }
}



public class Coordinator {
    ...
    public RowBatch getNext() {
        ...
        resultBatch = receiver.getNext(status);
        ...
    }
}

public class ResultReceiver {
    ...
    public RowBatch getNext(Status) {
        ...
        while(!isDone && !isCancel) {
            InternalService.PFetchDataRequest request;
            Future<InternalService.PFetchDataResult> future
                        = BackendServiceProxy.getInstance().fetchDataAsync(address, request);
            ...
            InternalService.PFetchDataResult pResult = null;
            while (pResult == null) {
                ...
                pResult = future.get(timeoutTs - currentTs, TimeUnit.MILLISECONDS);
            }
        }
    }
}
```


```proto
message PFetchDataRequest {
    required PUniqueId finst_id = 1;
    optional bool resp_in_attachment = 2;
};
```
finst_id 是构造 receiver 时候产生的一个随机数，似乎乎是用来标记 receiver 的，完全搞不明白这个变量命名是啥意思。。。但是隐约能猜到该变量的作用：在 send framgent 的时候应该会带上该参数，用来标记本次 query，后续 fetch data 的时候 be 会根据该参数寻找相关的 result sender 数据结构。

```cpp
void ResultBufferMgr::fetch_data(const PUniqueId& finst_id, GetResultBatchCtx* ctx)
{
    TUniqueId tid;
    tid.__set_hi(finst_id.hi());
    tid.__set_lo(finst_id.lo());
    std::shared_ptr<BufferControlBlock> cb = find_control_block(tid);
    if (cb == nullptr) {
        LOG(WARNING) << "no result for this query, id=" << tid;
        ctx->on_failure(Status::InternalError("no result for this query"));
        return;
    }
    cb->get_batch(ctx);
}

std::shared_ptr<BufferControlBlock> ResultBufferMgr::find_control_block(const TUniqueId& query_id) {
    // TODO(zhaochun): this lock can be bottleneck?
    std::lock_guard<std::mutex> l(_lock);
    BufferMap::iterator iter = _buffer_map.find(query_id);

    if (_buffer_map.end() != iter) {
        return iter->second;
    }

    return std::shared_ptr<BufferControlBlock>();
}

```
好吧，这个参数实际上是 query_id！头疼。

看一下这个 BufferControlBlock 是啥时候构造的：
```cpp
Status ResultBufferMgr::create_sender(const TUniqueId& query_id, int buffer_size,
                                      std::shared_ptr<BufferControlBlock>* sender,
                                      bool enable_pipeline, int exec_timout)
{
    *sender = find_control_block(query_id);
    ...
    control_block = std::make_shared<PipBufferControlBlock>(query_id, buffer_size);
    
    {
        ...
        std::lock_guard<std::mutex> l(_lock);
        _buffer_map.insert(std::make_pair(query_id, control_block));
        int64_t max_timeout = time(nullptr) + exec_timout + 5;
        cancel_at_time(max_timeout, query_id);
    }
}

Status VResultSink::prepare(RuntimeState* state) {
    ...
    // create sender
    RETURN_IF_ERROR(state->exec_env()->result_mgr()->create_sender(
            state->fragment_instance_id(), _buf_size, &_sender, state->enable_pipeline_exec(),
            state->execution_timeout()));

    ...
    return Status::OK();
}
```
VResultSink 继承自 DataSink，统一在 DataSink 的工厂函数 create_data_sink 中进行构造，可以看到只会在 sink 类型为 RESULT_SINK 时才会构造 VResultSink 对象。

```cpp
Status DataSink::create_data_sink(ObjectPool* pool, const TDataSink& thrift_sink,
                                  const std::vector<TExpr>& output_exprs,
                                  const TPlanFragmentExecParams& params,
                                  const RowDescriptor& row_desc, RuntimeState* state,
                                  std::unique_ptr<DataSink>* sink, DescriptorTbl& desc_tbl)
{
    switch (thrift_sink.type) {
        case TDataSinkType::DATA_STREAM_SINK: {
            ...
        }
        case TDataSinkType::RESULT_SINK: {
            if (!thrift_sink.__isset.result_sink) {
                return Status::InternalError("Missing data buffer sink.");
            }

            // TODO: figure out good buffer size based on size of output row
            sink->reset(new doris::vectorized::VResultSink(row_desc, output_exprs,
                                                        thrift_sink.result_sink, 4096));
            break;
        }
        ...
    }
}

Status PipelineFragmentContext::prepare(const doris::TPipelineFragmentParams& request,
                                        const size_t idx)
{
    ...
    if (request.fragment.__isset.output_sink) {
        RETURN_IF_ERROR_OR_CATCH_EXCEPTION(DataSink::create_data_sink(
                _runtime_state->obj_pool(), request.fragment.output_sink,
                request.fragment.output_exprs, request, idx, _root_plan->row_desc(),
                _runtime_state.get(), &_sink, *desc_tbl));
    }
    ...
    RETURN_IF_ERROR(_build_pipelines(_root_plan, _root_pipeline));

}
```
也就是说在 `PipelineFragmentContext::prepare` 执行时，会构造 VResultSink 对象，该对象保存在 `PipelineFragmentContext::_sink` 中

构造该对象之后，
```cpp
Status PipelineFragmentContext::_build_pipeline_tasks(
    const doris::TPipelineFragmentParams& request) {
    ...
    for (auto& task : _tasks) {
        RETURN_IF_ERROR(task->prepare(_runtime_state.get()));
    }
    ...
}
```
这里 task::prepare 函数里会调用 VResultSink::prepare，最后执行到 `ResultBufferMgr::create_sender` 创建 PipBufferControlBlock，并且把该对象添加到 ResultBufferMgr 中。


```cpp
void ResultBufferMgr::fetch_data(const PUniqueId& finst_id, GetResultBatchCtx* ctx) {
    TUniqueId tid;
    tid.__set_hi(finst_id.hi());
    tid.__set_lo(finst_id.lo());
    std::shared_ptr<BufferControlBlock> cb = find_control_block(tid);
    if (cb == nullptr) {
        LOG(WARNING) << "no result for this query, id=" << tid;
        ctx->on_failure(Status::InternalError("no result for this query"));
        return;
    }
    cb->get_batch(ctx);
}

void BufferControlBlock::get_batch(GetResultBatchCtx* ctx) {
    std::lock_guard<std::mutex> l(_lock);
    if (!_status.ok()) {
        ctx->on_failure(_status);
        return;
    }
    if (_is_cancelled) {
        ctx->on_failure(Status::Cancelled("Cancelled"));
        return;
    }
    if (!_batch_queue.empty()) {
        // get result
        std::unique_ptr<TFetchDataResult> result = std::move(_batch_queue.front());
        _batch_queue.pop_front();
        _buffer_rows -= result->result_batch.rows.size();
        _data_removal.notify_one();

        ctx->on_data(result, _packet_num);
        _packet_num++;
        return;
    }
    if (_is_close) {
        ctx->on_close(_packet_num, _query_statistics.get());
        return;
    }
    // no ready data, push ctx to waiting list
    _waiting_rpc.push_back(ctx);
}
```