## Doris 排序键查询优化的代码实现

Doris 通过排序键优化查询性能主要包括以下几个核心机制：

### 1. Short Key Index（前缀索引）

**代码位置**: short_key_index.h

```cpp
// ShortKeyIndexDecoder 负责解码 Short Key 索引
// 支持 lower_bound 和 upper_bound 操作，快速定位数据块
ShortKeyIndexIterator lower_bound(const Slice& key) const;
ShortKeyIndexIterator upper_bound(const Slice& key) const;
```

**使用逻辑**: segment_iterator.cpp

```cpp
// 通过 Short Key Index 查找数据的 ordinal 位置
// 1. 先用 lower_bound/upper_bound 定位到索引块
// 2. 再用二分查找找到精确位置
Status SegmentIterator::_lookup_ordinal_from_sk_index(const RowCursor& key, bool is_include,
                                                      rowid_t upper_bound, rowid_t* rowid) {
    const ShortKeyIndexDecoder* sk_index_decoder = _segment->get_short_key_index();
    // ...
    auto start_iter = sk_index_decoder->lower_bound(index_key);
    // 二分查找精确定位
    while (start < end) {
        rowid_t mid = (start + end) / 2;
        RETURN_IF_ERROR(_seek_and_peek(mid));
        int cmp = _compare_short_key_with_seek_block(key_col_ids);
        // ...
    }
}
```

### 2. Zone Map 索引剪枝

**代码位置**: column_reader.cpp

Zone Map 存储每个数据页的 min/max 值，用于快速跳过不满足条件的数据页：

```cpp
Status ColumnReader::_get_filtered_pages(
        const AndBlockColumnPredicate* col_predicates,
        const std::vector<std::shared_ptr<const ColumnPredicate>>* delete_predicates,
        std::vector<uint32_t>* page_indexes, const ColumnIteratorOptions& iter_opts) {
    const std::vector<ZoneMapPB>& zone_maps = _zone_map_index->page_zone_maps();
    for (size_t i = 0; i < page_size; ++i) {
        RETURN_IF_ERROR(_parse_zone_map(zone_maps[i], min_value.get(), max_value.get()));
        // 判断谓词是否可能匹配该页的 [min, max] 区间
        if (_zone_map_match_condition(zone_maps[i], min_value.get(), max_value.get(), col_predicates)) {
            page_indexes->push_back(i);  // 只读取可能匹配的页
        }
    }
}
```

**调用链**: segment_iterator.cpp

```cpp
// 在 _get_row_ranges_from_conditions 中使用 Zone Map 过滤
// second filter data by zone map
for (const auto& cid : cids) {
    RETURN_IF_ERROR(_column_iterators[cid]->get_row_ranges_by_zone_map(
            _opts.col_id_to_predicates.at(cid).get(), ...));
    // intersect different columns's row ranges to get final row ranges by zone map
    RowRanges::ranges_intersection(zone_map_row_ranges, column_row_ranges, &zone_map_row_ranges);
}
```

### 3. TopN 查询优化 (利用存储层排序)

**FE 端判断逻辑**: PhysicalPlanTranslator.java

当 ORDER BY 列是排序键的前缀时，可以利用存储层已排序的特性：

```java
/**
 * topN opt: using storage data ordering to accelerate topn operation.
 * refer pr: optimize topn query if order by columns is prefix of sort keys of table (#10694)
 */
private boolean checkPushSort(SortNode sortNode, OlapTable olapTable) {
    // Ensure limit is less than threshold
    if (sortNode.getLimit() <= 0
            || sortNode.getLimit() > context.getSessionVariable().topnOptLimitThreshold) {
        return false;
    }
    // Tablet's order by key only can be the front part of schema.
    // Do **prefix match** to check if order by key can be pushed down.
    // olap order by key: a.b.c.d
    // sort key: (a) (a,b) (a,b,c) (a,b,c,d) is ok
    //           (a,c) (a,c,d), (a,c,b) is NOT ok
    for (int i = 0; i < sortExprs.size(); i++) {
        Column sortColumn = sortKeyColumns.get(i);
        // 检查 ORDER BY 列是否与表的排序键前缀匹配
        if (sortExpr instanceof SlotRef) {
            if (!sortColumn.equals(slotRef.getColumn())) {
                return false;
            }
        }
    }
    return true;
}
```

**BE 端 TopN 优化**: segment_iterator.cpp

```cpp
bool SegmentIterator::_can_opt_topn_reads() {
    if (_opts.topn_limit <= 0) {
        return false;
    }
    // 检查所有列是否都通过了倒排索引的条件
    bool all_true = std::ranges::all_of(_schema->column_ids(), [this](auto cid) {
        if (_check_all_conditions_passed_inverted_index_for_column(cid, true)) {
            return true;
        }
        return false;
    });
    return all_true;
}
```

使用时限制读取行数：
```cpp
if (_can_opt_topn_reads()) {
    nrows_read_limit = std::min(static_cast<uint32_t>(_opts.topn_limit), nrows_read_limit);
}
```

### 4. Defer Materialize (延迟物化)

**代码位置**: DeferMaterializeTopNResult.java

对于 TopN 查询，只需要先读取排序列和 rowid，排序后再根据 rowid 物化其他列：

```java
// 延迟物化只读取排序相关的列，过滤后再读取其他列
LogicalDeferMaterializeOlapScan deferOlapScan = new LogicalDeferMaterializeOlapScan(
        logicalOlapScan, deferredMaterializedExprIds, columnId);
```

### 5. Bloom Filter 索引

**代码位置**: column_reader.cpp

```cpp
Status ColumnReader::get_row_ranges_by_bloom_filter(const AndBlockColumnPredicate* col_predicates,
                                                    RowRanges* row_ranges, ...) {
    for (auto& pid : page_ids) {
        std::unique_ptr<BloomFilter> bf;
        RETURN_IF_ERROR(bf_iter->read_bloom_filter(pid, &bf));
        // 用 Bloom Filter 过滤不可能包含目标值的数据页
        if (col_predicates->evaluate_and(bf.get())) {
            bf_row_ranges.add(RowRange(...));
        }
    }
    RowRanges::ranges_intersection(*row_ranges, bf_row_ranges, row_ranges);
}
```

---

### 总结

| 优化机制 | 作用 | 关键代码 |
|---------|-----|---------|
| **Short Key Index** | 快速定位满足排序键条件的数据行范围 | `_lookup_ordinal_from_sk_index` |
| **Zone Map** | 跳过不满足条件的数据页 | `get_row_ranges_by_zone_map` |
| **TopN Push Down** | ORDER BY 是排序键前缀时直接读取已排序数据 | `checkPushSort` |
| **Defer Materialize** | 延迟物化非排序列，减少 I/O | `DeferMaterializeTopNResult` |
| **Bloom Filter** | 过滤不包含目标值的数据页 | `get_row_ranges_by_bloom_filter` |


### IndexPage

```plantuml
PagePointer
```

### KeyRange
KeyRange 是 doris 在 segment iterator 中用来描述可以被 short key index 加速的谓词的数据结构。
```cpp
struct KeyRange {
    // the lower bound of the range, nullptr if not existed
    const RowCursor* lower_key = nullptr;
    // whether `lower_key` is included in the range
    bool include_lower;
    // the upper bound of the range, nullptr if not existed
    const RowCursor* upper_key = nullptr;
    // whether `upper_key` is included in the range
    bool include_upper;
}
```


```sql
CREATE TABLE `debug_pk_comp` (
  `a` int NULL,
  `b` int NULL,
  `val` int NULL
) ENGINE=OLAP
DUPLICATE KEY(`a`, `b`)
DISTRIBUTED BY HASH(`a`) BUCKETS 1
PROPERTIES (
"replication_allocation" = "tag.location.default: 1");

SELECT * FROM debug_pk_comp WHERE a = 2 AND b > 1 ORDER BY a,b;
```
谓词对应的 key range 表示为
```text
lower_key: 0&2|0&1, include_lower=0
upper_key: 0&2|0&2147483647, include_upper=1
```
