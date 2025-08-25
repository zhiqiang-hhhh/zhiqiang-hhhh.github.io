```plantuml
class AnnIndexColumnWriter {
    + init() : Status
    -  IndexFileWriter* _index_file_writer;
}

class IndexFileWriter {
    - InvertedIndexDirectoryMap _indices_dirs;
    - io::FileSystemSharedPtr _fs;
    - std::string _index_path_prefix;
    - std::string _rowset_id;
    - int64_t _seg_id;
    - InvertedIndexStorageFormatPB _storage_format;
    - std::string _tmp_dir;
    - const std::shared_ptr<io::LocalFileSystem>& _local_fs;
    - io::FileWriterPtr _idx_v2_writer
    IndexStorageFormatPtr _index_storage_format
}

class SegmentCreator {
    - SegmentFlusher _segment_flusher;
}

class SegmentFlusher {
    RowsetWriterContext& _context;
    SegmentFileCollection& _seg_files;
    InvertedIndexFileCollection& _idx_files;
}

```

```text
BetaRowsetWriterV2::flush_memtable()
|
|-SegmentFlusher::_flush_segment_writer
    |
    |-SegmentWriter::close_inverted_index()
        |
        |-IndexFileWriter::close()
```

```
SegmentFlusher::_flush_segment_writer
|
|-SegmentWriter::finalize
    |
    |-SegmentWriter::finalize_columns_index
        |
        |-SegmentWriter::_write_ann_index();
            |
            |-ArrayColumnWriter::write_ann_index()
                |
                |-AnnIndexColumnWriter->finish()
                    |
                    |-VectorIndex::save()
|
|-SegmentWriter::close_inverted_index()
    |
    |-IndexFileWriter::close()
```

```cpp
IndexFileWriter::close() {
    ...
    // Create compounded index file here.
    RETURN_IF_ERROR(_index_storage_format->write());

    for (const auto& entry : _indices_dirs) {
        const auto& dir = entry.second;
        // delete index path, which contains separated inverted index files
        if (std::strcmp(dir->getObjectName(), "DorisFSDirectory") == 0) {
            auto* compound_dir = static_cast<DorisFSDirectory*>(dir.get());
            compound_dir->deleteDirectory();
        }
    }
    ...
}
```
`IndexStorageFormatV2::write()` 会把当前 segment 对应的多个 tmp 目录下的索引文件打包压错成一个最终的 `.idx` 文件






```
zhiqiang-hhhh: I20250813 14:42:41.910436 3557178 faiss_vector_index.cpp:382] Faiss index saved to DorisRAMFSDirectory@/mnt/disk4/hezhiqiang/workspace/doris/cmaster/RELEASE/be1/storage/tmp/0200000000000001b34815d564ec81d95bed5cc408068b91_0_1755055107557, faiss.idx, rows 1, cost 0 ms
I20250813 14:42:41.910544 3557178 index_file_writer.cpp:225] IndexFileWriter closing, enable_write_index_searcher_cache: true
I20250813 14:42:41.910559 3557178 index_file_writer.cpp:122] IndexFileWriter::add_into_searcher_cache for /0200000000000001b34815d564ec81d95bed5cc408068b91_0
I20250813 14:42:41.910567 3557178 index_file_writer.cpp:125] IndexFileWriter::add_into_searcher_cache for /0200000000000001b34815d564ec81d95bed5cc408068b91_0
I20250813 14:42:41.910599 3557178 index_file_writer.cpp:127] IndexFileWriter::add_into_searcher_cache for /0200000000000001b34815d564ec81d95bed5cc408068b91_0, st: [E-6003]inverted index file /0200000000000001b34815d564ec81d95bed5cc408068b91_0.idx is not found.
I20250813 14:42:41.910611 3557178 index_file_writer.cpp:132] IndexFileWriter::add_into_searcher_cache for /0200000000000001b34815d564ec81d95bed5cc408068b91_0, no file found
I20250813 14:42:41.910619 3557178 segment_creator.cpp:248] tablet_id:1755055107539, flushing rowset_dir: , rowset_id:0200000000000001b34815d564ec81d95bed5cc408068b91, data size:526.00 B, index size:559

在什么情况下 index_file_writer会使用 StreamSinkFileWriter

GitHub Copilot: I'll scan the codebase to find where IndexFileWriter constructs a FileWriter and see the branch that returns a StreamSinkFileWriter, so I can state the exact conditions. I'll search for places where IndexFileWriter is constructed and where StreamSinkFileWriter is created, to identify the conditions leading to using it. I’ll pinpoint when IndexFileWriter is given a StreamSinkFileWriter and why your log shows “no file found”.

Checklist:
- Identify who constructs IndexFileWriter’s underlying FileWriter.
- Find the branch that returns StreamSinkFileWriter.
- Summarize the exact conditions and non-cases.

Answer:
- IndexFileWriter uses StreamSinkFileWriter when the write path is the stream-sink mode (i.e., using BetaRowsetWriterV2). In this path:
  - SegmentCreator asks RowsetWriter::create_index_file_writer(...).
  - That calls RowsetWriter::create_file_writer(..., FileType::INVERTED_INDEX_FILE).
  - In BetaRowsetWriterV2::create_file_writer, it always returns a StreamSinkFileWriter (initialized with load_id/partition/index/tablet/segment and file_type).
  - IndexFileWriter then receives this StreamSinkFileWriter as its _idx_v2_writer.

- This only applies for inverted index storage format V2+ (RowsetWriter only creates the idx file writer for V2; V1 doesn’t use _idx_v2_writer).

- Not used when writing locally via BaseBetaRowsetWriter (normal local files) or compaction/schema-change paths; those use LocalFileSystem/S3 writers.

- Your log line “IndexFileWriter::add_into_searcher_cache … not found file” is expected in this stream-sink case: data is streamed to replicas, so there’s no local idx file; IndexFileWriter explicitly treats “no file” as normal if _idx_v2_writer is a StreamSinkFileWriter.

zhiqiang-hhhh: 在cloud模式下会使用 S3FileWriter，这个逻辑在哪里实现的

GitHub Copilot: I’ll search the codebase to find S3FileWriter and trace where it’s instantiated and selected in “cloud” mode. 

zhiqiang-hhhh: @agent Try Again

GitHub Copilot: I’ll trace how “cloud mode” picks S3FileWriter and point you to the exact code paths.

Checklist
- Show where cloud mode switches to a remote FS (S3) for rowset writing.
- Show the call site that constructs S3FileWriter.
- Mention the alternate factory path when FileFactory is used.

Where the logic lives
- Cloud mode switches the writer to remote FS (S3)
  - cloud/cloud_rowset_writer.cpp: CloudRowsetWriter::init sets remote storage on the rowset and wires BaseBetaRowsetWriter machinery. It keeps RowsetWriterContext::storage_resource populated (cloud), so we don’t write to local FS.
  - olap/rowset/rowset_writer_context.h: RowsetWriterContext::fs() returns storage_resource->fs when is_local_rowset() == false (cloud). segment_path() also uses the remote path.
- The file writer creation funnels to S3FileWriter
  - beta_rowset_writer.cpp (BaseBetaRowsetWriter::_create_file_writer): calls _context.fs()->create_file(path, &file_writer, &opts). In cloud, fs() is an S3FileSystem.
  - io/fs/s3_file_system.cpp: S3FileSystem::create_file_impl constructs the concrete writer: *writer = std::make_unique<S3FileWriter>(...). This is the exact place S3FileWriter is chosen in cloud mode.
- Factory path (when creating writers via a generic factory)
  - io/file_factory.cpp: FileFactory::create_file_writer, case TFileType::FILE_S3, also returns std::make_unique<io::S3FileWriter>(...). Some flows use this factory directly.

Notes
- For inverted index V2 files, BaseBetaRowsetWriter::create_index_file_writer ultimately calls the same _create_file_writer path, so index files also use S3FileWriter in cloud.
- s3_obj_storage_client.cpp is used by S3FileWriter internally to talk to the object store; it’s not where the selection happens.

zhiqiang-hhhh: doris 里面的 memtbl 前移指的是什么

GitHub Copilot: Summarized conversation historyI'll scan key files to ground the “memtbl 前移” explanation in code: where the flag is set/checked and where the V2 sink and rowset writers are used. Then I’ll summarize findings and answer. I’ll ground “memtbl 前移” in this repo and point you to the exact flags, code paths, and effects.

## What “memtbl 前移” means

- Concept: Move memtable building/flushing from tablet replicas to the pipeline sink node (the BE running the sink operator), then stream the finished segment/index bytes to target replicas.
- Goal: Reduce network fan-out and per-replica build overhead; centralize write on sink then fan-out bytes via streams.

## When it’s used

- Sink selection: In pipeline_fragment_context.cpp, sink is switched to `OlapTableSinkV2OperatorX` only if:
  - `state->query_options().enable_memtable_on_sink_node` is true,
  - no inverted index V1 or partial update on the table,
  - and NOT in cloud mode.
  - See: `_create_data_sink(...)` case `TDataSinkType::OLAP_TABLE_SINK` (guard with `!config::is_cloud_mode()`).

- Writer path: With V2 sink enabled, BE uses `DeltaWriterV2` + `BetaRowsetWriterV2`, which create `StreamSinkFileWriter` for segments and inverted indexes.
  - beta_rowset_writer_v2.cpp: `create_file_writer(...)` builds `StreamSinkFileWriter`.
  - `be/src/io/fs/stream_sink_file_writer.*`: streams data to peers; no local files.

- Index v2 support: Enabled by context flag:
  - rowset_writer_context.h: `bool memtable_on_sink_support_index_v2`.
  - Set true in delta_writer_v2.cpp so inverted index V2 bytes are also streamed.

## How to enable/propagate

- Session variable (SQL): `set enable_memtable_on_sink_node = true`.
  - Tests/examples in regression suite show usage.
- Stream load HTTP header: `memtable_on_sink_node: true`
  - Parsed in stream_load.cpp and http_common.h.
- Routine load: Thrift field propagated to BE in routine_load_task_executor.cpp.

## What you’ll observe

- No local segment/index files at the sink; bytes go over streams.
- `IndexFileWriter` detects streaming and treats “file not found” as normal:
  - index_file_writer.cpp checks `dynamic_cast<StreamSinkFileWriter*>` and skips local lookup/logging accordingly.

## Limitations and guards

- Not in cloud mode: In cloud, Doris uses the FS path (e.g., S3) instead of memtable-on-sink V2; the code explicitly disables V2 sink when `config::is_cloud_mode()` is true.
- Not with inverted index V1 or partial update (guarded in the same sink selection branch).

## Benefits and trade-offs

- Pros: Less network fan-out, shared delta writers (see comments in `be/src/common/config.{h,cpp}`), higher throughput on wide replication.
- Cons: Higher memory pressure at the sink; requires index V2 compatibility; different failure/retry surface at stream layer.

If you want, I can trace a specific load/query to show whether it took the V2 sink path and where the streams were opened.
```