[TOC]
AnalyticDB: 实时OLAP数据库。简称ADB。

## 创新贡献
1. Efficient Index Management。为了满足 ad-hot 负载查询的性能要求，ADB为所有表的所有列都创建了索引。并且使用了所谓“runtime filter-ratio-based index path selection mechanism“来避免索引滥用。为了比秒在特定路径大规模更新索引，ADB中索引会在off-peak时异步构建。对于构建索引期间的增量数据，维护了一个轻量级的有序索引来减少异步索引过程的影响。
2. 对于结构化和负载类型数据设计了一种分层存储。设计饿了底层存储来支持混合类型的行列格式。利用快速的顺序磁盘IO来使得其overhead处于OLAP/点查负载可以接受的范围内。
3. 读写解耦以同时支持高吞吐量的写和低延迟查询。读与写会被读节点与写节点分开处理，这两类节点之间相互隔离。写节点会把写数据写入Pangu分布式存储；读节点会使用version verification机制来确保读数据时数据的freshness。一个高效的实时采样技术用于评估优化器的开销（an efficient real-time sampling technique for cardinality estimation in cost based optimizer.）设计了一个高性能的向量化执行引擎用于混合存储，提高了计算密集型分析查询的执行效率。

## 2. 与其他系统区别
### OLTP数据库
MySQL与PostgreSQL设计之初便是用于支持事务性Query（一行或者多行的点查询），因此OLTP数据库的存储引擎通常使用B+树索引以及行式存储。然而在分析型查询下，行式存储会引入巨大的IO开销。此外，OLTP数据库往往会在写入路径上主动更新索引，这会影响写入吞吐与查询时延。
### OLAP数据库
Vertia使用projection来提高查询性能，Vertica的索引只保留Min/Max值，这导致查询时查询裁剪效率较低，查询时延较高。teradataDB与Greenplum使用了列村，并且允许用户指定被索引的列。然而他们主要有两类限制：首先，他们会在写入路径上修改列的索引，这对于ADB采用的全列索引（all-column index）来说是效率很低；其次，面对很多点查询请求，列存的随机IO很多。
### 大数据系统
以Map-Reduce模型为基础的批处理请求引擎。比如Hive以及Spark-SQL。适合处理离线任务：任务持续数分钟以上。Impala通过使用pipeline处理模型以及列式存储，将离线查询的时延降低。但是Impala没有列存索引（只有Min/Max statics），因此Impala难以处理复杂的查询。
### 实时OLAP系统
Druid以及Pinot，均使用列存。均构建了bitmap-based 倒排索引。Pinot对所有列，Druid只是对Dimension Columns。如果查询涉及到的列不是Dimension Columns，那么会有较高的时延。这俩系统的索引都是在写入路径上更新的，这会影响写入性能。除此之外，他们均缺少对于重要查询类型如JOIN，UPDATE以及DELETE的支持。因此对于point-lookup query，这些系统的效率很低。
### 云分析服务（Cloud Analytival Services）
Amazon Redshift以及Google BigQuery
Amazon Redshift：包含计算节点与Leader Node。与此对照，ADB使用的是读写解耦的读写节点相互独立架构。
Google BigQuery：使用列式存储以及基于树形拓扑的查询分发。ADB行列混合，并且使用DAG执行框架。

## 3. 系统设计
ADB利用阿里云的基础设施：Pangu分布式存储以及Fuxi资源管理与任务调度器。
### 3.1 数据模型与查询语言
支持标准关系型模型：关系中的元祖具有固定的schema。
支持文本类型、JSON以及向量。
ADB支持 ANSI SQL：2003。并且对其进行了加强。
### 3.2 分区（Table Partitioning）
两层分区：Priamry & secondary
Primary partition is based on the hash of a user-specified column.
Secondary partition: range partition, 需要定义最大分区数量。典型以时间进行分区。当分区数达到阈值，会把最旧的分区删除。

### 3.3 架构总览
节点分为三类：
Cordinator：从客户端接收请求，并转发给读写节点
Write Node
Read Node

流水间模式的执行引擎。
### 3.4 读写解耦
#### 高吞吐的写
从所有写节点中选出一个Master节点，所有节点通过ZooKeeper来进行同步。当写节点启动时，Master节点会给该节点配置一个分区，协调节点接收到写请求后根据该配置进行写请求转发。所有的写节点相当于一个内存缓冲池，他们把接收到的SQL语句当作log周期性地刷到分布式文件系统里。当分布式文件系统中的log数量达到一定程度后，ADB会通过Fuxi启动一批MapReduce任务来把日志真正转换为数据。
#### 实时读
协调节点为每一个读节点分配一个分区号。
每个节点会从分布式文件系统读取最初的分区，并且从写节点周期性地拉取后续更新，并且把这些更新写到本地副本中（不会写回分布式文件系统），直接从写节点拉取更新的方式减少了同步延迟。

ADB提供两种读模式。实时读：立刻读到写完的数据。Bounded-staleness read：有一定延迟。

实时读模式会面临数据一致性问题，ADB设计了一套版本机制来解决问题。

#### 可靠性与可扩展性

### 3.5 集群管理
设计了一套集群管理工具：Gallardo。相当于管控系统。

## 4. 存储
支持结构化与非结构化数据。混合行列存储。