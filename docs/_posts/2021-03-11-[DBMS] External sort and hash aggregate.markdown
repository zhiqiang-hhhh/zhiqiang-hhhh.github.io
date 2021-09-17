---
layout: post
title:  "[DBMS] External sort and hash aggregate"
date:   2021-03-11 19:20:11 +0800
categories: jekyll update
---
[TOC]
# External sort 

本文介绍DBMS实现时常用的外部排序算法，以及如何进行聚合计算。

## External sort
外部排序即面向磁盘的排序算法，面向磁盘的排序算法性能度量核心是磁盘IO次数。
外部排序算法是一种基于分治思想的算法，由两个阶段组成：
1. Sorting：将数据划分为可以被放进内存的文件块，然后利用内部排序算法对其排序，再将文件块写回磁盘，被写回的文件块被称为 run；
2. Merge：将排好序的segments合并成一个大文件。

### Two-way Merge Sort
最简单的外部排序方法。Two-way 是指在 Merge 阶段，该算法共使用三“块”内存（3 buffer pool pages），使用两个 page 作为读入 run（单个 run 中的数据已经排好序），然后将这两个 page 合并的结果写入第三个 page，用作保存 Merge 的临时结果，当这块 page 满了以后，其内容被写入磁盘，然后这块 page 被清空。
迭代这一过程，直到全部数据都被排好序。

假设待 Merge 的文件一共 N pages，那么第一趟（pass）merge 之后我们将会剩下 N / 2 个 pages，因此 Merge 阶段一共需要 <a href="https://www.codecogs.com/eqnedit.php?latex=\dpi{100}&space;\lceil&space;Log_{2}{N}&space;\rceil" target="_blank"><img src="https://latex.codecogs.com/png.latex?\dpi{100}&space;\lceil&space;Log_{2}{N}&space;\rceil" title="\lceil Log_{2}{N} \rceil" /></a>趟。Sorting 阶段累计读入 N 个 pages 然后写回 N 个pages，可以算是单独的一趟。每趟都需要 2N 次IO，因此两路归并排序一共需要<a href="https://www.codecogs.com/eqnedit.php?latex=\dpi{100}&space;2*(1&plus;\lceil&space;Log_{2}{N}&space;\rceil)" target="_blank"><img src="https://latex.codecogs.com/png.latex?\dpi{100}&space;2*(1&plus;\lceil&space;Log_{2}{N}&space;\rceil)" title="2*(1+\lceil Log_{2}{N} \rceil)" /></a>次 IO。

### General K-way Merge Sort
假设我们有更多的buffer pool pages，总数为 B。那么在sorting阶段，我们可以一次读取 B 个 pages，假设带排序文件依然是 N 个 pages，那么在 sorting 阶段结束后，可以得到 <a href="https://www.codecogs.com/eqnedit.php?latex=\dpi{100}&space;2N*(1&plus;Log_{2}{\lceil&space;N&space;\rceil})" target="_blank"><img src="https://latex.codecogs.com/png.latex?\dpi{100}&space;2N*(1&plus;Log_{2}{\lceil&space;N&space;\rceil})" title="2N*(1+Log_{2}{\lceil N \rceil})" /></a> 个 runs。

在 merge 阶段，依然使用 1 个 buffer pool page 用作输出，那么我们可以同时对 B-1 个 runs 进行 merge。所以整个 K 路归并会有 <a href="https://www.codecogs.com/eqnedit.php?latex=\dpi{100}&space;1&plus;(Log_{B-1}{\lceil&space;\frac{N}{B}&space;\rceil})" target="_blank"><img src="https://latex.codecogs.com/png.latex?\dpi{100}&space;1&plus;(Log_{B-1}{\lceil&space;\frac{N}{B}&space;\rceil})" title="1+(Log_{B-1}{\lceil \frac{N}{B} \rceil})" /></a> 趟（1 for sorting），所以总的 IO 次数为 <a href="https://www.codecogs.com/eqnedit.php?latex=2N*(1&plus;Log_{B-1}{\lceil&space;\frac{N}{B}&space;\rceil})" target="_blank"><img src="https://latex.codecogs.com/gif.latex?2N*(1&plus;Log_{B-1}{\lceil&space;\frac{N}{B}&space;\rceil})" title="2N*(1+Log_{B-1}{\lceil \frac{N}{B} \rceil})" /></a> 次。

### Double Buffering Optimization
外部排序的一个常见优化是使用另一个 buffer 来保存预先读取的 page，这样当前 page 排序或者归并之后就不需要等待读取下一个 page。double buffering 需要另一个线程进行预读。

## Aggregate
聚合操作通常有两种实现方式，一种是基于Sorting的，另一种是基于Hash的。
比如有如下的SQL语句
```SQL
SELECT DISTINCT cid
    FROM enrolled
    WHERE grade in ('B', 'C')
    ORDER BY cid
```
DBMS通常会采用如下pipeline来完成查询：
首先使用filter遍历所有tuple，遍历结束后只剩下grade等于B和C的tuple，然后remove除了cid之外的其他列值(假设底层是行存)，这时得到的结果是无序的，那么再用之前说的外部排序对cid列进行排序，最后顺序遍历陪排序过的所有tuple，消除重复。

什么时候应该选择基于sorting的聚合过程：当我们需要结果按照某个属性被排序时。

假如执行的聚合操作不需要排序，比如是`GROUP BY`或者`DISTINCT`子句，那么使用基于Hash的聚合过程会更高效。

### Hashing Aggregate
```sql
SELECT DISTINCT cid
    FROM enrolled
    WHERE grade in ('B', 'C')
```
假如数据能够全部放入内存，那么hashing aggregate的过程就很容易想到：
1. 遍历tuples，留下所有满足where条件的tuples
2. remove columns，只剩下 cid 列
3. apply hash function on every cid value，得到一个hash table
4. 返回 hash table 中的所有元素

以上过程都是在内存中执行的。

现在假设**数据不能完全放进内存**。很明显，步骤1和步骤2依然是有必要的，只不过中间结果现在需要被保存在磁盘上。我们需要对第3步进行改造。

假设我们给buffer pool一共分配了B个内存块（每个内存块的大小并不一定就是一个磁盘page的大小）。与external sorting不太一样的一点是，我们需要留1个buffer pool page(bpp)用于保存输入数据（external sorting中留一个用于保存输出）。那么可用于保存hash结果的内存块为B-1个，所以设计hash函数的桶数量为B-1，我们对所有的输入tuple应用该hash函数，将tuple分配至B-1个partition内，partition是逻辑概念，对应一个bpp。输入bpp不断读入tuple，每当某个输出bpp满了以后，就将其全部内存flush到磁盘，并清空该bpp以迎接后续tuple。重复该过程直到所有tuple都被分入到某个partition内。

以上过程是hashing aggregate的**第一阶段，称为partition过程**。
![picture 1](../../images/75e15e31dd37c40d5b728c17864711fa5dcc9373fad35333b5fbee26b5ae8761.png)  


partition过程结束后我们得到了B-1个逻辑partition，每个partition实际上可能会对应一个或者多个disk page。同时，考虑到B-1很可能是小于distinct tuple个数的，那么每个partition内都有可能具有多个不同的cid tuple。我们还需要**第二阶段rehash**继续进行处理。

Partition阶段的一个重要保证是所有相同的cid都在同一个partition内。第二阶段rehash时，我们对所有partition应用另一个hash函数，得到一个临时hash表，该临时hash表内保存了该partition中的所有不同cid。当我们为所有partition都得到一个临时hash表后，我们需要做的就是将所有临时hash表合并，这样就得到了最终结果。
![picture 2](../../images/35cbf8184489f291cf2d4e7d1032c9a1895f9a1020db8ec9644379013bc31fbd.png)  


临时hash表的内容通常是`(GroupByKey -> RunningValue)`的形式，`Runningvalue`内容取决于具体的aggregate function。假如我们要计算的是count函数，那么`Runningvalue`就是counts，每当一个cid hits hash table，那么就将对应的 counts++；否则就在hash table中新增加一行。

# Practice
假设database文件一共有8000000个pages，并且没有使用double buffering等优化。

1. Assume that the DBMS has four buffers. How many passes does the DBMS need to perform in order to sort the file?
1 + 14 = 15
2. Again, assuming that the DBMS has four buffers. What is the total I/O cost to sort the file?
15 * 2 * 8000000 = 24000000
3. What is the smallest number of buffers B that the DBMS can sort the target file using only two passes? 
2829
4. What is the smallest number of buffers B that the DBMS can sort the target file using only five passes?
5. Suppose the DBMS has twenty buffers. What is the largest database file (expressed in terms of N, the number of pages) that can be sorted with external merge sort using 6 passes?