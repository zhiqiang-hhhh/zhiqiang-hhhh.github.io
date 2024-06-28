
<!-- @import "[TOC]" {cmd="toc" depthFrom=1 depthTo=6 orderedList=false} -->

<!-- code_chunk_output -->

- [表，分区， 分桶](#表分区-分桶)
- [rowset segment](#rowset-segment)
- [代码](#代码)
  - [ScanLocalState::open](#scanlocalstateopen)
    - [创建 Scanner 的策略](#创建-scanner-的策略)
      - [tablet reader](#tablet-reader)
  - [Scanner::init](#scannerinit)
  - [Scanner::open](#scanneropen)
  - [ScannerContext::get_free_block](#scannercontextget_free_block)
  - [Scanner::get_block_after_projects](#scannerget_block_after_projects)
  - [get next](#get-next)

<!-- /code_chunk_output -->



ref
* https://blog.csdn.net/SHWAITME/article/details/136155008?spm=1001.2014.3001.5502
* https://mp.weixin.qq.com/s/vc5gafqHyxKvMiLiYVyRLQ

### 表，分区， 分桶
分区：逻辑概念
分桶：存储的物理概念
```sql
CREATE TABLE debug_storage(pkey int, bkey varchar(1))
    PARTITION BY range(pkey) (
        PARTITION `p1` VALUES LESS THAN (5),
        PARTITION `p2` VALUES LESS THAN (10))
    DISTRIBUTED BY HASH(bkey) BUCKETS 2
    properties("replication_num"="1")

insert into debug_storage values(1, "A"), (1,"B"), (5,"A"), (5,"B")
```
一共两个分区，每个分区都有两个分桶，因此 debug_storage 这个 table 一共 4 个 tablet
```sql
explain select * from debug_storage
--------------

+-------------------------------------------------------------------+
| Explain String(Nereids Planner)                                   |
+-------------------------------------------------------------------+
| PLAN FRAGMENT 0                                                   |
|   OUTPUT EXPRS:                                                   |
|     pkey[#0]                                                      |
|     bkey[#1]                                                      |
|   PARTITION: RANDOM                                               |
|                                                                   |
|   HAS_COLO_PLAN_NODE: false                                       |
|                                                                   |
|   VRESULT SINK                                                    |
|      MYSQL_PROTOCAL                                               |
|                                                                   |
|   0:VOlapScanNode(43)                                             |
|      TABLE: demo.debug_storage(debug_storage), PREAGGREGATION: ON |
|      partitions=2/2 (p1,p2)                                       |
|      tablets=4/4, tabletList=2746073,2746075,2746077 ...          |
|      cardinality=0, avgRowSize=0.0, numNodes=1                    |
|      pushAggOp=NONE                                               |
+-------------------------------------------------------------------+
```
```sql
show tablets from debug_storage
--------------

+----------+-----------+-----------+------------+---------+-------------------+------------------+---------------+---------------+----------------+----------+--------+-------------------------+--------------+---------------------+--------------+-----------+----------------------+---------------------------------------------------+------------------------------------------------+--------------------------------------------------------------+-------------------+----------------+
| TabletId | ReplicaId | BackendId | SchemaHash | Version | LstSuccessVersion | LstFailedVersion | LstFailedTime | LocalDataSize | RemoteDataSize | RowCount | State  | LstConsistencyCheckTime | CheckVersion | VisibleVersionCount | VersionCount | QueryHits | PathHash             | Path                                              | MetaUrl                                        | CompactionStatus                                             | CooldownReplicaId | CooldownMetaId |
+----------+-----------+-----------+------------+---------+-------------------+------------------+---------------+---------------+----------------+----------+--------+-------------------------+--------------+---------------------+--------------+-----------+----------------------+---------------------------------------------------+------------------------------------------------+--------------------------------------------------------------+-------------------+----------------+
| 2746073  | 2746074   | 10005     | 1366655391 | 2       | 2                 | -1               | NULL          | 0             | 0              | 0        | NORMAL | NULL                    | -1           | 2                   | 2            | 0         | -9005019231402287789 | /mnt/disk1/hezhiqiang/workspace/ASAN/be_1/storage | http://10.16.10.8:8241/api/meta/header/2746073 | http://10.16.10.8:8241/api/compaction/show?tablet_id=2746073 | -1                |                |
| 2746075  | 2746076   | 10005     | 1366655391 | 2       | 2                 | -1               | NULL          | 440           | 0              | 2        | NORMAL | NULL                    | -1           | 2                   | 2            | 0         | -9005019231402287789 | /mnt/disk1/hezhiqiang/workspace/ASAN/be_1/storage | http://10.16.10.8:8241/api/meta/header/2746075 | http://10.16.10.8:8241/api/compaction/show?tablet_id=2746075 | -1                |                |
| 2746077  | 2746078   | 10005     | 1366655391 | 2       | 2                 | -1               | NULL          | 0             | 0              | 0        | NORMAL | NULL                    | -1           | 2                   | 2            | 0         | -9005019231402287789 | /mnt/disk1/hezhiqiang/workspace/ASAN/be_1/storage | http://10.16.10.8:8241/api/meta/header/2746077 | http://10.16.10.8:8241/api/compaction/show?tablet_id=2746077 | -1                |                |
| 2746079  | 2746080   | 10005     | 1366655391 | 2       | 2                 | -1               | NULL          | 442           | 0              | 2        | NORMAL | NULL                    | -1           | 2                   | 2            | 0         | -9005019231402287789 | /mnt/disk1/hezhiqiang/workspace/ASAN/be_1/storage | http://10.16.10.8:8241/api/meta/header/2746079 | http://10.16.10.8:8241/api/compaction/show?tablet_id=2746079 | -1                |                |
+----------+-----------+-----------+------------+---------+-------------------+------------------+---------------+---------------+----------------+----------+--------+-------------------------+--------------+---------------------+--------------+-----------+----------------------+---------------------------------------------------+------------------------------------------------+--------------------------------------------------------------+-------------------+----------------+
```
分区数量是 2，这里 A 跟 B 都被分到了同一个分桶中，所以 73 跟 77 中数据大小为 0。看一下目录结构：
```bash
[hezhiqiang@VM-10-8-centos storage]$ find . -type d -name 2746073
./data/20/2746073
[hezhiqiang@VM-10-8-centos storage]$ find . -type d -name 2746075
./data/21/2746075
[hezhiqiang@VM-10-8-centos storage]$ find . -type d -name 2746077
./data/22/2746077
[hezhiqiang@VM-10-8-centos storage]$ find . -type d -name 2746079
./data/23/2746079
[hezhiqiang@VM-10-8-centos storage]$ cd ./data/21/2746075/1366655391/
[hezhiqiang@VM-10-8-centos 1366655391]$ ls
0200000000000034024daaf90dc78b9be0dee7c6939de5a8_0.dat
[hezhiqiang@VM-10-8-centos 1366655391]$ pwd
/mnt/disk1/hezhiqiang/workspace/ASAN/be_1/storage/data/21/2746075/1366655391
[hezhiqiang@VM-10-8-centos 2746079]$ cd /mnt/disk1/hezhiqiang/workspace/ASAN/be_1/storage/data/23/2746079
[hezhiqiang@VM-10-8-centos 2746079]$ ls
1366655391
[hezhiqiang@VM-10-8-centos 2746079]$ cd 1366655391/
[hezhiqiang@VM-10-8-centos 1366655391]$ ls
0200000000000033024daaf90dc78b9be0dee7c6939de5a8_0.dat
```
22: 分桶 ID
2746075：tablet ID
1366655391: rowset 文件名
0200000000000034024daaf90dc78b9be0dee7c6939de5a8_0.dat: segment 文件名

**每个 tablet 下都有相同的 rowset 文件名，以及 segment 文件名**



### rowset segment
rowset 是逻辑概念，用于进行版本控制。作用类似于 clickhouse 中 data_part 的版本后缀。

```sql
mysql [demo]>insert into debug_storage values(2, "C"), (2,"D"), (6,"C"), (6,"D");
--------------
insert into debug_storage values(2, "C"), (2,"D"), (6,"C"), (6,"D")
--------------

Query OK, 4 rows affected (0.09 sec)
{'label':'label_cacc51e75b184b5c_b59697531fd3f7dd', 'status':'VISIBLE', 'txnId':'141022'}

mysql [demo]>show tablets from debug_storage;
--------------
show tablets from debug_storage
--------------

+----------+-----------+-----------+------------+---------+-------------------+------------------+---------------+---------------+----------------+----------+--------+-------------------------+--------------+---------------------+--------------+-----------+----------------------+---------------------------------------------------+------------------------------------------------+--------------------------------------------------------------+-------------------+----------------+
| TabletId | ReplicaId | BackendId | SchemaHash | Version | LstSuccessVersion | LstFailedVersion | LstFailedTime | LocalDataSize | RemoteDataSize | RowCount | State  | LstConsistencyCheckTime | CheckVersion | VisibleVersionCount | VersionCount | QueryHits | PathHash             | Path                                              | MetaUrl                                        | CompactionStatus                                             | CooldownReplicaId | CooldownMetaId |
+----------+-----------+-----------+------------+---------+-------------------+------------------+---------------+---------------+----------------+----------+--------+-------------------------+--------------+---------------------+--------------+-----------+----------------------+---------------------------------------------------+------------------------------------------------+--------------------------------------------------------------+-------------------+----------------+
| 2746073  | 2746074   | 10005     | 1366655391 | 3       | 3                 | -1               | NULL          | 0             | 0              | 0        | NORMAL | NULL                    | -1           | 2                   | 2            | 0         | -9005019231402287789 | /mnt/disk1/hezhiqiang/workspace/ASAN/be_1/storage | http://10.16.10.8:8241/api/meta/header/2746073 | http://10.16.10.8:8241/api/compaction/show?tablet_id=2746073 | -1                |                |
| 2746075  | 2746076   | 10005     | 1366655391 | 3       | 3                 | -1               | NULL          | 440           | 0              | 2        | NORMAL | NULL                    | -1           | 2                   | 2            | 0         | -9005019231402287789 | /mnt/disk1/hezhiqiang/workspace/ASAN/be_1/storage | http://10.16.10.8:8241/api/meta/header/2746075 | http://10.16.10.8:8241/api/compaction/show?tablet_id=2746075 | -1                |                |
| 2746077  | 2746078   | 10005     | 1366655391 | 3       | 3                 | -1               | NULL          | 0             | 0              | 0        | NORMAL | NULL                    | -1           | 2                   | 2            | 0         | -9005019231402287789 | /mnt/disk1/hezhiqiang/workspace/ASAN/be_1/storage | http://10.16.10.8:8241/api/meta/header/2746077 | http://10.16.10.8:8241/api/compaction/show?tablet_id=2746077 | -1                |                |
| 2746079  | 2746080   | 10005     | 1366655391 | 3       | 3                 | -1               | NULL          | 442           | 0              | 2        | NORMAL | NULL                    | -1           | 2                   | 2            | 0         | -9005019231402287789 | /mnt/disk1/hezhiqiang/workspace/ASAN/be_1/storage | http://10.16.10.8:8241/api/meta/header/2746079 | http://10.16.10.8:8241/api/compaction/show?tablet_id=2746079 | -1                |                |
+----------+-----------+-----------+------------+---------+-------------------+------------------+---------------+---------------+----------------+----------+--------+-------------------------+--------------+---------------------+--------------+-----------+----------------------+---------------------------------------------------+------------------------------------------------+--------------------------------------------------------------+-------------------+----------------+
4 rows in set (0.00 sec)
```
写入数据后，rowset 下多了一个 segment 文件，文件名的区别是中间位置的 34 变成了 3c 
```bash
[hezhiqiang@VM-10-8-centos 1366655391]$ ls
0200000000000034024daaf90dc78b9be0dee7c6939de5a8_0.dat  020000000000003c024daaf90dc78b9be0dee7c6939de5a8_0.dat
```
我的猜测：每次 alter/compaction 会生成一个新的 rowset 目录。

### 代码
#### ScanLocalState::open
```cpp
template <typename Derived>
Status ScanLocalState<Derived>::open(RuntimeState* state) {
    ...
    auto status = _eos ? Status::OK() : _prepare_scanners();
    if (_scanner_ctx) {
        DCHECK(!_eos && _num_scanners->value() > 0);
        RETURN_IF_ERROR(_scanner_ctx->init());
    }
}

template <typename Derived>
Status ScanLocalState<Derived>::_prepare_scanners() {
    // 创建的是 Scanner 对象
    std::list<vectorized::VScannerSPtr> scanners;
    RETURN_IF_ERROR(_init_scanners(&scanners));
    
    // 创建的是 ScannerDelegate 对象
    for (auto it = scanners.begin(); it != scanners.end(); ++it) {
        _scanners.emplace_back(std::make_shared<vectorized::ScannerDelegate>(*it));
    }
    if (scanners.empty()) {
        _eos = true;
        _scan_dependency->set_always_ready();
    } else {
        for (auto& scanner : scanners) {
            scanner->set_query_statistics(_query_statistics.get());
        }
        COUNTER_SET(_num_scanners, static_cast<int64_t>(scanners.size()));
        RETURN_IF_ERROR(_start_scanners(_scanners));
    }
    return Status::OK();
}

Status OlapScanLocalState::_init_scanners(std::list<vectorized::VScannerSPtr>* scanners) {
    // 根据策略去创建scanner
    ...
}

template <typename Derived>
Status ScanLocalState<Derived>::_start_scanners(
    const std::list<std::shared_ptr<vectorized::ScannerDelegate>>& scanners) {
        _scanner_ctx = vectorized::ScannerContext::create_shared(
            ..., scanners, ... );
    ...
}
```

`Scan Node` 所在的 Pipeline 会有 ParallelPipelineTaskNum 个 PipelineTask
每个 PipelineTask 会根据策略创建 X 个 Scanner 对象
每个 PipelineTask 在 open 的时候会创建一个 ScannerContext，这个 ScannerContext 管理这个 PipelineTask 创建的 Scanner 对象。

每个 PipelineTask 创建的 Scanner 对象数量与 Tablet 数量有关，具体来说是一个策略问题。

ScanNodeOperator 会创建 parallel_pipeline_task 个 pipeline task，每个 pipeline task 会创建 X 个 scanner，X 等于多少是一个策略问题，总的 scanner 数量是 parallel_pipeline_task * X，策略的原则是这些所有的 scanner 去读所有涉及到的 tablet，互不重叠。PipelineTask A 只会从自己创建的那 X 个scanner中消费block，所以各个pipeline task 之间的数据也是不重叠的。
```cpp
template <typename Derived>
Status ScanLocalState<Derived>::open(RuntimeState* state) {
    ...
    auto status = _eos ? Status::OK() : _prepare_scanners();
    RETURN_IF_ERROR(status);
    if (_scanner_ctx) {
        RETURN_IF_ERROR(_scanner_ctx->init());
    }
}

Status ScannerContext::init() {
    ...
    for (int i = 0; i < _max_thread_num; ++i) {
        std::weak_ptr<ScannerDelegate> next_scanner;
        if (_scanners.try_dequeue(next_scanner)) {
            this->submit_scan_task(std::make_shared<ScanTask>(next_scanner));
            _num_running_scanners++;
        }
    }
}

void ScannerContext::submit_scan_task(std::shared_ptr<ScanTask> scan_task) {
    _scanner_sched_counter->update(1);
    _num_scheduled_scanners++;
    _scanner_scheduler->submit(shared_from_this(), scan_task);
}
```
`_max_thread_num` 是根据策略计算出来的一个值，普通情况下可以假设这里的值等于 `Scanner` 对象的数量。

那么对于每个 Scanner，我们会创建一个 `ScannerTask`，然后提交给 `_scanner_scheduler`

PipelinTask 在 open 阶段调用所有 operator 的 open 函数，对于 OlapScanOperator 来说，就是完成上面的 Scanner 相关数据结构的创建，并且把 ScanTask 提交到 ScannerScheduler 中。
##### 创建 Scanner 的策略
###### tablet reader
```cpp
template <typename Derived>
Status ScanLocalState<Derived>::_prepare_scanners() {
    std::list<vectorized::VScannerSPtr> scanners;
    RETURN_IF_ERROR(_init_scanners(&scanners));
    ...
}

Status OlapScanLocalState::_init_scanners(std::list<vectorized::VScannerSPtr>* scanners) {
    ParallelScannerBuilder scanner_builder(this, tablets, _scanner_profile, key_ranges, state(),
                                               p._limit, true, p._olap_scan_node.is_preaggregation);
    RETURN_IF_ERROR(scanner_builder.build_scanners(*scanners));
}

Status ParallelScannerBuilder::build_scanners(std::list<VScannerSPtr>& scanners) {
    if (_is_dup_mow_key) {
        return _build_scanners_by_rowid(scanners);
    }
}

Status ParallelScannerBuilder::_build_scanners_by_rowid(std::list<VScannerSPtr>& scanners) {
    for (auto&& [tablet, version] : _tablets) {
        auto& rowsets = _all_rowsets[tablet->tablet_id()];

        for (auto& rowset : rowsets) {
            if (beta_rowset->num_rows() == 0) {
                continue;
            }

            for (size_t i = 0; i != segments.size(); ++i) {
                const auto& segment = segments[i];
            }
        }
    }
}

Status BetaRowset::create_reader(RowsetReaderSharedPtr* result) {
    LOG_INFO("create rowset_reader, rowset id {}", this->rowset_id().to_string());
    result->reset(new BetaRowsetReader(std::static_pointer_cast<BetaRowset>(shared_from_this())));
    return Status::OK();
}
```
遍历所有的 tablet，为每个 rowset 创建一个 rowset reader，
在当前的例子里，一共 4 个 tablet，每个 tablet 有 3 个 rowset（两次写入 + 一次 compaction），那么就会创建 12 个 RowsetReaderSharedPtr。

`_build_scanners_by_rowid` 是按照行数来决定创建多少个 scanner，
```plantuml
struct ReadSource {
    + rs_splits : std::vector<RowSetSplits>
    + delete_predicates : std::vector<RowsetMetaSharedPtr>
    + fill_delete_predicates() : void
}

struct RowSetSplits {
    + rs_reader : RowsetReaderSharedPtr
    + segment_offsets : std::pair<int, int>
    + segment_row_ranges : std::vector<RowRanges> 
}

class RowRanges {
    - _ranges : std::vector<RowRange>
    - _count : size_t
}

class RowRange {
    - _from : int64_t
    - _to : int64_t
}

ReadSource *-- RowSetSplits
RowSetSplits *-- RowRanges
RowRanges *-- RowRange
```



```cpp
scanners.emplace_back(_build_scanner(tablet, version, _key_ranges,
                                               {std::move(read_source.rs_splits),
                                                reade_source_with_delete_info.delete_predicates}));
```
对于每个scanner，他的数据成员记录了他应该读 tablet A 的 rowset id B 的 segment C 的行 `[n, m]`






```cpp
Status BetaRowsetReader::next_block(vectorized::Block* block) {
    SCOPED_RAW_TIMER(&_stats->block_fetch_ns);
    RETURN_IF_ERROR(_init_iterator_once());

    do {
        auto s = _iterator->next_batch(block);
        if (!s.ok()) {
            if (!s.is<END_OF_FILE>()) {
                LOG(WARNING) << "failed to read next block: " << s.to_string();
            }
            return s;
        }
    } while (block->empty());
}
```
在第一次从 RowsetReader 中读 block 时，会创建需要的 segment_iterators
每个 tablet，有 3 个 rowset，所以有 3 个 rowset version，那么首先为了 delete 会创建 3 个 RowSetSplit，记录到 Tablet 的 ReadSource 中。
```cpp
struct RowSetSplits {
    RowsetReaderSharedPtr rs_reader;

    // if segment_offsets is not empty, means we only scan
    // [pair.first, pair.second) segment in rs_reader, only effective in dup key
    // and pipeline
    std::pair<int, int> segment_offsets;

    // RowRanges of each segment.
    std::vector<RowRanges> segment_row_ranges;

    RowSetSplits(RowsetReaderSharedPtr rs_reader_)
            : rs_reader(rs_reader_), segment_offsets({0, 0}) {}
    RowSetSplits() = default;
};

struct ReadSource {
        std::vector<RowSetSplits> rs_splits;
        std::vector<RowsetMetaSharedPtr> delete_predicates;
        // Fill delete predicates with `rs_splits`
        void fill_delete_predicates();
    };
```
`fill_delete_predicates()` 会把 rs_splits 中的所有 RowSetSplit 的 delete_predicates 塞到 delete_predicates 中。

这 3 个 rowset 中，只有最新版本的 rowset 中有数据，所以会再创建一个 RowSetSplit。

最后会把用于读 Segment 的 RowSetSplits 和 用于 Delelte 的 delete_predicates 拼在一起，得到一个新的 ReadSource。这个 ReadSource 会被 move 给 OlapScanner。

```cpp
NewOlapScanner::NewOlapScanner(pipeline::ScanLocalStateBase* parent,
                               NewOlapScanner::Params&& params)
        : VScanner(params.state, parent, params.limit, params.profile),
          _key_ranges(std::move(params.key_ranges)),
          _tablet_reader_params({
                  .tablet = std::move(params.tablet),
                  .aggregation = params.aggregation,
                  .version = {0, params.version},
          }) {
    _tablet_reader_params.set_read_source(std::move(params.read_source));
    _is_init = false;
}
```
4 个 tablet, 有 6 个 rowset 中的行数不为 0， 共有 6 个有效的 segment， 创建了 4 个 scanner，共 6 个 rowset reader。

#### Scanner::init
```cpp
Status NewOlapScanner::init() {
    ...
    _tablet_reader = std::make_unique<BlockReader>();
}
```
#### Scanner::open
```cpp
Status NewOlapScanner::open(RuntimeState* state) {
    RETURN_IF_ERROR(VScanner::open(state));

    auto res = _tablet_reader->init(_tablet_reader_params);
    ...
}

Status BlockReader::init(const ReaderParams& read_params) {
    RETURN_IF_ERROR(TabletReader::init(read_params));
    // {
    //     Status TabletReader::init(const ReaderParams& read_params) {
    //         _predicate_arena = std::make_unique<vectorized::Arena>();

    //         Status res = _init_params(read_params);
    //         ...
    //     }

    //     Status TabletReader::_init_params(const ReaderParams& read_params) {
    //         ...
    //         Status res = _init_delete_condition(read_params);
    //         ...
    //     }

    //     Status TabletReader::_init_delete_condition(const ReaderParams& read_params) {
    //         ...
    //         return _delete_handler.init(_tablet_schema, read_params.delete_predicates,
    //                                     read_params.version.second, enable_sub_pred_v2);    
    //     }
    // }

    auto status = _init_collect_iter(read_params);

}


```

#### ScannerContext::get_free_block
#### Scanner::get_block_after_projects


#### get next

上述 prepare 阶段完成后，PiplineTask 开始 get next_batch

```cpp
template <typename LocalStateType>
Status ScanOperatorX<LocalStateType>::get_block(RuntimeState* state, vectorized::Block* block,
                                                bool* eos) {
    ...
    RETURN_IF_ERROR(local_state._scanner_ctx->get_block_from_queue(state, block, eos, 0));
    ...
}

Status ScannerContext::get_block_from_queue(RuntimeState* state, vectorized::Block* block,
                                            bool* eos, int id) {
    ...
    if (!_blocks_queue.empty() && !done()) {
        
    }
}
```

```cpp
Status NewOlapScanner::_get_block_impl(RuntimeState* state, Block* block, bool* eof) {
    // Read one block from block reader
    // ATTN: Here we need to let the _get_block_impl method guarantee the semantics of the interface,
    // that is, eof can be set to true only when the returned block is empty.
    RETURN_IF_ERROR(_tablet_reader->next_block_with_aggregation(block, eof));
    if (!_profile_updated) {
        _profile_updated = _tablet_reader->update_profile(_profile);
    }
    if (block->rows() > 0) {
        _tablet_reader_params.tablet->read_block_count.fetch_add(1, std::memory_order_relaxed);
        *eof = false;
    }
    _update_realtime_counters();
    return Status::OK();
}

Status BlockReader::next_block_with_aggregation(Block* block, bool* eof) {
    auto res = (this->*_next_block_func)(block, eof);
    return res;
}

Status BlockReader::init(const ReaderParams& read_params) {
    RETURN_IF_ERROR(TabletReader::init(read_params));
    ...
    if (_direct_mode) {
        _next_block_func = &BlockReader::_direct_next_block;
        return Status::OK();
    }

    switch (tablet()->keys_type()) {
        case KeysType::DUP_KEYS:
            _next_block_func = &BlockReader::_direct_next_block;
            break;
        ...
    }
}

Status BlockReader::_direct_next_block(Block* block, bool* eof) {
    auto res = _vcollect_iter.next(block);
    if (UNLIKELY(!res.ok() && !res.is<END_OF_FILE>())) {
        return res;
    }
    *eof = res.is<END_OF_FILE>();
    _eof = *eof;
    ...    
}
```