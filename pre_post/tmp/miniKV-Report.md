# 云架构平台部 miniProject 实现报告

作者：数据库研发中心-OLAP内核组-roanhe（何智强）

## 概述

编程语言：C++
测试框架：gtest

实现的功能：
1. KV-Server 的 Insert，update，delete，get接口；
2. 上述接口的单线程/多线程功能测试；
3. 通过内存池 和 B+Tree 实现数据持久化到磁盘

## 各部分实现介绍

**整体实现参考了 CMU-15445 的课程实验**

### 磁盘管理
通过 DiskManager 类来管理磁盘文件。类图如下：
```plantuml
class DiskManager {
    + ReadPage(page_id, chat* page_data) : void
    + WritePage(page_id, char* page_data) : void
    + AllocatePage() : page_id_t
    + DellocatePage(page_id) : void
    - db_file_name : string
    - db_io : std::fstream
    - next_page_id : page_id_t
}
```

每个磁盘上的 database 文件被划分为大小为 16KB 的 page，通过 DiskManager 来读写 database file
### 内存池

通过 BufferPoolManager （BPM）来管理内存中的 page。类图如：
```plantuml
class BufferPoolManager {
    + FetchPage(page_id) : PagePtr
    + UnpinPage(page_id, is_dirty) : bool
    + FlushPage(page_id) : bool
    + NewPage() : PagePtr
    + DeletePage(page_id) : bool
    - pool_size : size_t
    - disk_manager : DiskManager
    - replacer : IReplacer
    - pages : vector<PagePtr>
    - free_list : list<PagePtr>
    - latch : mutex
    - page_table : Map<PageId, FrameId>
}
```
BPM 管理内存中的 Page，通过 Replacer 来决定当内存池满了之后应该将哪个 Page 置换到磁盘。目前默认策略为 LRU。

### B+Tree

通过 Lock crabbing 实现了对 B+Tree 的并发访问。每个 B+Tree 的 node 均被持久化到磁盘对应的 page 内，每个 node 中可以包含的 entry 数量由如下公式计算：
1. InternalNode： （PAGE_SIZE - INTTER_HEADER_SIZE) / SIZE_OF_KVPAIR - 1
2. LeafNode: （PAGE_SIZE - LEAF_HEADER_SIZE) / SIZE_OF_KVPAIR

InternalNode 之所以空出来一个位置是为了防止在节点裂变前的最后一次写入导致内存越界。

#### InternalNode

Header: 

    Header format (size in byte, 20 bytes in total):
    -------------------------------------------------
    | PageType (4) |  CurrentSize (4) | MaxSize (4) |
    -------------------------------------------------
    | ParentPageId (4) | PageId (4) |
    ---------------------------------

#### LeafNode

Header:

    Header format (size in byte, 24 bytes in total):
    -------------------------------------------------
    | PageType (4) |  CurrentSize (4) | MaxSize (4) |
    -------------------------------------------------
    | ParentPageId (4) | PageId (4) | NextPageId (4) ｜
    -------------------------------------------------

### 测试

使用 gtest 框架，构建了三个测试场景：
1. 并发写入
2. 并发删除
3. 并发读数据

**并发写入**
4个线程总共写入 200000 个 KV，每个 Key 均随机产生，对应的 Value 为其低 32 位。4个写入线程写入完成后，单线程读所有的 Key，验证 Value
是否正确。

**并发删除**
提前写入 200000 个 KV，然后将前 100 个 Key 均匀分给 10 个线程去执行删除。删除执行完成后，单线程读所有被写入的 Key，验证被删除的 Key 不存在

**并发读**
单线程写入 20000 个 KV，然后创建 10 个读线程，读所有的 Key，验证 Value 是否正确
