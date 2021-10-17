[TOC]


    适用读者：有 Clickhouse 的基本使用经历，了解 ReplicatedMergeTree 的使用方式。



## 写入

### initial 节点
initial 节点的写入过程总体来看需要完成两个任务，首先是 part 在本地的写入，其次是在 zk 中创建一条 log 告诉其他 replica 需要进行 fetch。
```c++
void ReplicatedMergeTreeBlockOutputStream::write(const Block & block)
{
    ...
    auto part_blocks = storage.writer.splitBlockIntoParts(block, max_parts_per_block, metadata_snapshot);

    for (auto & current_block : part_blocks)
    {
        Stopwatch watch;

        /// Write part to the filesystem under temporary name. Calculate a checksum.

        MergeTreeData::MutableDataPartPtr part = storage.writer.writeTempPart(current_block, metadata_snapshot, optimize_on_insert);

        /// If optimize_on_insert setting is true, current_block could become empty after merge
        /// and we didn't create part.
        if (!part)
            continue;

        String block_id;

        if (deduplicate)
        {
            /// We add the hash from the data and partition identifier to deduplication ID.
            /// That is, do not insert the same data to the same partition twice.
            block_id = part->getZeroLevelPartBlockID();

            LOG_DEBUG(log, "Wrote block with ID '{}', {} rows", block_id, current_block.block.rows());
        }
        else
        {
            LOG_DEBUG(log, "Wrote block with {} rows", current_block.block.rows());
        }

        try
        {
            commitPart(zookeeper, part, block_id);

            /// Set a special error code if the block is duplicate
            int error = (deduplicate && last_block_is_duplicate) ? ErrorCodes::INSERT_WAS_DEDUPLICATED : 0;
            PartLog::addNewPart(storage.getContext(), part, watch.elapsed(), ExecutionStatus(error));
        }
        catch (...)
        {
            PartLog::addNewPart(storage.getContext(), part, watch.elapsed(), ExecutionStatus::fromCurrentException(__PRETTY_FUNCTION__));
            throw;
        }
    }
}
```
这段函数不长，所以我把全部代码贴过来。在 commitPart 之前的部分很简单，这里不再做详细解释，参考 MergeTree 的写入过程。commitPart 函数比较复杂的部分在于各种异常处理，我们这里只列出正常处理过程伪代码。
```c++
void ReplicatedMergeTreeBlockOutputStream::commitPart(
    zkutil::ZooKeeperPtr & zookeeper, MergeTreeData::MutableDataPartPtr & part, const String & block_id)
{
    ...
    String block_id_path = storage.zookeeper_path + "/blocks/" + block_id;
    auto block_number_lock = storage.allocateBlockNumber(part->info.partition_id, zookeeper, block_id_path);
    ...
    
    block_number = block_number_lock->getNumber();

    StorageReplicatedMergeTree::LogEntry log_entry;
    log_entry.type = StorageReplicatedMergeTree::LogEntry::GET_PART;
    log_entry.create_time = time(nullptr);
    log_entry.source_replica = storage.replica_name;
    log_entry.new_part_name = part->name;
    log_entry.block_id = block_id;
    ops.emplace_back(zkutil::makeCreateRequest(
        storage.zookeeper_path + "/log/log-",
        log_entry.toString(),
        zkutil::CreateMode::PersistentSequential));

    /// Information about the part.
    storage.getCommitPartOps(ops, part, block_id_path);
    MergeTreeData::Transaction transaction(storage); /// If you can not add a part to ZK, we'll remove it back from the working set.
    bool renamed = false;
    renamed = storage.renameTempPartAndAdd(part, nullptr, &transaction);
    Coordination::Responses responses;
    Coordination::Error multi_code = zookeeper->tryMultiNoThrow(ops, responses); /// 1 RTT

    transaction.commit();
    storage.merge_selecting_task->schedule();
}
```
commitPart 过程分为三步：
1. 在 zk 中添加 part
2. 创建一条 GET_PART log
3. rename temp part

结合一个实际的例子来看看以上三个步骤完成的具体操作。
```bash
INSERT INTO TABLE insertDebug VALUES (100, 10000, 1),(101, 10001, 2),(102, 10002, 3),(103, 10003, 4),(104, 10004, 1)
```
block 切分之后得到的第一个 part 内容包含 `(100, 10000, 1),(104, 10004, 1)`，block_id 内容根据 part 内容计算这里为`1_11848747669222048964_7469084187050794676`，用于数据输入时候的去重。

当成功获得 block_number_lock 之后，在 zk 中创建一条 GET_PART log，内容如下：
```bash
[zk: localhost:2181(CONNECTED) 36] get /clickhouse/tables/01/insertDebug/log/log-0000000000
format version: 4
create_time: 2021-10-02 12:52:03
source replica: r1
block_id: 1_11848747669222048964_7469084187050794676
get
1_1_1_0
part_type: Compact
```
然后下一步将之前insert的临时part `tmp_insert_1_1_1_0` 重命名为 `1_1_1_0`，如果本次操作成功，将会把 part 信息真正注册到 zk 中如下：
```bash
[zk: localhost:2181(CONNECTED) 37] ls /clickhouse/tables/01/insertDebug/blocks
[1_11848747669222048964_7469084187050794676]
[zk: localhost:2181(CONNECTED) 39] get /clickhouse/tables/01/insertDebug/blocks/1_11848747669222048964_7469084187050794676
1_1_1_0
[zk: localhost:2181(CONNECTED) 42] ls /clickhouse/tables/01/insertDebug/block_numbers
[1]
```

**多写情景如何处理 block number 同步**
考虑多写情景，如果客户端在两个 replica 上同时写某个 partition，我们需要保证 block number 在两个 replica 上的顺序性。该顺序性是通过 zk 的[sequential node](https://zookeeper.apache.org/doc/r3.4.10/zookeeperProgrammers.html#Sequence+Nodes+--+Unique+Naming)实现的。zk 中创建 znode 共有[四种模式](https://zookeeper.apache.org/doc/r3.2.2/api/org/apache/zookeeper/CreateMode.html)，我们这里需要关注的是`EPHEMERAL_SEQUENTIAL`：The znode will be deleted upon the client's disconnect, and its name will be appended with a monotonically increasing number.

ReplicatedMergeTree 写入流程中，commitPart 的时候会创建一个 EmphemeralLockInZookeeper 对象，这个对象对应zk的两个节点，一个是代表 part 正在被写入的 /block_numbers/partitiond_id/block_xxxxx ，另一个是 /temp/abandonable_lock-xxxxx，前者引用后者。

以我们写入的第一个 part 为例，partition_id = 1，由于是第一次在当前 partition 插入 znode，那么这个 block 就可以获得 number 0000000000，同样 temp 下也是第一次插入 znode，那么其唯一的 id 也是 0000000000

<img alt="picture 1" src="../../images/fd7d7846e65030a93516b15c70dec358468f4af0a1467a09ab521d7a266bdedc.png" />  

当我们处理完第一个 part，连接断开，partition 下的 `block_` 和 temp 下的 `abandonable_lock` 都会被自动删除，第二个 part 的 partition id 为 2，重复上述过程，我们可以得到

<img alt="picture 2" src="../../images/19579eca222a612ebd53bccf43a501762105ee34095efaf67a30f276e5616c2d.png" />  
 
上述过程是在函数 EphemeralLockInZooKeeper 中完成的，返回的 lock 对象将会包含 `block_` 后跟随的 sequential number，然后该 number 将会被赋值给 part
```c++
lock = EphemeralLockInZooKeeper(
            partition_path + "/block-", zookeeper_path + "/temp", *zookeeper, &deduplication_check_ops);

...
block_number = block_number_lock->getNumber();
part->info.min_block = block_number;
part->info.max_block = block_number;
...
```
实际上只需要 block_ 节点就足够保证 block number 的顺序性了，不是很明白这里 abandonable_lock_ 的作用。（猜测是为了能够区分不同 partition 下正在被写入的 part）





### follower 节点
目前我们完成了在 initail 节点的 part 写入，现在我们需要完成将 initial 节点写入的 part 同步到 follower 节点的工作。

#### 线程模型

首先明确几个概念：
- task：代表传递给 thread pool 的一个任务。创建 task 可以理解为是交给线程池一个去执行的任务，但是绝对不等价于创建一个执行线程。task 将 thread pool 的接口封装后对外抽象出自己的接口，比如`schedule()`接口可以将本 task 直接前置到 thread pool 的队列头，如果有空闲线程本 task 将会被立刻执行。
- thread：本质就是在对象构造时创建一个 task。区别在于 thread 对象没有对外提供主动调度自己执行时机的接口。也就是说，所有交给 thread 执行的函数何时能够执行都是随缘的。
- executor：将 task 与一个逻辑 thread pool 结合在一起抽象出的对使用者来说功能更加丰富的对象。逻辑 thread pool 是指并不是真的创建新的线程，更加丰富的功能是指提供了可以控制有多少线程去执行 task。

这几个概念比较容易理解，如果不理解也没关系，有个大致概念，后面结合例子会看懂的。我们先来整体看看 ReplicatedMergeTree 的构造函数：
```c++
StorageReplicatedMergeTree::StorageReplicatedMergeTree(ContextPtr context_, ...) : MergeTreeData(context_, ...)
    ...
    , background_executor(*this, getContext())
    , background_moves_executor(*this, getContext())
    , cleanup_thread(*this)
    , part_check_thread(*this)
    , restarting_thread(*this)
    , replicated_fetches_pool_size(getContext()->getSettingsRef().background_fetches_pool_size)
    ...
{
    queue_updating_task = getContext()->getSchedulePool().createTask(
        getStorageID().getFullTableName() + " (StorageReplicatedMergeTree::queueUpdatingTask)", [this]{ queueUpdatingTask(); });

    mutations_updating_task = getContext()->getSchedulePool().createTask(
        getStorageID().getFullTableName() + " (StorageReplicatedMergeTree::mutationsUpdatingTask)", [this]{ mutationsUpdatingTask(); });

    merge_selecting_task = getContext()->getSchedulePool().createTask(
        getStorageID().getFullTableName() + " (StorageReplicatedMergeTree::mergeSelectingTask)", [this] { mergeSelectingTask(); });

    /// Will be activated if we win leader election.
    merge_selecting_task->deactivate();

    mutations_finalizing_task = getContext()->getSchedulePool().createTask(
        getStorageID().getFullTableName() + " (StorageReplicatedMergeTree::mutationsFinalizingTask)", [this] { mutationsFinalizingTask(); });
    
    ...
}
```
构造函数内部，其中 cleanup_thread、part_check_thread、restarting_thread、queue_updating_task、mutations_updating_task、merge_selecting_task、mutations_finalizing_task 都是往**全局的被所有表共用的** Background Schedule Pool 中塞进一个 task，该线程池大小由参数`background_schedule_pool_size`控制，最新的clickhouse master里默认值为 128 [PR#25072](https://github.com/ClickHouse/ClickHouse/pull/25072)

初始化列表里有两个 executor，其中比较关键的是 `BackgroundJobsExecutor`。`BackgroundJobsExecutor`对象构造时创建了两个**逻辑**线程池。一个线程池负责执行 MERGE_MUTATE 任务，另一个负责执行 FETCH 任务。前者大小由参数`background_pool_size`决定，后者由参数`background_fetches_pool_size`决定。

初始化列表里还创建了三个 thread，它们没有提供主动调度自己执行的接口，通常执行相对不太“重要”的任务。

#### part 同步

前面我们提到，initial 节点会在 zk 中创建 log，告诉 follower 节点需要进行 part fetch。follower 节点的 queue_updating_task 的任务是从 zk 拉取一个批次的 log 到本地，并且在 replica/queue 中添加一条 queue-xxxx ，表示当前 log 正在/已经被执行，最后触发 background_executor。

**queue_updating**
```c++
int32_t ReplicatedMergeTreeQueue::pullLogsToQueue(zkutil::ZooKeeperPtr zookeeper, Coordination::WatchCallback watch_callback)
{
    ...
    String index_str = zookeeper->get(replica_path + "/log_pointer");
    Strings log_entries = zookeeper->getChildrenWatch(zookeeper_path + "/log", nullptr, watch_callback);
    ...
    log_entries.erase(所有小于index的log);
    ...
    /// We update mutations after we have loaded the list of log entries, but before we insert them
    /// in the queue.
    /// With this we ensure that if you read the log state L1 and then the state of mutations M1,
    /// then L1 "happened-before" M1.
    updateMutations(zookeeper);
    
    std::sort(log_entries.begin(), log_entries.end());
    for (size_t entry_idx = 0, num_entries = log_entries.size(); entry_idx < num_entries;) {
        /// 一次取 current_multi_batch_size 条 log 去处理
        auto begin = log_entries.begin() + entry_idx;
        auto end = entry_idx + current_multi_batch_size >= log_entries.size()
                ? log_entries.end()
                : (begin + current_multi_batch_size);
        auto last = end - 1;

        zkutil::AsyncResponses<Coordination::GetResponse> futures;
        futures.reserve(end - begin);

        for (auto it = begin; it != end; ++it)
            futures.emplace_back(*it, zookeeper->asyncGet(zookeeper_path + "/log/" + *it));

        for (auto & future : futures)
        {
            ...
            /// 在本 replica 的 queue 下创建一条 queue_xxx 纪录 log 的执行状态
            ops.emplace_back(zkutil::makeCreateRequest(
                    replica_path + "/queue/queue-", res.data, zkutil::CreateMode::PersistentSequential));
            ...
        }
        /// 更新 log_pointer 指向下一条未处理的 log
        ops.emplace_back(zkutil::makeSetRequest(
                replica_path + "/log_pointer", toString(last_entry_index + 1), -1));

        /// Now we have successfully updated the queue in ZooKeeper. Update it in RAM.

        try
        {
            ...
            insertUnlocked(...);
        }

    storage.background_executor.triggerTask();
    }
    return stat.version;
}
```

**DataProcessing**
一切顺利的话，前面一步 queue 中添加的 log entry 就该被 getDataProcessingJob 消费掉。
```c++
std::optional<JobAndPool> StorageReplicatedMergeTree::getDataProcessingJob()
{
    /// This object will mark the element of the queue as running.
    ReplicatedMergeTreeQueue::SelectedEntryPtr selected_entry = selectQueueEntry();
    ...
    processQueueEntry(selected_entry);
    ...
}
```
selectQueueEntry 函数决定了某个 log 是否应该被执行，其详细逻辑在后面单独介绍。**一旦某个 log 被选中可以执行，则其在 system.replication_queue 中对应项的 num_tries 会加一**

**executeLogEntry**

对于写入过程产生的 GET_PART log，如果 part_name 在当前 replica 存在（包括 Committed/PreCommitted 状态）且在 zk 的 replica_path/parts/part_name 下存在，则不会进行 fetch。否则我们将会执行 fetch

**executeFetch**
```c++
bool StorageReplicatedMergeTree::executeFetch(LogEntry & entry)
{
    String replica = findReplicaHavingCoveringPart(entry, true);
    ...
    String part_name = entry.actual_new_part_name.empty() ? entry.new_part_name : entry.actual_new_part_name;
    fetchPart(part_name, metadata_snapshot, zookeeper_path + "/replicas/" + replica, false, entry.quorum));
    ...
    return true;
}
```
fetchPart 的具体执行过程就不写了。当该函数返回 true 时，ReplicatedMergeTreeQueue 会将本次 log 从 replica/queue 下删除



## 其他任务
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



## 任务管控

前面通几个典型任务的流程分析，对 ReplicationMergeTree 的任务处理有了整体印象，大致分为三个阶段：
1. log 创建：由 initial 节点在 zk 的 log 下创建一条 log-xxxxx 
2. 任务队列更新：folower 节点监听 log，将新的 log 拉取到本地，更新 zk 中 replica/queue 的状态，触发 background_executor
3. background_executor 消费 queue

本小节介绍其他 thread/task

### ReplicatedMergeTreeRestartingThread
该对象构造时，在**全局的SchedulePool**中创建一个新的task。这个 task 有两个主要任务：
1. 进程启动/表刚创建时，对表进行初始化。包括启动必要线程、进行 leader 选举
2. Zookeeper Session Expired，典型如 XID Overflow 后，重建新的 session

对于初始化过程，执行的关键函数是 `tryStartup()`，tryStartUp 不仅需要处理初始化，还得处理重启以及过了一段时间后重新加入节点的情况，而后者其实是比较复杂的。
```c++
bool ReplicatedMergeTreeRestartingThread::tryStartup()
{
    ...
    /// 在 zk 记录自己的信息，如 ip:port 等
    activateReplica();

    /// 重启过程
    storage.cloneReplicaIfNeeded(zookeeper);

    storage.queue.load(zookeeper);

    /// pullLogsToQueue() after we mark replica 'is_active' (and after we repair if it was lost);
    /// because cleanup_thread doesn't delete log_pointer of active replicas.
    storage.queue.pullLogsToQueue(zookeeper);
    storage.queue.removeCurrentPartsFromMutations();
    storage.last_queue_update_finish_time.store(time(nullptr));
    ...
    /// 选主
    if (storage_settings->replicated_can_become_leader)
        storage.enterLeaderElection(); 

    storage.partial_shutdown_called = false;
    storage.partial_shutdown_event.reset();

    storage.queue_updating_task->activateAndSchedule();
    storage.mutations_updating_task->activateAndSchedule();
    storage.mutations_finalizing_task->activateAndSchedule();
    storage.cleanup_thread.start();
    storage.part_check_thread.start();

    return true;
}
```
值得注意的一点是，`enterLeaderElection`函数中如果自己成功充当主节点，那么则会启动`merge_selecting_task`，负责统一 merge 与 mutation 操作。
###  

###
`selectQueueEntry()`函数决定某条 log 是否可以被执行，如果 log 被该函数认为不应该执行（推迟执行），则其被推迟原因会被纪录在其内存状态中，我们在 system.replication_queue 查询到的某个 log 的 postpone_reason 就是在这里填写的。