MergeTreeData 所处的位置：

```plantuml
abstract class IStorage

IStorage <-- MergeTreeData

abstract class MergeTreeData

MergeTreeData <-- StorageMergeTree
MergeTreeData <-- StorageReplicatedMergeTree
MergeTreeData <-- StorageShareDiskMergeTree
```

数据成员（Merge相关）：
```plantuml
abstract class MergeTreeData {
    - relative_data_path : String
    - data_parts_mutex : mutex
    - data_parts_indexes : DataPartsIndexes
    - background_operations_assignee : BackgroundJobsAssignee
    ...
}

MergeTreeData *-- BackgroundJobsAssignee

class BackgroundJobsAssignee {
    + BackgroundJobsAssignee(MergeTreeData&, Type, ContexPtr)
    - data : MergeTreeData&
    - holder : BackgroundSchedulePool::TaskHolder
    + start() : void
    + trigger() : void
    + postpone() : void
    + finish() : void
    - threadFunc() : void
}
```

MergeTreeData 的 contructor 中构造 BackgroundJobsAssignee 对象。

后台任务的启动流程：
```c++
BackgroundJobsAssignee(MergeTreeData & data_, Type type, ContextPtr global_context_)
    : ..., data(data_), ... {}

MergeTreeData::MergeTreeData(
    const StorageID & table_id_,
    ...)
    : ...
    , ...
    , background_operations_assignee(*this, BackgroundJobsAssignee::Type::DataProcessing, getContext())
    ...)
{
    ...
}

StorageMergeTree(
    const StorageID & table_id_,
    ...
    ) : MergeTreeData(
        table_id_,
        ...
    )
)

StorageMergeTree::startup()
{
    ...
    background_operations_assignee.start();
    ...
}

void BackgroundJobsAssignee::start()
{
    std::lock_guard lock(holder_mutex);
    if (!holder)
        holder = getContext()->getSchedulePool().createTask("BackgroundJobsAssignee:" + toString(type), [this]{ threadFunc(); });

    holder->activateAndSchedule();
}
```
也就是说，在StorageMergeTree 对象的 start 方法中，在 schedule pool 创建一个 task，该 task 执行的是`BackgroundJobsAssignee::threadFunc()` 方法，
```c++
void BackgroundJobsAssignee::threadFunc()
{
    try
    {
        bool succeed = false;
        switch (type)
        {
            case Type::DataProcessing:
                succeed = data.scheduleDataProcessingJob(*this);
                break;
            case Type::Moving:
                succeed = data.scheduleDataMovingJob(*this);
                break;
        }

        if (!succeed)
            postpone();
    }
    catch (...) /// Catch any exception to avoid thread termination.
    {
        tryLogCurrentException(__PRETTY_FUNCTION__);
        postpone();
    }
}
```
最终执行的是`StorageMergeTree::scheduleDataProcessingJob(BackgroundJobAssignee&)`。

```c++
bool StorageMergeTree::scheduleDataProcessingJob(BackgroundJobsAssignee & assignee)
{
    ...
    auto metadata_snapshot = getInMemoryMetadataPtr();
    std::shared_ptr<MergeMutateSelectedEntry> merge_entry, mutate_entry;

    auto share_lock = lockForShare(RWLockImpl::NO_QUERY, getSettings()->lock_acquire_timeout_for_background_operations);

    ...

    bool has_mutations = false;
    {
        std::unique_lock lock(currently_processing_in_background_mutex);
        if (merger_mutator.merges_blocker.isCancelled())
            return false;

        merge_entry = selectPartsToMerge(metadata_snapshot, false, {}, false, nullptr, share_lock, lock);
        if (!merge_entry)
            mutate_entry = selectPartsToMutate(metadata_snapshot, nullptr, share_lock, lock, were_some_mutations_skipped);

        has_mutations = !current_mutations_by_version.empty();
    }

    if ((!mutate_entry && has_mutations) || were_some_mutations_skipped)
    {
        /// Notify in case of errors or if some mutation was skipped (because it has no effect on the part).
        /// TODO @azat: we can also spot some selection errors when `mutate_entry` is true.
        std::lock_guard lock(mutation_wait_mutex);
        mutation_wait_event.notify_all();
    }

    if (merge_entry)
    {
        auto task = std::make_shared<MergePlainMergeTreeTask>(*this, metadata_snapshot, false, Names{}, merge_entry, share_lock, common_assignee_trigger);
        assignee.scheduleMergeMutateTask(task);
        return true;
    }
}
```

### ThreadPool, Thread, Executor, Task
```c++
class Context {
    ...
    mutable std::optional<BackgroundSchedulePool> schedule_pool;
    mutable std::optional<BackgroundSchedulePool> distributed_schedule_pool;
    ...
    MergeMutateBackgroundExecutorPtr merge_mutate_executor;
    OrdinaryBackgroundExecutorPtr moves_executor;
    OrdinaryBackgroundExecutorPtr fetch_executor;
    OrdinaryBackgroundExecutorPtr common_executor;
    ...    
}
```
ThreadPool 实现如下，包含一组真正的线程，轮番消费 priority_queue 中的 job。
```c++
ThreadPoolImpl
{
public:
    using Job = std::function<void()>;
    ...
private:
    mutable std::mutex mutex;
    std::condition_variable job_finished;
    std::condition_variable new_job_or_shutdown;

    size_t max_threads;
    size_t max_free_threads;
    size_t queue_size;

    size_t scheduled_jobs = 0;
    bool shutdown = false;

    struct JobWithPriority
    {
        Job job;
        int priority;

        JobWithPriority(Job job_, int priority_)
            : job(job_), priority(priority_) {}

        bool operator< (const JobWithPriority & rhs) const
        {
            return priority < rhs.priority;
        }
    };

    boost::heap::priority_queue<JobWithPriority> jobs;
    std::list<Thread> threads;
    std::exception_ptr first_exception;

    template <typename ReturnType>
    ReturnType scheduleImpl(Job job, int priority, std::optional<uint64_t> wait_microseconds);

    void worker(typename std::list<Thread>::iterator thread_it);

    void finalize();
}
```
Task 是 Clickhouse 自己写的 coroutine，所有的后台操作都可以通过实现 `IExecutableTask` 来创建一个 task。每个 Task 都是 a sequence of steps, 较重的 task 需要更多的 steps，Executor 每次只调度 task 执行一个 step，这样可以实现不同 task 穿插执行。





Executor 是将 ThreadPool 与 Task 封装在一起构成的更加“高级”类。
```c++
template <class Queue>
class MergeTreeBackgroundExecutor
{
public:
    MergeTreebackgroundExecutor(
        String name_,
        size_t threads_count_,
        size_t max_tasks_count_,
        CurrentMetrics::Metric metric_)
        : name(name_)
        , threads_count(thread_count_)
        , max_tasks_cound(max_tasks_count_),
        , metric(metric_)
    {
        ...
        if (max_tasks_count == 0)
            throw Exception(ErrorCodes::LOGICAL_ERROR, "Task count for MergeTreeBackgroundExecutor must not be zero");

        pending.setCapacity(max_tasks_count);
        active.set_capacity(max_tasks_count);

        pool.setMaxThreads(std::max(1UL, threads_count));
        pool.setMaxFreeThreads(std::max(1UL, threads_count));
        pool.setQueueSize(std::max(1UL, threads_count));

        for (size_t number = 0; number < threads_count; ++number)
            pool.scheduleOrThrowOnError([this] { threadFunction(); });
    }
    ...
    bool trySchedule(ExecutableTaskPtr task);
    ...
    void wait();

private:
    String name;
    size_t threads_count;
    size_t max_tasks_cound;
    CurrentMetrics::Metric metric;
}
```




```c++
class MergePlainMergeTreeTask : public IExecutableTask
{
public:

    bool executeStep() override;
    void onCompleted() override;
    StorageID getStorageID() override;
    UInt64 getPriority() override { return priority; }

private:
    void prepare();
    void finish();

    enum class State
    {
        NEED_PREPARE,
        NEED_EXECUTE,
        NEED_FINISH,

        SUCCESS
    };

    State state{State::NEED_PREPARE};
}
```
