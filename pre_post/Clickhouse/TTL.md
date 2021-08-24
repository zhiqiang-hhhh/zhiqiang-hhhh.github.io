[TOC]

## 数据写入过程

```sql
insert into table h2 values ('127.0.0.1', '2021-08-10 00:01:01', 1213),('127.0.0.1', '2021-08-01 00:01:01', 1213),('127.0.0.1', '2021-08-02 00:01:01', 1213)
```
`TCPHandler`中的主要部分：
```c++
void TCPHandler::runImpl()
{
    ...
    state.io = executeQuery(state.query, query_context, false, state.stage, may_have_embedded_data);
    ...
    if (state.io.out)
    {
        state.need_receive_data_for_insert = true;
        processInsertQuery(connection_settings);
    }
}
```
```c++
BlockIO executeQuery(
    const String & query,
    ContextPtr context,
    bool internal,
    QueryProcessingStage::Enum stage,
    bool may_have_embedded_data)
{
    ASTPtr ast;
    BlockIO streams;
    std::tie(ast, streams) = executeQueryImpl(query.data(), query.data() + query.size(), context,
        internal, stage, !may_have_embedded_data, nullptr);

    ...

    return streams;
}

static std::tuple<ASTPtr, BlockIO> executeQueryImpl(
    const char * begin, // begin: "insert into table h2 values "
    ...
    )
{
    ...
    auto interpreter = InterpreterFactory::get(ast, context, SelectQueryOptions(stage).setInternal(internal));
    ...
    OpenTelemetrySpanHolder span("IInterpreter::execute()");
    res = interpreter->execute();   // BlockIO InterpreterInsertQuery::execute()

    QueryPipeline & pipeline = res.pipeline;
    bool use_processors = pipeline.initialized();
    ...
    return std::make_tuple(ast, std::move(res));
}
```
根据 ast 创建对应的 Interperter，这里返回的是 `InterpreterInsertQuery`。
`InterpreterInsertQuery::execute()`根据 Table 类型创建对应的`BlockOutputStream`。
不过这里的处理没有看懂，只知道最后用到的是`PushingToViewsBlockOutputStream`。
```c++
// InterpreterInsertQuery.cpp
BlockIO InterpreterInsertQuery::execute()
{
    const Settings & settings = getContext()->getSettingsRef();
    ...
    StoragePtr table = getTable(query);
    ...
    BlockOutputStreams out_streams;
    if (!is_distributed_insert_select || query.watch) // Y
    {
        size_t out_streams_size = 1;
        if (query.select) // X
        {
            ...
        }
        else if (query.watch) // X
        {
            ...
        }

        for (size_t i = 0; i < out_streams_size; i++)
        {
            /// We create a pipeline of several streams, into which we will write data.
            BlockOutputStreamPtr out;
            if (table->noPushingToViews() && !no_destination) // X
                ...
            else
                out = std::make_shared<PushingToViewsBlockOutputStream>(table, metadata_snapshot, getContext(), query_ptr, no_destination);
            
            if (const auto & constraints = metadata_snapshot->getConstraints(); !constraints.empty()) // X

            if (!(settings.insert_distributed_sync && table->isRemote()) && !no_squash && !query.watch)
            {
                bool table_prefers_large_blocks = table->prefersLargeBlocks();

                out = std::make_shared<SquashingBlockOutputStream>(
                    out,
                    out->getHeader(),
                    table_prefers_large_blocks ? settings.min_insert_block_size_rows : settings.max_block_size,
                    table_prefers_large_blocks ? settings.min_insert_block_size_bytes : 0);
            }

            auto out_wrapper = std::make_shared<CountingBlockOutputStream>(out);
            out_wrapper->setProcessListElement(getContext()->getProcessListElement());
            out = std::move(out_wrapper);
            out_streams.emplace_back(std::move(out));

            ...
            res.out = std::move(out_streams.at(0));
        }
    }
}
```
前面创建了合适的`BlockOutputStream`，在`TCPHandler::processInsertQuery()`中调用了其write方法，实现写入过程。
```c++
void PushingToViewsBlockOutputStream::write(const Block & block)
{
    ...
    if(output)
        output->write(block);
}

void MergeTreeBlockOutputStream::write(const Block & block)
{
    // part_blocks.size() == 3
    auto part_blocks = storage.writer.splitBlockIntoParts(block, max_parts_per_block, metadata_snapshot);
    for (auto & current_block : part_blocks)
    {
        Stopwatch watch;

        MergeTreeData::MutableDataPartPtr part = storage.writer.writeTempPart(current_block, metadata_snapshot, optimize_on_insert);

        /// If optimize_on_insert setting is true, current_block could become empty after merge
        /// and we didn't create part.
        if (!part)
            continue;

        /// Part can be deduplicated, so increment counters and add to part log only if it's really added
        if (storage.renameTempPartAndAdd(part, &storage.increment, nullptr, storage.getDeduplicationLog()))
        {
            PartLog::addNewPart(storage.getContext(), part, watch.elapsed());

            /// Initiate async merge - it will be done if it's good time for merge and if there are space in 'background_pool'.
            storage.background_executor.triggerTask();
        }
    }
}

MergeTreeData::MutableDataPartPtr MergeTreeDataWriter::writeTempPart(BlockWithPartition & block_with_partition, const StorageMetadataPtr & metadata_snapshot, bool optimize_on_insert)
{
    Block & block = block_with_partition.block;

    static const String TMP_PREFIX = "tmp_insert_";

    /// This will generate unique name in scope of current server process.
    Int64 temp_index = data.insert_increment.get();

    IMergeTreeDataPart::MinMaxIndex minmax_idx;
    minmax_idx.update(block, data.getMinMaxColumnsNames(metadata_snapshot->getPartitionKey()));

    MergeTreePartition partition(std::move(block_with_partition.partition));

    MergeTreePartInfo new_part_info(partition.getID(metadata_snapshot->getPartitionKey().sample_block), temp_index, temp_index, 0);
    String part_name;
    if (data.format_version < MERGE_TREE_DATA_MIN_FORMAT_VERSION_WITH_CUSTOM_PARTITIONING)
    {
        DayNum min_date(minmax_idx.hyperrectangle[data.minmax_idx_date_column_pos].left.get<UInt64>());
        DayNum max_date(minmax_idx.hyperrectangle[data.minmax_idx_date_column_pos].right.get<UInt64>());

        const auto & date_lut = DateLUT::instance();

        auto min_month = date_lut.toNumYYYYMM(min_date);
        auto max_month = date_lut.toNumYYYYMM(max_date);

        if (min_month != max_month)
            throw Exception("Logical error: part spans more than one month.", ErrorCodes::LOGICAL_ERROR);

        part_name = new_part_info.getPartNameV0(min_date, max_date);
    }
    else
        part_name = new_part_info.getPartName(); // "a7782d68c6fcbf018303f5d04d188a89_1_1_0"

    Names partition_key_columns = metadata_snapshot->getPartitionKey().column_names;
    if (optimize_on_insert) // Y
        block = mergeBlock(block, sort_description, partition_key_columns, perm_ptr);

    /// Size of part would not be greater than block.bytes() + epsilon
    size_t expected_size = block.bytes();   // 30

    /// If optimize_on_insert is true, block may become empty after merge.
    /// There is no need to create empty part.
    if (expected_size == 0)
        return nullptr;

    DB::IMergeTreeDataPart::TTLInfos move_ttl_infos;
    const auto & move_ttl_entries = metadata_snapshot->getMoveTTLs(); 
    for (const auto & ttl_entry : move_ttl_entries) // size() == 0
        updateTTL(ttl_entry, move_ttl_infos, move_ttl_infos.moves_ttl[ttl_entry.result_column], block, false);

    ...

    auto new_data_part = data.createPart(
        part_name,
        data.choosePartType(expected_size, block.rows()),
        new_part_info,
        createVolumeFromReservation(reservation, volume),
        TMP_PREFIX + part_name);

    if (data.storage_settings.get()->assign_part_uuids)
        new_data_part->uuid = UUIDHelpers::generateV4();

    ...

    if (new_data_part->isStoredOnDisk()) // Y
    {
        /// The name could be non-unique in case of stale files from previous runs.
        // "store/edf/edf3732d-2e71-4d8a-bf1c-878b6d17d412/tmp_insert_a7782d68c6fcbf018303f5d04d188a89_1_1_0/"
        String full_path = new_data_part->getFullRelativePath(); 
        ...
        const auto disk = new_data_part->volume->getDisk();
        disk->createDirectories(full_path);

        if (data.getSettings()->fsync_part_directory)
            sync_guard = disk->getDirectorySyncGuard(full_path);
    }

    if (metadata_snapshot->hasRowsTTL()) // Y
        updateTTL(metadata_snapshot->getRowsTTL(), new_data_part->ttl_infos, new_data_part->ttl_infos.table_ttl, block, true);

    ...

    new_data_part->ttl_infos.update(move_ttl_infos);

    /// This effectively chooses minimal compression method:
    ///  either default lz4 or compression method with zero thresholds on absolute and relative part size.
    auto compression_codec = data.getContext()->chooseCompressionCodec(0, 0);
    ...
    MergedBlockOutputStream out(new_data_part, metadata_snapshot, columns, index_factory.getMany(metadata_snapshot->getSecondaryIndices()), compression_codec);
    bool sync_on_insert = data.getSettings()->fsync_after_insert;
    out.writePrefix();
    out.writeWithPermutation(block, perm_ptr);
    out.writeSuffixAndFinalizePart(new_data_part, sync_on_insert);
    ...
    new_data_part->getBytesOnDisk());

    return new_data_part;
}

```

```c++
void updateTTL(
    const TTLDescription & ttl_entry,
    IMergeTreeDataPart::TTLInfos & ttl_infos,
    DB::MergeTreeDataPartTTLInfo & ttl_info,
    const Block & block,
    bool update_part_min_max_ttls)
{
    auto ttl_column = ITTLAlgorithm::executeExpressionAndGetColumn(ttl_entry.expression, block, ttl_entry.result_column);

    ...
    else if (const ColumnUInt32 * column_date_time = typeid_cast<const ColumnUInt32 *>(ttl_column.get())) // Y
    {
        for (const auto & val : column_date_time->getData())
            ttl_info.update(val);
    }
    ...
    }
    ...

    if (update_part_min_max_ttls)
        ttl_infos.updatePartMinMaxTTL(ttl_info.min, ttl_info.max);
}


void MergeTreeDataPartTTLInfos::update(const MergeTreeDataPartTTLInfos & other_infos)
{
    ...

    table_ttl.update(other_infos.table_ttl);
    updatePartMinMaxTTL(table_ttl.min, table_ttl.max);
}
```

### TTLBlockInputStream
```c++
// MergeTreeDataMergerMutator.cpp
MergeTreeData::MutableDataPartPtr MergeTreeDataMergerMutator::mergePartsToTemporaryPart(
    const FutureMergedMutatedPart & future_part,
    const StorageMetadataPtr & metadata_snapshot,
    MergeList::Entry & merge_entry,
    TableLockHolder &,
    time_t time_of_merge, // time()
    ContextPtr context,
    const ReservationPtr & space_reservation,
    bool deduplicate,
    const Names & deduplicate_by_columns)
{   
    bool need_remove_expired_values = false;
    ...
    if (part_min_ttl && part_min_ttl <= time_of_merge) // Y
        need_remove_expired_values = true;
    ...
    if (need_remove_expired_value)
        merged_stream = std::make_shared<TTLBlockInputStream>(merged_stream, data, metadata_snapshot, new_data_part, time_of_merge, force_ttl);
    ...
    merged_stream->readPrefix();
    ...
    while (!is_calcelled() && (block = merged_stream->read()))
    {
        rows_written += block.rows();

        to.write(block);

        merge_entry->rows_written = merged_stream->getProfileInfo().rows;
        merge_entry->bytes_written_uncompressed = merged_stream->getProfileInfo().bytes;

        /// Reservation updates is not performed yet, during the merge it may lead to higher free space requirements
        if (space_reservation && sum_input_rows_upper_bound)
        {
            /// The same progress from merge_entry could be used for both algorithms (it should be more accurate)
            /// But now we are using inaccurate row-based estimation in Horizontal case for backward compatibility
            Float64 progress = (chosen_merge_algorithm == MergeAlgorithm::Horizontal)
                ? std::min(1., 1. * rows_written / sum_input_rows_upper_bound)
                : std::min(1., merge_entry->progress.load(std::memory_order_relaxed));

            space_reservation->update(static_cast<size_t>((1. - progress) * initial_reservation));
        }
    }

    merged_stream->readSuffix();
    ...
}    
```
```c++
Block TTLBlockInputStream::readImpl()
{
    if (all_data_dropped)
        return {};

    auto block = children.at(0)->read();
    for (const auto & algorithm : algorithms)
        algorithm->execute(block);

    if (!block)
        return block;

    return reorderColumns(std::move(block), header);
}
```

## Merge Parts

```bash
StorageMergeTree::optimize
- StorageMergeTree::merge
- - StorageMergeTree::selectPartsToMerge
- - StorageMergeTree::mergeSelectedParts
```

### 后台任务创建
`createTable`的最后，通过`StorageMergeTree::startup()`来创建后台的Merge任务。
```c++
bool InterpreterCreateQuery::doCreateTable(ASTCreateQuery & create,
                                           const InterpreterCreateQuery::TableProperties & properties)
{
    ...
    StoragePtr res;
    ...
    res->startup();
    return true;
}

void StorageMergeTree::startup()
{
    clearOldPartsFromFilesystem();
    clearOldWriteAheadLogs();
    clearEmptyParts();

    /// Temporary directories contain incomplete results of merges (after forced restart)
    ///  and don't allow to reinitialize them, so delete each of them immediately
    clearOldTemporaryDirectories(0);

    /// NOTE background task will also do the above cleanups periodically.
    time_after_previous_cleanup.restart();
    ...
    // BackgroundJobsExecutor
    background_executor.start();
    startBackgroundMovesIfNeeded();
    ...
}
```
这里的`BackgroundJobsExecutor background_executor`是在`StorageMergeTree`的构造函数里创建的：
```cpp
StorageMergeTree::StorageMergeTree(
    ...
    )
    : ..., background_executor(*this, getContext()) ... {}
``` 
`BackgroundJobsExecutor` 继承自 `IBackgroundJobExecutor`

```cpp
void IBackgroundJobExecutor::start()
{
    std::lock_guard lock(scheduling_task_mutex);
    if (!scheduling_task)
    {
        scheduling_task = getContext()->getSchedulePool().createTask(
            getBackgroundTaskName(), [this]{ jobExecutingTask(); });
    }

    scheduling_task->activateAndSchedule();
}
```
`start`执行的时候创建的这个函数对象执行的是`IBackgroundJobExecutor`的成员函数`jobExecutingTask`

```cpp
void IBackgroundJobExecutor::jobExecutingTask()
{
    auto job_and_pool = getBackgroundJob();
    ...
    bool job_succeed = job();
    ...
}


std::optional<JobAndPool> BackgroundJobsExecutor::getBackgroundJob()
{
    return data.getDataProcessingJob();
}
```
所以，当后台Merge任务执行是最终执行的是`StorageMergeTree::getDataProcessingJob()`

### Merge 任务执行
当前我们只关注merge的流程：
```cpp
std::optional<JobAndPool> StorageMergeTree::getDataProcessingJob()
{
    ...
    merge_entry = selectPartsToMerge(metadata_snapshot, false, {}, false, nullptr, share_lock);
    ...
    return mergeSelectedParts(metadata_snapshot, false, {}, *merge_entry, share_lock);
    ...
}
```
后台Merge Job执行分为两个阶段，第一个阶段是从所有Parts中选取可合并的Parts，第二个阶段是真正执行Merge。
```cpp
std::shared_ptr<StorageMergeTree::MergeMutateSelectedEntry> StorageMergeTree::selectPartsToMerge(
    const StorageMetadataPtr & metadata_snapshot, bool aggressive, const String & partition_id, bool final, String * out_disable_reason, TableLockHolder & /* table_lock_holder */, bool optimize_skip_merged_partitions, SelectPartsDecision * select_decision_out)
{
    ...
    // A. Lambda expression that decides if left datapart and right datapart could be
    // considerd for merging
    auto can_merge = [this, &lock] (const DataPartPtr & left, const DataPartPtr & right, String *) -> bool
    {
        /// This predicate is checked for the first part of each partition.
        /// (left = nullptr, right = "first part of partition")
        if (!left)
            return !currently_merging_mutating_parts.count(right);
        return !currently_merging_mutating_parts.count(left) && !currently_merging_mutating_parts.count(right)
            && getCurrentMutationVersion(left, lock) == getCurrentMutationVersion(right, lock);
    };

    SelectPartsDecision select_decision = SelectPartsDecision::CANNOT_SELECT;

    // B. Traverse all partitions to get data parts for merging
    if (partition_id.empty())
    {
        // C. Parts that are larger than max_source_parts_size is excluded from list
        UInt64 max_source_parts_size = merger_mutator.getMaxSourcePartsSizeForMerge();
        // D. Limit background task num for merge with ttl
        bool merge_with_ttl_allowed = getTotalMergesWithTTLInMergeList() < data_settings->max_number_of_merges_with_ttl_in_pool;

        ...
        
        select_decision = merger_mutator.selectPartsToMerge(
            future_part,
            aggressive,
            max_source_parts_size,
            can_merge,
            merge_with_ttl_allowed,
            out_disable_reason);
        ...
    }

}
```
- A `can_merge`是一个lambda表达式，用于判断当前part是否可以进行merge，判断条件也很简单，如果当前part不在`currently_mergeing_mutating_parts`中，那么它就可以被merge。可以被merge并不代表一定会被merge，后续还会通过`ITTLMergeSelector`进行过滤。
- B 后台 merge job 执行时，没有被指定 partition，所以这里会遍历所有parts
- C `max_source_parts_size`用于限制source parts大小，所有source parts加起来不能大于该值
- D 同时进行 ttl merge 的线程数不得超过`max_number_of_merges_with_ttl_in_pool`
  
```cpp
SelectPartsDecision MergeTreeDataMergerMutator::selectPartsToMerge(
    FutureMergedMutatedPart & future_part,
    bool aggressive,
    size_t max_total_size_to_merge,
    const AllowedMergingPredicate & can_merge_callback,
    bool merge_with_ttl_allowed,
    String * out_disable_reason)
{
    // A. 
    MergeTreeData::DataPartsVector data_parts = data.getDataPartsVector();
}
```