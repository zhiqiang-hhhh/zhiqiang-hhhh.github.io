[TOC]
## 任务管控
后台任务创建
### StorageReplicatedMergeTree::getDataProcessingJob()
```c++
std::optional<JobAndPool> StorageReplicatedMergeTree::getDataProcessingJob()
{
    /// If replication queue is stopped exit immediately as we successfully executed the task
    if (queue.actions_blocker.isCancelled())
        return {};

    /// This object will mark the element of the queue as running.
    ReplicatedMergeTreeQueue::SelectedEntryPtr selected_entry = selectQueueEntry();

    if (!selected_entry)
        return {};

    PoolType pool_type;

    /// Depending on entry type execute in fetches (small) pool or big merge_mutate pool
    if (selected_entry->log_entry->type == LogEntry::GET_PART)
        pool_type = PoolType::FETCH;
    else
        pool_type = PoolType::MERGE_MUTATE;

    return JobAndPool{[this, selected_entry] () mutable
    {
        return processQueueEntry(selected_entry);
    }, pool_type};
}
```

选择一条 log entry 用于执行
```c++
/// StorageReplicatedMergeTree.cpp
ReplicatedMergeTreeQueue::SelectedEntryPtr StorageReplicatedMergeTree::selectQueueEntry()
{
    /// This object will mark the element of the queue as running.
    ReplicatedMergeTreeQueue::SelectedEntryPtr selected;

    try
    {
        selected = queue.selectEntryToProcess(merger_mutator, *this);
    }
    catch (...)
    {
        tryLogCurrentException(log, __PRETTY_FUNCTION__);
    }

    return selected;
}
```
处理log entry的真正函数：processQueueEntry，最终执行到 executeLogEntry(LogEntry & entry)，该函数的完成代码如下，后面分析各种具体任务时会逐段分析。
```c++
/// StorageReplicatedMergeTree.cpp
bool StorageReplicatedMergeTree::executeLogEntry(LogEntry & entry)
{
    if (entry.type == LogEntry::DROP_RANGE)
    {
        executeDropRange(entry);
        return true;
    }

    if (entry.type == LogEntry::REPLACE_RANGE)
    {
        executeReplaceRange(entry);
        return true;
    }

    const bool is_get_or_attach = entry.type == LogEntry::GET_PART || entry.type == LogEntry::ATTACH_PART;

    if (is_get_or_attach || entry.type == LogEntry::MERGE_PARTS || entry.type == LogEntry::MUTATE_PART)
    {
        /// If we already have this part or a part covering it, we do not need to do anything.
        /// The part may be still in the PreCommitted -> Committed transition so we first search
        /// among PreCommitted parts to definitely find the desired part if it exists.
        DataPartPtr existing_part = getPartIfExists(entry.new_part_name, {MergeTreeDataPartState::PreCommitted});

        if (!existing_part)
            existing_part = getActiveContainingPart(entry.new_part_name);

        /// Even if the part is local, it (in exceptional cases) may not be in ZooKeeper. Let's check that it is there.
        if (existing_part && getZooKeeper()->exists(replica_path + "/parts/" + existing_part->name))
        {
            if (!is_get_or_attach || entry.source_replica != replica_name)
                LOG_DEBUG(log, "Skipping action for part {} because part {} already exists.",
                    entry.new_part_name, existing_part->name);

            return true;
        }
    }

    if (entry.type == LogEntry::ATTACH_PART)
    {
        if (MutableDataPartPtr part = attachPartHelperFoundValidPart(entry); part)
        {
            LOG_TRACE(log, "Found valid part to attach from local data, preparing the transaction");

            Transaction transaction(*this);

            renameTempPartAndReplace(part, nullptr, &transaction);
            checkPartChecksumsAndCommit(transaction, part);

            writePartLog(PartLogElement::Type::NEW_PART, {}, 0 /** log entry is fake so we don't measure the time */,
                part->name, part, {} /** log entry is fake so there are no initial parts */, nullptr);

            return true;
        }

        LOG_TRACE(log, "Didn't find part with the correct checksums, will fetch it from other replica");
    }

    if (is_get_or_attach && entry.source_replica == replica_name)
        LOG_WARNING(log, "Part {} from own log doesn't exist.", entry.new_part_name);

    /// Perhaps we don't need this part, because during write with quorum, the quorum has failed
    /// (see below about `/quorum/failed_parts`).
    if (entry.quorum && getZooKeeper()->exists(zookeeper_path + "/quorum/failed_parts/" + entry.new_part_name))
    {
        LOG_DEBUG(log, "Skipping action for part {} because quorum for that part was failed.", entry.new_part_name);
        return true;    /// NOTE Deletion from `virtual_parts` is not done, but it is only necessary for merge.
    }

    bool do_fetch = false;

    switch (entry.type)
    {
        case LogEntry::ATTACH_PART:
            /// We surely don't have this part locally as we've checked it before, so download it.
            [[fallthrough]];
        case LogEntry::GET_PART:
            do_fetch = true;
            break;
        case LogEntry::MERGE_PARTS:
            /// Sometimes it's better to fetch the merged part instead of merging,
            /// e.g when we don't have all the source parts.
            do_fetch = !tryExecuteMerge(entry);
            break;
        case LogEntry::MUTATE_PART:
            /// Sometimes it's better to fetch mutated part instead of merging.
            do_fetch = !tryExecutePartMutation(entry);
            break;
        case LogEntry::ALTER_METADATA:
            return executeMetadataAlter(entry);
        default:
            throw Exception(ErrorCodes::LOGICAL_ERROR, "Unexpected log entry type: {}", static_cast<int>(entry.type));
    }

    if (do_fetch)
        return executeFetch(entry);

    return true;
}
```


`relative_data_path_` : `store/177/1774b2f7-26ba-4f22-9d69-cb5eaa7ddfd7/`

## 具体任务
### DROP PARTS
以如下操作为例
```sql
create table dropPartition (key Int64, value Int64, devider Int64) engine=ReplicatedMergeTree('/clickhouse/tables/01/dropPartition', '{replica}') order by key partition by devider

insert into table dropPartition values (100,100,1),(101,101,2),(99,99,3),(88,88,1)
```

```sql
SELECT
    partition,
    name AS part,
    partition_id,
    rows,
    path
FROM system.parts
WHERE table = 'dropPartition'

Query id: 57b9daff-7f93-4db4-b5b5-d78ef8acdd29

┌─partition─┬─part────┬─partition_id─┬─rows─┬─path─────────────────────────────────────────────────────────────────────────┐
│ 1         │ 1_0_0_0 │ 1            │    2 │ /tmp/clickhouse-data/store/f4e/f4e2a330-2098-434b-babf-4982c9365d53/1_0_0_0/ │
│ 2         │ 2_0_0_0 │ 2            │    1 │ /tmp/clickhouse-data/store/f4e/f4e2a330-2098-434b-babf-4982c9365d53/2_0_0_0/ │
│ 3         │ 3_0_0_0 │ 3            │    1 │ /tmp/clickhouse-data/store/f4e/f4e2a330-2098-434b-babf-4982c9365d53/3_0_0_0/ │
└───────────┴─────────┴──────────────┴──────┴──────────────────────────────────────────────────────────────────────────────┘
```

DROP PARTITION 的整体执行分为两步：
1. 在 zk 创建任务
2. 拉取 zk 中的任务进行处理

第一阶段由函数 dropPartition 完成：
```c++
// StorageReplicatedMergeTree.cpp
void StorageReplicatedMergeTree::dropPartition(...) {
    ...
    String partition_id = getPartitionIDFromQuery(partition, query_context);
    /// Step1 在 zk 中创建 log-entry
    did_drop = dropAllPartsInPartition(...);
    if (did_drop) 
    {
        /// If necessary, wait until the operation is performed on itself or on all replicas.
        ...
    }
    ...
}
```
dropAllPartsInPartition 在 zookeeper 中创建如下内容节点
```txt
[zk: localhost:2181(CONNECTED) 15] get /clickhouse/tables/01/dropPartition/log/log-0000000003
format version: 4
create_time: 2021-09-15 14:42:15
source replica: clickhouse01
block_id:
drop
3_0_1_999999999
```
创建完成后 dropPartition 函数根据 users.xml 中的 `<replication_alter_partitions_sync>` 参数决定是否等待本次 drop 被所有节点完全执行。

第二阶段函数 executeDropRange 处理前一个阶段在 zk 中创建的 log entry。删除过程也是分为两个阶段执行：
1. 修改需要被删除的part的 remove_time 为现在
2. 调度 cleanup_thread 主动删除 old parts，并且删除 zk 中 parts 信息

```c++
void StorageReplicatedMergeTree::executeDropRange(const LogEntry & entry)
{
    ...
    queue.removePartProducingOpsInRange(getZooKeeper(), drop_range_info, entry);
    ...
    /// 这一步遍历已有的part，设置需要被删除 part 的 remove_time 为现在
    parts_to_remove = removePartsInRangeFromWorkingSet(drop_range_info, true, true, data_parts_lock);

    if (entry.detach)
    {
        /// If DETACH clone parts to detached/ directory
        for (const auto & part : parts_to_remove)
        {
            part->makeCloneInDetached("", metadata_snapshot);
        }
    }

    /// Forcibly remove parts from ZooKeeper
    tryRemovePartsFromZooKeeperWithRetries(parts_to_remove);

    if (entry.detach)
        LOG_DEBUG(log, "Detached {} parts inside {}.", parts_to_remove.size(), entry.new_part_name);
    else
        LOG_DEBUG(log, "Removed {} parts inside {}.", parts_to_remove.size(), entry.new_part_name);

    /// We want to remove dropped parts from disk as soon as possible
    /// To be removed a partition should have zero refcount, therefore call the cleanup thread at exit
    parts_to_remove.clear();
    /// clean up thread 真正将数据从磁盘删除
    cleanup_thread.wakeup();
```
cleanup_thread 是后台线程，周期执行
```c++
void ReplicatedMergeTreeCleanupThread::iterate() {
    storage.clearOldPartsAndRemoveFromZK();
    ...
}
```

```c++
void StorageReplicatedMergeTree::clearOldPartsAndRemoveFromZK() {
    ...
    /// 判断某个 part 是否为 old part 的依据是其 remove_time
    DataPartsVector parts = grabOldParts();
    ...
    
    /// Remove parts from filesystem and finally from data_parts
    if (!parts_to_remove_from_filesystem.empty())
    {
        removePartsFromFilesystem(parts_to_remove_from_filesystem);
        removePartsFinally(parts_to_remove_from_filesystem);

        LOG_DEBUG(log, "Removed {} old parts", parts_to_remove_from_filesystem.size());
    }
    ...

    removePartsFromZooKeeper(zookeeper, part_names_to_delete_completely, &part_names_to_retry_deletion);
}
```
### DELETE WHERE
`ALTER TABLE ... DELETE WEHER ...`