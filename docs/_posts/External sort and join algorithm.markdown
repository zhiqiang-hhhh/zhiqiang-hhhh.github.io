---
layout: post
title:  "External sort and join algorithm"
date:   2021-03-11 19:20:11 +0800
categories: jekyll update
---
# External sort and join algorithm

本文介绍DBMS实现时常用的两个算法。外部排序算法，以及Join算法。

## External sort
外部排序即面向磁盘的排序算法，面向磁盘的排序算法性能度量核心是磁盘IO次数。
外部排序算法是一种基于分治思想的算法，由两个阶段组成：
1. Sorting：将文件划分为可以被放进内存的文件块，然后利用内部排序算法对其排序，再将文件块写回磁盘，被写回的文件块被称为 run；
2. Merge：将排好序的segments合并成一个大文件。

### Two-way Merge Sort
最简单的方法是两路归并排序。Two-way是指在归并阶段，该算法使用三“块”内存（buffer pool大小为3），同时读两个page，然后将这两个page合并的结果写入另一块用作保存输出的内存，当这块输出内存满了以后，其内容被写入磁盘，然后这块内存被清空，继续迭代这一过程，直到全部数据都被排好序。

假设待排序的文件一共 N pages，那么第一趟（pass）merge 之后我们将会剩下 N / 2 个 pages，因此merge阶段一共需要 <a href="https://www.codecogs.com/eqnedit.php?latex=\dpi{100}&space;\lceil&space;Log_{2}{N}&space;\rceil" target="_blank"><img src="https://latex.codecogs.com/png.latex?\dpi{100}&space;\lceil&space;Log_{2}{N}&space;\rceil" title="\lceil Log_{2}{N} \rceil" /></a>趟。sorting阶段读入N个pages然后写回N个pages，因此是单独的一趟，同时每趟都需要 2N 次IO，因此两路归并排序一共需要
<a href="https://www.codecogs.com/eqnedit.php?latex=\dpi{100}&space;2*(1&plus;\lceil&space;Log_{2}{N}&space;\rceil)" target="_blank"><img src="https://latex.codecogs.com/png.latex?\dpi{100}&space;2*(1&plus;\lceil&space;Log_{2}{N}&space;\rceil)" title="2*(1+\lceil Log_{2}{N} \rceil)" /></a>次 IO。

### General K-way Merge Sort
假设我们有更多的buffer pool pages，总数为 B。那么在sorting阶段，我们可以一次读取 B 个 pages，假设带排序文件依然是 N 个 pages，那么在 sorting 阶段结束后，可以得到 <a href="https://www.codecogs.com/eqnedit.php?latex=\dpi{100}&space;2N*(1&plus;Log_{2}{\lceil&space;N&space;\rceil})" target="_blank"><img src="https://latex.codecogs.com/png.latex?\dpi{100}&space;2N*(1&plus;Log_{2}{\lceil&space;N&space;\rceil})" title="2N*(1+Log_{2}{\lceil N \rceil})" /></a> 个 runs。

在 merge 阶段，依然使用 1 个 buffer pool page 用作输出，那么我们可以同时对 B-1 个 runs 进行 merge。所以整个 K 路归并会有 <a href="https://www.codecogs.com/eqnedit.php?latex=\dpi{100}&space;1&plus;(Log_{B-1}{\lceil&space;\frac{N}{B}&space;\rceil})" target="_blank"><img src="https://latex.codecogs.com/png.latex?\dpi{100}&space;1&plus;(Log_{B-1}{\lceil&space;\frac{N}{B}&space;\rceil})" title="1+(Log_{B-1}{\lceil \frac{N}{B} \rceil})" /></a> 趟（1 for sorting），所以总的 IO 次数为 <a href="https://www.codecogs.com/eqnedit.php?latex=2N*(1&plus;Log_{B-1}{\lceil&space;\frac{N}{B}&space;\rceil})" target="_blank"><img src="https://latex.codecogs.com/gif.latex?2N*(1&plus;Log_{B-1}{\lceil&space;\frac{N}{B}&space;\rceil})" title="2N*(1+Log_{B-1}{\lceil \frac{N}{B} \rceil})" /></a> 次。

### Double Buffering Optimization
外部排序的一个常见优化是使用另一个 buffer 来保存预先读取的 page，这样当前 page 排序或者归并之后就不需要等待读取下一个 page。double buffering 需要另一个线程进行预读。