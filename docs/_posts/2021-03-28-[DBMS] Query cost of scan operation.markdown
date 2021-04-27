---
layout: post
title:  "[DBMS] Query cost of scan operation"
date:   2021-03-29 16:51:11 +0800
categories: jekyll update
---
[TOC]
## Measures of Query Cost
Two factors：
1. The number of blocks transferred from storage.
2. The number of random I/O.


Supposing that average time of transferring a block of data from storage
is $t_{T}$, and average block-access time (disk seek + rotational latenct) is $t_{S}$, then an operation that transfers B blocks and performs S random IO accesses would take $b ∗ t_{T} + S ∗ t_{S}$seconds.

Database systems must ideally perform test seeks and block transfers to estimate $t_{S}$ and $t_{T}$ for specific systems/storage devices, as part of the software installation process. Databases that do
not automatically infer these numbers often allow users to specify the numbers as part of configuration files.

Main memory matters for a specific query. In fact, a good deal of main memory is typically available for processing a query, and our cost estimates use the amount of memory available to an operator, M, as a parameter.

The response time for query is hard to estimate until we actually executing the plan. RT depends on the contents of the buffer and the data distribution among disks.

### Selection Operation
Assumption:
1. $b_{r}$ file blocks to transfer from magnetic storage 
2. Average height of b+-tree is $h_{i}$
3. Every nodes of b+-tree needs a random IO operation


* Linear search. 
DBMS scans each file block and tests all records to see whether they meet the predication. One initial seek takes $t_{S}$ seconds, and takes $b_{r}*t_{T}$ seconds for subsequent data block transferring. So overall cost is $t_{S} + b_{r}*t_{T}$

* Clustering B+-tree Index, Equality on Key
For each targe key, DBMS traverses the B+-tree to find out whether it exists in database. The seek operation of traversing B+-tree takes $(h_{i})* t_{S}$ seconds, and each fetch operation needs one seek and transfers one data block, so overall time cost is $(1 + h_{i})*(t_{S}+t_{T})$

* Clustering B+-tree Index, Equality on Non-key
Equality on non-key means index is built on a column which could have duplicate records. We would fetch multiple records, and this is the only defference with previous case. Supposing number of blocks containing records with specific search non-key is $b$, we need first to traverse b+-tree index, and then do one seek operation followed by $b$ data block transfering. So total cost is $h_{i}*(t_{T}+t_{S})+t_{S}+b*t_{T}$

* Secondary B+-tree Index, Equality on key
Secondary index means physical sequence of key in database file is different from key sequence in leaf node of b+-tree index. In the case of equality on key, this difference does not matter, because we just need to fetch one record from database file. So total cost is same whis case 2. $(h_{i} + 1)*(t_{S} + t_{T})$

* Secondary B+-tree Index, Equality on non-key
Difference of record layout in database file between non-clustering index and clustering index matters in this situation. Because the records with quality on non-key may reside on multiple blocks, we need to do extral seek & transfer operations. Supposing we have $n$ records to be fetched, the total time cost is $(h_{i}+n)*(t_{T} + t+{S})$

* Clustering B+tree Index, Comparison
Selection of the form: $\sigma_{A\leq{V}}(r)$. For clustering b+tree index, it's easy to implement. First, look up the left most tuple that satisfies the comparison; then, linear search to find resting tuples. The cost estimate is identical to clustering b+-tree index with equality on non-key. $h_{i}*(t_{T}+t_{S}) + t_{S} + b*t_{T}$

* Secondary B+tree Index, Comparison
The secondary index provides pointers to the records, but to get the actual records we have to fetch the records by using the pointers. This step may require an I/O operation for each record fetched, since consecutive records may be on different disk blocks; as before, each I/O operation requires a disk seek and a block transfer. If the number of retrieved records is large, using the secondary index may be even more expensive than using linear search. Therefore, the secondary index should be used only if very few records are selected. Total cost: $h_{i}*(t_{T}+t_{S}) + n*(t_{T}+t_{S})$. 这里假设只需要search一个leaf node.

可以看到，对于Selection with comparison on secondary b+tree index 的情景，总的时间花费取决于我们最终获取了多少条记录。如果记录较多，那么使用secondary index的效率反而会比linear search的效率更低（极端情况下所有block均需要访问，那么此时访问secondary index就是在浪费时间）。
对于这种情况，PostgreSQL中使用了一种混和算法：bitmap index scan。该算法会创建一个bitmap，bit的位数等于database file中block的数量，所有的bit都被初始化为0。Selection过程中使用secondary index找到matching tuple的所有index entries，每当找到一个满足comparison的entry，那么就会把db file中block number对应的bitmap中的bit设置为1。bitmap建立完成后，selection过程按照linear search遍历db file，**跳过在bitmap中对应为0的block**。这样最差情况（fetch所有记录）下只会比linear search多一点点访问bitmap的开销，但是在最好情况下会比linear search好很多。相比使用secondary index，最差情况下（只fetch一条记录），bitmap index只比使用secondary index差一点点（构建bitmap，记录位于database file最后);最好情况下（fetch所有记录），bitmap index好很多。

||Algorithm|Cost|Reason|
|--|--|--|--|
|A1|Linear search|$t_{S} + b_{r}*t_{T}$||
|A2|Clustering B+-tree Index, Equality on Key|$(1 + h_{i})*(t_{S}+t_{T})$||
|A3|Clustering B+-tree Index, Equality on Non-key|$h_{i}*(t_{T}+t_{S})+t_{S}+b*t_{T}$||
|A4|Secondary B+-tree Index, Equality on key|$(h_{i} + 1)*(t_{S} + t_{T})$||
|A5|Secondary B+-tree Index, Equality on Non-key|$(h_{i}+n)*(t_{T} + t+{S})$||
|A6|Clustering B+-tree Index, Comparision|$h_{i}*(t_{T}+t_{S}) + t_{S} + b*t_{T}$||
|A7|Secondary B+tree Index, Comparison|$h_{i}*(t_{T}+t_{S}) + n*(t_{T}+t_{S})$||

Comparision与Equality对于获取数据的区别在于前者往往会返回一个范围内的数据，后者通常是返回一条数据。

对于在聚集索引上的selection，不论是equality还是comparision条件，相比linear search总是开销更少；对于在secondary index上的selection，其开销取决于selectivity。
### Complex Selection
* Conjunctive selection：多个simple selection的并
* Disjunctive selection：多个simple selection的或
* Negation: 取反