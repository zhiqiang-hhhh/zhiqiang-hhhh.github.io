[TOC]

### ThreadPoolImpl

```plantuml
class JobWithPriority
{
    + job : Job
    + priority : int
    + operator < () : bool
}

class ThreadPoolImpl
{
    - max_threads : size_t
    - max_free_threads : size_t
    - queue_size : size_t
    - mutex : std::mutex
    - job_finished : cond_var
    - new_job_or_shutdown : cond_var
    - jobs : priority_queue<JobWithPriority>
    - threads : list<Thread>
    
    + {abstract} scheduleOrThrownOnError(Job, priority) : void
    + {abstract} trySchedule(Job, priority, wait_time) : bool
    + {abstract} scheduleOrThrow(Job, priority, wait_time) : bool

    - worker(typename std::list<Thread>::iterator thread_it) : void 
    - finalize() : void
}

note left of ThreadPoolImpl::worker
    typename:
    template dependent type 
    法区分是type还是value，需要指定
end note

ThreadPoolImpl *-- JobWithPriority
ThreadPoolImpl <-- GlobalThreadPool

class GlobalThreadPool
```
```c++
template <typename Thread>
template <typename ReturnType>
ReturnType ThreadPoolImpl<Thread>::scheduleImpl(Job job, int priority, std::optional<uint64_t> wait_microseconds)
{
    std::unique_lock lock(mutex);

    /// A
    auto pred = [this] { return !queue_size || scheduled_jobs < queue_size || shutdown; }

    /// B
    if (wait_microseconds)  /// Check for optional. Condition is true if the optional is set and the value is zero.
    {
        if (!job_finished.wait_for(lock, std::chrono::microseconds(*wait_microseconds), pred))
            return on_error(fmt::format("no free thread (timeout={})", *wait_microseconds));
    }
    else
        job_finished.wait(lock, pred);

    if (shutdown)
        return on_error("shutdown");

    
    /// We must not to allocate any memory after we emplaced a job in a queue.
    /// Because if an exception would be thrown, we won't notify a thread about job occurrence.

    /// C
    /// Check if there are enough threads to process job.
    if (threads.size() < std::min(max_threads, scheduled_jobs + 1))
    {
        try
        {
            threads.emplace_front();
        }
        catch (...)
        {
            /// Most likely this is a std::bad_alloc exception
            return on_error("cannot allocate thread slot");
        }

        try
        {
            threads.front() = Thread([this, it = threads.begin()] { worker(it); });
        }
        catch (...)
        {
            threads.pop_front();
            return on_error("cannot allocate thread");
        }
    }

    /// D
    jobs.emplace(std::move(job), priority);
    ++scheduled_jobs;
    new_job_or_shutdown.notify_one();

    return ReturnType(true);
}
```
ThreadPoolImpl 的整体调度流程：
1. 将 Job 添加到 jobs 这个优先队列中
2. 从 jobs 队列中选择一个 job 将其交给某个 thread 去执行

- max_threads: maximum number of running jobs
- queue_size: maximum number of running plus scheduled jobs. 
  
queue_size = max(max_threads, queue_size)

即，如果 queue_size 一定大于等于 max_threads。jobs 优先队列的大小等于 queue_size。

* A
  如果 queue_size 为 0，或者runningJobs + scheduledJobs < queue_size，或者线程池被 shutdown 了，则返回 pred 为 true。pred 为 true 表示可以将 Job 添加到 jobs 队列中。
 
* B
  如果 pred 为 true，则这里 condition_var 将不会等待，直接跳过，否则需要等待有 Job 完成腾出位置。
  如果 caller 传入了 wait_microseconds，则如果超出 wait_microseconds 后任然没有任何正在执行的 Job finish，则本次 scheduleImpl 失败；
  如果 caller 没有指定调度时长，则将会一直等待有某个 job 完成；

* C
  执行到 C 说明有 pred 一定为 true 且线程池未被 shutdown。C 最主要的部分是创建一个 worker，并且将其绑定到某个 thread。worker 在创建后并不会立刻执行当前传入的 Job，而是等待 job 被加入到 jobs 后，从 new_job_or_shutdown 唤醒，然后根据优先级从 jobs 中选择一个 job 去执行。

* D
  这部分就是将 Job 添加到 jobs 队列，并且通知 worker 运行。

下面看一下 worker 的执行
```c++
template <typename Thread>
void ThreadPoolImpl<Thread>::worker(typename std::list<Thread>::iterator thread_it)
{
    while (true)
    {
        Job job;
        bool need_shutdown = false;

        {
            /// A
            std::unique_lock lock(mutex);
            new_job_or_shutdown.wait(lock, [this] { return shutdown || !jobs.empty(); });
            need_shutdown = shutdown;

            /// B
            if (!jobs.empty())
            {
                /// boost::priority_queue does not provide interface for getting non-const reference to an element
                /// to prevent us from modifying its priority. We have to use const_cast to force move semantics on JobWithPriority::job.
                job = std::move(const_cast<Job &>(jobs.top().job));
                jobs.pop();
            }
            else
            {
                /// shutdown is true, simply finish the thread.
                return;
            }
            
            ...

            /// C
            try
            {
                job();
                job = {}
            }

            /// D
            {
                std::unique_lock lock(mutex);
                --scheduled_jobs;

                if (threads.size() > scheduled_jobs + max_free_threads)
                {
                    thread_it->detach();
                    threads.erase(thread_it);
                    job_finished.notify_all();
                    return;
                }
            }

            job_finished.notify_all();
        }
    }
}
```
* A
  当 scheduleImpl 的 D 步骤执行 `new_job_or_shutdown.notify_one()` 之后，worker 从 A 处唤醒

* B
  选择 jobs 中优先级最高的 job，并将其从队列中移除

* C
  执行 job

* D
  Job 执行完成，减少 scheduled_jobs 计数（这里可以看到该参数记录的是 running + queued jobs)。如果 threads 的数量超过了 scheduled + max_free_threads，即如果空闲线程的数量超出了 max_free_threads，则将该线程 detach 并且析构。否则当前 worker 将会尝试从 jobs 中选择下一个 job 去执行。
  
### BackgroundSchedulePool
```c++
BackgroundSchedulePool & Context::getSchedulePool() const
{
    auto lock = getLock();
    if (!shared->schedule_pool)
        shared->schedule_pool.emplace(
            settings.background_schedule_pool_size,
            CurrentMetrics::BackgroundSchedulePoolTask,
            "BgSchPool");
    return *shared->schedule_pool;
}
```
```c++
BackgroundSchedulePool::BackgroundSchedulePool(size_t size_, CurrentMetrics::Metric tasks_metric_, const char *thread_name_)
    : size(size_)
    , tasks_metric(tasks_metric_)
    , thread_name(thread_name_)
{
    LOG_INFO(&Poco::Logger::get("BackgroundSchedulePool/" + thread_name), "Create BackgroundSchedulePool with {} threads", size);

    threads.resize(size);
    for (auto & thread : threads)
        thread = ThreadFromGlobalPool([this] { threadFunction(); });

    delayed_thread = ThreadFromGlobalPool([this] { delayExecutionThreadFunction(); });
}
```
BackgroundSchedulePool 对象构造时，向 GlobalThreadPool 中添加了 size 个 job，每个 job 在被执行的时候，执行的是 `BackgroundSchedulePool::threadFunction()`。


NOTE：存疑，好像既有 context 中全局共享的 BgSchPool 也有各个 table 自己独占的。
size 大小由 settings.background_schedule_pool_size 决定，默认为 128。**这 128 个线程是全局所有 table 共用的。**
```c++
void BackgroundSchedulePool::threadFunction()
{
    setThreadName(thread_name.c_str());

    attachToThreadGroup();

    while (!shutdown)
    {
        /// We have to wait with timeout to prevent very rare deadlock, caused by the following race condition:
        /// 1. Background thread N: threadFunction(): checks for shutdown (it's false)
        /// 2. Main thread: ~BackgroundSchedulePool(): sets shutdown to true, calls queue.wakeUpAll(), it triggers
        ///    all existing Poco::Events inside Poco::NotificationQueue which background threads are waiting on.
        /// 3. Background thread N: threadFunction(): calls queue.waitDequeueNotification(), it creates
        ///    new Poco::Event inside Poco::NotificationQueue and starts to wait on it
        /// Background thread N will never be woken up.
        /// TODO Do we really need Poco::NotificationQueue? Why not to use std::queue + mutex + condvar or maybe even DB::ThreadPool?
        constexpr size_t wait_timeout_ms = 500;
        if (Poco::AutoPtr<Poco::Notification> notification = queue.waitDequeueNotification(wait_timeout_ms))
        {
            TaskNotification & task_notification = static_cast<TaskNotification &>(*notification);
            task_notification.execute();
        }
    }
}
```
每个 background_thread 都是一个无限循环，从 BackgroundSchedulePool::queue 中取一个 task 来执行。

#### BackgroundThreadPool 任务调度
使用者通过`TaskHolder createTask(const std::string & log_name, const TaskFunc & function)` 来创建一个 BackgroundSchedulePoolTaskHolder，其中包含一个 BackgroundSchedulePoolTaskInfo。

注意，createTask 函数并不是立刻将 task 添加到 BgSchPool 的 queue 中。这里 createTask 可能更应该理解为是注册一个 task，所以后续将使用注册 task 来指代 createTask 函数。

当使用者希望 Task 被执行时，通过 `BackgroundSchedulePoolTaskInfo::scheduleImpl()` 来将任务添加到 BgSchPool 的 queue 内：
```c++
void BackgroundSchedulePoolTaskInfo::scheduleImpl(std::lock_guard<std::mutex> & schedule_mutex_lock)
{
    scheduled = true;

    if (delayed)
        pool.cancelDelayedTask(shared_from_this(), schedule_mutex_lock);

    /// If the task is not executing at the moment, enqueue it for immediate execution.
    /// But if it is currently executing, do nothing because it will be enqueued
    /// at the end of the execute() method.
    if (!executing)
        pool.queue.enqueueNotification(new TaskNotification(shared_from_this()));
}
```
添加完成后，如果有空闲的 background thread，那么它将会从 threadFunction 中被唤醒，执行 task。

#### BackgroundJobsAssignee
* 任务注册
  
MergeTreeData 构造的时候会构造两个 BackgroundJobsAssignee。

```c++
class BackgroundJobsAssignee
{
private:
    MergeTreeData & data;
    BackgroundSchedulePool::TaskHolder holder;

public:
    void start();
    ...
private:
    /// Function that executes in background scheduling pool
    void threadFunc();  
}

void BackgroundJobsAssignee::start()
{
    std::lock_guard lock(holder_mutex);
    if (!holder)
        holder = getContext()->getSchedulePool().createTask("BackgroundJobsAssignee:" + toString(type), [this]{ threadFunc(); });

    holder->activateAndSchedule();
}

void BackgroundJobsAssignee::threadFunc()
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
```

在 `BackgroundJobsAssignee::start()` 中，向全局的 BgSchPool 注册一个 task，task 执行的函数为 `BackgroundJobsAssignee::threadFunc()`

* 任务触发
```c++
void BackgroundJobsAssignee::trigger()
{
    std::lock_guard lock(holder_mutex);

    if (!holder)
        return;

    /// Do not reset backoff factor if some task has appeared,
    /// but decrease it exponentially on every new task.
    no_work_done_count /= 2;
    /// We have background jobs, schedule task as soon as possible
    holder->schedule();
}
```


### ReplicatedMergeTree 的 BgSchPool
MergeTree 没有特殊的 BgSchPool，就是 MergeTreeData 里的两个 BackgroundJobsAssignee。分别负责 MergeMutate 和 Move。着重看一下 ReplicatedMergeTree。

```c++
StorageReplicatedMergeTree::StorageReplicatedMergeTree(
        ...
    ) : MergeTreeData (...)
    , ...
    , cleanup_thread(*this)
    , part_check_thread(*this)
    , restarting_thread(*this)
    , ...
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
已经把 ReplicatedMergeTree 构造函数中与任务创建相关的代码都列出来了。函数内部的 4个 task 均注册在了全局的 BgSchPool 内。三个 thread 中我们挑 
ReplicatedMergeTreeCleanupThread 来分析。

```c++
ReplicatedMergeTreeCleanupThread::ReplicatedMergeTreeCleanupThread(StorageReplicatedMergeTree & storage_)
    : storage(storage_)
    , log_name(storage.getStorageID().getFullTableName() + " (ReplicatedMergeTreeCleanupThread)")
    , log(&Poco::Logger::get(log_name))
{
    task = storage.getContext()->getSchedulePool().createTask(log_name, [this]{ run(); });
}
```
每个 thread 也是在全局的 BgSchPool 内注册一个 task。


### 任务触发
