
<!-- @import "[TOC]" {cmd="toc" depthFrom=1 depthTo=6 orderedList=false} -->

<!-- code_chunk_output -->

- [15.5 Join Operation](#155-join-operation)
  - [15.5.1 Nested-Loop Join](#1551-nested-loop-join)
  - [15.5.2 Block Nested-Loop Join](#1552-block-nested-loop-join)
  - [15.5.3 Indexed Nested-Loop Join](#1553-indexed-nested-loop-join)
  - [15.5.4 Merge Join](#1554-merge-join)
  - [15.5.5 Hash Join](#1555-hash-join)
  - [15.5.6 Complex Joins](#1556-complex-joins)
  - [15.5.7 Joins over Spatial Data](#1557-joins-over-spatial-data)
- [15.7 Evaluation of Expression](#157-evaluation-of-expression)
  - [15.7.1 Materialization](#1571-materialization)
  - [15.7.2 Pipeline](#1572-pipeline)

<!-- /code_chunk_output -->



## 15.5 Join Operation

equi-join 

student $\Join$ takes

* Number of records of student: $n_{student}$ = 5000;
* Number of blocks of student: $b_{student}$ = 100;
* Number of records of takes: $n_{takes}$ = 10,000;
* Number of blocks of takes: $b_{takes}$ = 400;


### 15.5.1 Nested-Loop Join
```txt
for each tuple tr in r do:
    for each tuple ts in s do:
        test pair(tr, ts) to see if they satisfy the join condition
        if they do, add tr * ts to the result
    end
end
```
NLJ requires no indices.
NLJ 可以被扩展为 natrual join，只需要在结果中删除重复的属性。


Cost
Total tuples: $n_{r} * n_{s}$.

Worst case: We can only hold one block for each table.
**For each record in r, we have to do a full scan on s**. So, we have $b_{r} + n_{r} * b_{s}$ block transforms in total. For inner table s, we only have one seek since it is scanned sequentially, and a total of $b_{r}$ seek for outer table r, which leading to $b_{r} + n_{r}$ seeks in total.

Most probable best case: One of the relations fits entirely in the memory.
We should make that table a inner table, so that we just need to do 1 + 1 seek for table r and s (we have page cache and memory processing is much faster than reading from disk, so that we can read blocks from table r sequentially), and $b_{r} + b_{s}$ blocks to transform in total.
Consider using that table a outer table. We have still $b_{r} + b_{s}$ blocks to transform. After 1 seek to read table r, we need $n_{r}$ times seek for continue processing, leading to $1 + n_{r}$ seeks in total.

For student $\Join$ takes, assume we have no indices on either relation.
Using student as outer relation, we have $n_{s} * n_{t} = 5000 * 10,000 = 50 * 10^{6}$ pairs of tuples to processing. In the worst case, number of block transfers is $b_{student} + n_{student} * b_{takes} = 100 * 5000 * 400 = 200100$ plus $b_{student} + n_{student} = 5000 + 100 = 5100$ seeks. In best cases, both relation can be read in memory, so we have $100 + 400$ blocks to transfer, and 2 seeks.
If we use takes as outer relation. In worst cases, we need to transfor $b_{takes} + n_{takes} * b_{student} = 400 +  10000 * 100 = 100400$ blocks plus $b_{takes} + n_{takes} = 10000 + 400 = 10400$ seeks.

|outer relation|worst|best|
|--|--|--|
|student|200100 block transfer and 5100 seeks|500 block transfer and 2 seek|
|takes|100400 block transfer and 10400 seeks|500 block transfer and 2 seek|

通常一次 seek 花费的时间是 tranfer 一个 block 的 40 倍。 $Time_{seek} \simeq 40 * Time_{transfer}$，估算两种计算方式的相对开销：
`2 * 1 + 1/20 * 40 = 2 + 2 = 4`
`1 * 1 + 1/10 * 40 = 1 + 4 = 5`

### 15.5.2 Block Nested-Loop Join
15.5.1 中的算法是针对行的：每次对 outer table 的一行进行计算，因此一个明显的优化是改成基于 block 的计算。
```txt
for each block Br of d:
    for each Block Bs of s:
        for each Tuple Tr of Br:
            for each Tuple Ts of Bs:
                test pair(Tr, Ts) to see if they satisfy the join condition
                if they do, add Tr * Ts to the result
            end
        end
    end
end
```

### 15.5.3 Indexed Nested-Loop Join
The cost formula indicates that, if indices are available on both relations r and s, **it is generally most efficient to use the one with fewer tuples as the outer relation.**

### 15.5.4 Merge Join

### 15.5.5 Hash Join
Basic iead: partition the tuples of each of the relations into sets that have the same hash value on the join attributes.

![Alt text](image.png)

After the partition of the relations, the rest of code performs a separate indexed nested-loop join on each of the partitions pairs. To do so, it first builds a hash index on each $s_{i}$, and then probes (that is, looks up $s_{i}$) with tuples from $r_{i}$. The relation s is the build input, and r is the probe input.

The hash function used to build this hash index must be different from the hash function h used earlier, but it is still applied to only the join attributes.
```text
/*Partition s*/
for each tuple ts in s:
    i := h(ts[JoinAttrs])
    H_si := H_si and {ts}
end
/*Partition r*/
for each tuple tr in r:
    i := h(tr[JoinAttrs])
    H_ri := H_ri and {tr}
end

/*Perform join on each partition*/
for i:= 0 to n_h:
    read H_si and build an in-memory hash index on it;
    for each tuple tr in H_ri do
        probe the hash index of H_si to locate all tuples ts
            such that ts[JoinAttrs] = tr[JoinAttrs];
        for each mathcing tuple ts in H_si do
            add ts * ts to result
        end
    end
end
```

**It is best to use the smaller input relation as the build relation.**

### 15.5.6 Complex Joins
### 15.5.7 Joins over Spatial Data


## 15.7 Evaluation of Expression
### 15.7.1 Materialization
### 15.7.2 Pipeline
We can improve query-evaluation efficiency by reducing the number of temporary files that are produced. We achieve this reduction by combining several relational operations into a pipeline of operations, in which the results of one operation are passed along to the next operation in the pipeline. Evaluation as just described is called pipelined evaluation.