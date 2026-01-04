```text
ShardId
    |
    TabletId
        |
        SchemaHash
            |
            RowSetId_SegmentId.dat
```
```cpp
Status DeltaWriter::write(const vectorized::Block* block, const DorisVector<uint32_t>& row_idxs) {
    if (UNLIKELY(row_idxs.empty())) {
        return Status::OK();
    }
    _lock_watch.start();
    std::lock_guard<std::mutex> l(_lock);
    _lock_watch.stop();
    if (!_is_init && !_is_cancelled) {
        RETURN_IF_ERROR(init());
    }
    {
        SCOPED_TIMER(_wait_flush_limit_timer);
        while (_memtable_writer->flush_running_count() >=
               config::memtable_flush_running_count_limit) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    return _memtable_writer->write(block, row_idxs);
}
```
### Light Schema Change 与数据导入
```cpp
Status RowsetBuilder::init() {
    ...
    // build tablet schema in request level
    RETURN_IF_ERROR(_build_current_tablet_schema(_req.index_id, _req.table_schema_param.get(),
                                                 *_tablet->tablet_schema()));
    ...
}
```
`_build_current_tablet_schema`：Align the BE write schema with the FE-provided index schema for the current write, handling schema evolution, index selection, and partial-update configuration. It ensures the RowsetWriter writes data with the correct TabletSchema for a specific index_id.

```cpp
Status MemTableWriter::write(const vectorized::Block* block,
                             const DorisVector<uint32_t>& row_idxs) {
    auto st = _mem_table->insert(block, row_idxs);
    ...
    if (UNLIKELY(_mem_table->need_flush())) {
        RETURN_IF_ERROR(_flush_memtable());
    }
}
```
`MemTableWriter::_flush_memtable`实际上是提交了一个 MemtableFlushTask。执行 flush 动作：
```cpp
void FlushToken::_flush_memtable(std::shared_ptr<MemTable> memtable_ptr, int32_t segment_id,
                                 int64_t submit_task_time) {
    ...
    RETURN_IF_ERROR(_rowset_writer->flush_memtable(block.get(), segment_id, flush_size));
}
```
```cpp
Status BaseBetaRowsetWriter::flush_memtable(vectorized::Block* block, int32_t segment_id,
                                            int64_t* flush_size) {
    ...
    RETURN_IF_ERROR(_segment_creator.flush_single_block(block, segment_id, flush_size));
    ...
    return Status::OK();
}

Status SegmentFlusher::flush_single_block(const vectorized::Block* block, int32_t segment_id,
                                          int64_t* flush_size) {
    ...
    std::unique_ptr<segment_v2::VerticalSegmentWriter> writer;
    RETURN_IF_ERROR(_create_segment_writer(writer, segment_id, no_compression));
    RETURN_IF_ERROR_OR_CATCH_EXCEPTION(_add_rows(writer, &flush_block, 0, flush_block.rows()));
    RETURN_IF_ERROR(_flush_segment_writer(writer, flush_size));
}
```
`SegmentFlusher::_create_segment_writer()` 会使用创建 RowsetWriter 时候构造的 tablet schema 来决定某张表的 schema。
```cpp
Status SegmentFlusher::_create_segment_writer(
        std::unique_ptr<segment_v2::VerticalSegmentWriter>& writer, int32_t segment_id,
        bool no_compression) {
    io::FileWriterPtr segment_file_writer;
    RETURN_IF_ERROR(_context.file_writer_creator->create(segment_id, segment_file_writer));

    IndexFileWriterPtr index_file_writer;
    if (_context.tablet_schema->has_inverted_index() || _context.tablet_schema->has_ann_index()) {
        RETURN_IF_ERROR(_context.file_writer_creator->create(segment_id, &index_file_writer));
    }
}
```
