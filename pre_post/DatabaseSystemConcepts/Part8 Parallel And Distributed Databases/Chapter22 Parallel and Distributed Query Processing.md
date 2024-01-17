# Parallel and Distributed Query Processing
## Overview
* Interquery parallelism

    Executon of multiple queries in parallel with each other, across multiple nodes. Interquery parallelism 对于事务处理系统非常重要。interquery processing 不会使得单个事务的响应时间比其单独执行时更快。

* Intraquery parallelism

    The processing of different part of a single query, in parallel across multiple nodes. Intraquery parallelism 用于加速长时间运行的查询。


单个查询的执行会涉及到多个 operation，比如 select，joins 或者 aggregate operation。

* Intraoperation parallelism

    Process each operation above parallel, across multiple nodes.

比如有个查询要求对 table 排序，假设数据已经在多个磁盘上按照某个属性进行了 range 分区，并且 order by 字段正是在这个属性上，那么我们可以把 sort operation 实现成：1. 并行对每个 partition 排序；2. 对最终结果进行 concatenation

* Interoperation parallelism

    Parallelize the evaluation of the operator tree by evaluating in parallel some of the operations that do not depend on one another.

## Parallel Sort
Suppose that we wish to sort a relation r that resides on n nodes N1, N2, ... , Nn. If the relation has been range-partitioned on the attributes on which it is to be sorted, we can sort each partition separately and concatenate the results to get the full sorted relation. Since the tuples are partitioned on n nodes, the time required for reading the entire relation is reduced by a factor of n by the parallel access.

如果数据本身不是按照 order by 的字段进行分区的，那么我们有两种方式实现全局的排序：

1. 根据排序字段，对数据进行 range partition，把不同的 range 发送到不同的 node，在不同的 node 完成对 range 内数据排序后，将结果组合成最终的顺序
2. 使用一个多节点并行版本的 merge sort

### Range-partitioning sort
关键：能够在多个 node 上均匀地进行 range partition —— Virtual node partition。

### Parallel External Sort-Merge
假设数据已经在多个 node 之间分区，那么 Parallel external sort-merge 的步骤是：
1. 每个 node 在本地基于 order by 目标列对其数据进行排序
2. 系统对多个 node 上已经排序好的多个 partition 进行并行排序

其中第二步可以用如下方式进行并行实现：
1. 对每个 node 上的数据进行 range-partition，每个 node 都会把 Range [i, j) 发送给 Node i
2. 每个 Node 都收到的是一组排好序的 stream，那么每个 node 就将其收到的数据进行 merge
3. The system concatenates the sorted runs on nodes N1, N2, ... , Nm to get the final result.

## Parallel Join

