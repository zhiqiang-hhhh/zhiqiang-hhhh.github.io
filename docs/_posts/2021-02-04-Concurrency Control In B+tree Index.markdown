---
layout: post
title:  "Concurrency Control In B+tree Index"
date:   2021-02-04 14:50:11 +0800
categories: jekyll update
---
# B+树索引的并发控制
B+树是数据库索引的常用数据结构。本文不会介绍B+树的实现，而是专注于对B+树索引的并发控制。

我们知道，并发控制中最重要的用来管理共享资源的“东西”就是锁。在B+树的并发控制中，理解锁的实现以及作用是非常重要的，这关系到我们最终实现的索引在使用时的效率。
那么锁到底是个什么“东西”呢？
## 锁是个什么“东西”

    Situations where two or more processes are reading or writing some shared data and the final result depends on who runs precisely when, are called race conditions.

在操作系统原理中，有对锁的最初启蒙，锁是作为解决竟态（Race Condition）的工具被提出的。锁实际上是一种互斥（Mutual exclusion）机制，该机制需要保证没有两个进程/线程能够同时进入临界区（Critical region）为了实现这种机制有不同的方法：
### Mutual exclusion with busy waiting

    Busy waiting: Continuously testing a variable until some value appears is called busy waiting.

busy waiting的原理很简单，就是并发的线程持续不断地检测某个共享变量的值是否为某个期待值，如果满足期待，则说明当前线程可以进入临界区，否则就一直循环检测。在离开临界区时，线程将该共享变量的值恢复，以允许其他线程进入临界区。

这种实现需要硬件的支持：需要硬件提供TSL(Test and Set Lock)/CAS(Compare and Set)指令支持。这类指令将原来需要多条指令配合才能完成的行为简化为一条指令，目的是防止在对共享的lock-variable检测期间发生线程/进程的切换。

在C++标准库中提供了`std::atomic`库，用来调用CAS指令。

### Mutual exclusion with Sleep and Wakeup
Busy-waitings实现的互斥锁问题在于并发的线程会一直占用CPU直到时间片用完，如果这些线程没有其他任务去做，那么在等待的这段时间完全是在空占CPU。当临界区的访问会持续较长时间时，这种实现的效率不高。

C++标准库提供了`std::mutex`来提供互斥锁的另一种实现。mutex只有两种状态：locked or unlocked。
当一个线程想要访问临界区时，它调用 mutex_lock，如果 mutex 当前状态为 locked，则线程会被阻塞，直到另一个成功获取了mutex的线程将mutex的状态还原为unlocked。


## 读写锁
上述两类锁是从实现角度区分的，简单说，前者加锁失败后不会阻塞，后者会阻塞。而从实际功能上来说，还可以将锁与其他逻辑封装在一起，实现具有不同功能的锁。最典型的就是读写锁。

在数据库系统中，不同的线程往往具有不同的任务。有的比如查询线程只会读取索引以访问数据，有些执行写入/删除任务的线程则会修改索引结构。如果这两类线程都只使用统一的互斥锁，那么带来的问题就是：任何时刻只会有一个线程能够访问索引。很明显，我们期望的行为模式应该是：可以并发读，不能并发写。

为了提供这种访问索引的能力，我们需要将互斥锁封装一下，以读写锁的形式对外提供。

### 读写锁的逻辑
对于使用者来说，读写锁提供的逻辑很简单：
1. 读可以并发：同时有多个读线程同时读
2. 写必须互斥：任意时刻只能有一个写线程在写数据
3. 读写必须互斥：读和写不能并发，要获得读锁，必须确保没有线程在写；要获得写锁，必须确保没有线程在读。

### 读写锁实现
```c++
#include <climits>
#include <condition_variable>
#include <mutex>               

class WriterReaderLock{
public:
    WriterReaderLock() = default;
    ~WriterReaderLock(){ std::lock_guard<mutex_> guard{mutex_}; }

    void WLock(){
        std::lock_guard<mutex_t> guard{mutex_};
        while(writer_entered_){
            reader_.wait(mutex_);
        }
        writer_entered_ = true;
        while(reader_count > 0){
            writer_.wait(mutex_);
        }
    }

    void WULock(){
        std::lock_guard<mutex_t> guard{mutex_};
        writer_entered_ = false;
        reader_.notify_all();
    }

    void RLock(){
        std::lock_guard<mutex_t> guard{mutex_};
        while(writer_entered || reader_count_ == MAX_READERS){
            reader_.wait(mutex_);
        }
        reader_count_++;
    }

    void RULock(){
        std::lock_guard<mutex_t> guard{mutex_};
        reader_count_--;
        if(writer_entered_){
            if(reader_count_ == 0){
                writer_.notify_one();
            }
        } else {
            if(reader_count_ == MAX_READERS - 1){
                reader_.notify_one();
            }
        }
    }

private:
    using mutex_t = std::mutex;
    using cond_t = std::condition_variable;
    static const uint32_t MAX_READERS = UINT_MAX;

    mutex_t mutex_;
    cond_t writer_;
    cond_t reader_;
    uint32_t reader_count_{0};
    bool writer_entered_{false};
};
```

## B+树的并发访问

具体到B+树的并发访问，一个最简单的方法是使用一个全局的锁来管理整个B+树。这当然不是一个好的方法。

我们需要将锁的粒度降低到Node级别。此时，最直观的想法是，每当要要访问一个新节点，就获取一个该节点的读/写锁。当离开该节点后释放这个锁。

如果我们只有读操作，那毫无疑问这种方法是可行的，甚至加锁都多余了，啥锁都不需要。
当读写操作混合时，这种方案就会有问题。具体例子我这里不写了，CMU15445课件里解释的很清楚。

这里我们专注于如何进行并发控制
### Crabbing protocol

对于读操作：
* 首先对根节点加锁，当沿着树往下遍历时，首先对下一个子节点加锁，在对子节点加锁成功后，再释放父节点的锁。重复此过程直到到达叶子节点。

对于写操作（插入/删除）：
* 首先与读操作的过程一样，访问到叶子节点
* 获得目标叶子节点的写锁，然后插入/删除
* 如果当前节点需要分裂或者与sibling进行coalesce/redistribute，那么需要获得parent的写锁，然后再开始操作。操作完成后，释放parent/sibling/node的写锁。

当然了，Crabbing protocol只是能够满足要求的最简单的一种协议，对于B+树的并发访问有很多可以值得优化的地方。
