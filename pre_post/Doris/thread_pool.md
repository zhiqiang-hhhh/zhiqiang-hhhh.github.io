```cpp {.line-numbers}
template <bool Priority = false>
class WorkThreadPool {
public:
    using WorkFunction = std::function<void()>;
    struct Task {
    public:
        int priority;
        WorkFunction work_function;
        bool operator<(const Task& o) const { return priority < o.priority; }
        Task& operator++() {
            priority += 2;
            return *this;
        }
    }

    using WorkQueue =
            std::conditional_t<Priority, BlockingPriorityQueue<Task>, BlockingQueue<Task>>;

    WorkThreadPool(uint32_t num_threads, uint32_t queue_size, const std::string& name)
            : _work_queue(queue_size), _shutdown(false), _name(name), _active_threads(0) {
        for (int i = 0; i < num_threads; ++i) {
            _threads.create_thread(
                    std::bind<void>(std::mem_fn(&WorkThreadPool::work_thread), this, i));
        }
    }
protected:
    ThreadGroup _threads;

private:
    void work_thread(int thread_id) {
        Thread::set_self_name(_name);
        while (!is_shutdown()) {
            Task task;
            if (_work_queue.blocking_get(&task)) {
                _active_threads++;
                task.work_function();
                _active_threads--;
            }
            if (_work_queue.get_size() == 0) {
                _empty_cv.notify_all();
            }
        }
    }
...
}
```
`WorkThreadPool` 构造的时候，创建 num_threads 个线程，每个线程都会阻塞在第 34 行的 `_work_queue.blocking_get(&task)` 上，等待有任务被提交。