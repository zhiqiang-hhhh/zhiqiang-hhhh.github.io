Server::main

```plantuml
BaseDaemon <-- Server
IServer <-- Server

class IServer
{
    {abstract} config() : Configuration
    {abstract} context() : ContextMutablePtr
}

class Server
```
关于 path
```c++
Server::main()
{
    ...
    std::string path_str = getCanonicalPath(config().getString("path", DBMS_DEFAULT_PATH));
    fs::path path = path_str;
    ...
    global_context->setPath(path_str);
    ...
}
```



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
  
```plantuml
class BackgroundSchedulePool
{

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
BackgroundSchedulePool 在构造的时候，是向 GlobalThreadPool 中添加了 size 个 job，每个 job 在被执行的时候，执行的是 `BackgroundSchedulePool::threadFunction()`。