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
所以，当后台Merge任务执行是最终执行的是`MergeTreeData::getDataProcessingJob()`