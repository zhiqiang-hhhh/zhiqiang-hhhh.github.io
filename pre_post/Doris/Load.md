Doris 导入流程提供的语意：一次导入完全成功或者完全失败。用事务 ID 来标记一次导入行为，用版本来维护每个 tablet 对用户可见的数据范围。
元数据是用来驱动可见性的载体，通过维护元数据列表来实现多版本控制。
元数据有哪些：
* Tablet 元数据: Tablet 上有哪些 rowset、哪些版本可见、哪些是 stale 状态（等待被回收）。
* Rowset 元数据: 一次生成的数据文件集合（segments）的描述信息，包含：版本、行数、大小、schema hash、生成时间、数据位置等。
* MOW 场景里还有 Delete Bitmap / Partial Update 信息：用来支撑 unique key merge-on-write 的可见性与一致性。


- 写入阶段（txn 进行中）

BE 接收数据，生成 segment 文件，形成一个“候选 rowset”。
这时候它的元数据处于 pending/非可见 状态：即使 BE 已经写出了数据文件，查询也不会读到它。
BE/FE 会记录“这个 txn_id 在哪些 tablets 上写了哪些东西”的跟踪信息（用于 commit/publish 时对齐）。
- Commit（单个BE执行成功）

单个 BE 完成某个 txn 的写入操作，添加元数据到 rocksdb，注意此时这批数据的版本号为 0，还不具备查询条件，版本号将会在 publish 的时候分配。FE 会收到各个 BE 的写入完成/提交信息，认为这批数据“已经具备发布条件”。

- Publish（真正让数据可见的元数据原子切换）

FE 下发 publish 任务到各个 BE：为每个 tablet 分配要发布的 version。
BE 做几件关键的元数据动作：
把 rowset 标记为 visible（绑定到目标 version）。
持久化 rowset_meta（以及 MOW 的 delete bitmap 等必要信息）。
更新 tablet 的版本视图：把“新版本”纳入可见集合。
清理 txn 跟踪信息。
这一步完成后，查询才会在版本线上“看到”这批数据。

- 失败与恢复语义

如果 publish 失败，Doris 依赖“持久化的元数据 + 可重试的 publish 协调”来保证最终一致：要么重试 publish 完成可见，要么超时/回滚清理 pending 数据。
关键点：可见性切换是由持久化元数据决定的，重启后依然能恢复到一致状态。

Doris 的“事务”（用户侧）本质是：把一批新数据作为整体发布成新的可见版本。
元数据管理围绕两件事展开：
* 写入阶段允许不稳定/可失败（pending，不影响查询）
* publish/切换阶段必须原子、可持久化、可恢复（决定可见性）
* compaction 虽然不是用户事务，但在元数据上同样遵循“先生成新 rowset → 再原子替换 → 旧的变 stale → 异步回收”

### 文件结构
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
