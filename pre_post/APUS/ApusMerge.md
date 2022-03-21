# 云原生的Merge策略

# 问题背景

- 当前Clickhouse的MergeTree引擎会在数据写入之后做Merge，逐渐把小文件合并成大文件；合并成大文件之后，由于数据都是有序的，所以读取的时候能减少读取的文件块的数量，加速查询。通过一些业务方的线上使用来看，即使使用bulk这样的微批导入（比如微信log，每个批次2w条，大概6MB），写放大可以到8倍，调整一系列参数（例如，最大的part大小，每次merge的part数目）之后，也会有4，5倍左右，参见 [微信日志Clickhouse的优化]( https://km.woa.com/group/24938/articles/show/446018?kmref=search&from_page=1&no=1)。由于COS 底层也是类似LSM Tree的结构，所以他们会进一步放大，对磁盘的写放大压力最后是乘数关系。另外，COS 本身对于读写次数也是有收费的，所以在成本上也会有一定的提升。

- COS或者S3 之类的系统的SLA基本在[99.95%](https://cloud.tencent.com/document/product/301/1979)左右，这意味着1w次写入可能就会有5次失败，在Merge的时候如果产生的文件太大，直接写COS 会有失败的风险，失败重试，会带来大量的重复计算，有的时候如果频繁失败还会导致 Too Many Parts的问题。

- 当前只有一个节点参与Merge，在高速导入的情况下可能Merge不过来，需要引入多个副本并行Merge的策略。

# 方案思路

- Clickhouse中引入一种VolatilePart，这种Part 的特点是：
	- 它是由merge或者mutation产生，insert不能产生这种part。
	- 它产生之后，写入到本地磁盘，不会写入到COS，所以它的数据可能丢失。
	- 当它的数据上传到COS 之后，它就变成非volatile part了。
	- 所有NormalPart（也就是非VolatilePart）写入的时候直接写入COS。
	- 当读取的时候如果VolatilePart 不存在的话，那么就要从NormalPart中读取数据。
	
- Clickhouse 中我们实现2种DataPartVector
	- 类型一，NormalDataPartVector： 只包含非VolatilePart，这个列表跟当前的DataPartVector一致。
	- 类型二，VolatilePartVector： 包含VolatilePart。

- 更新Part列表的时候：
	- 先更新VolatilePartVector
	- 如果当前操作Part是NormalPart，那么还需要操作NormalPartVector
	- 通过这种机制，能够保证VolatilePart的数据如果被删除了，那么是可以从NormalPartVector中找到对应的NormalPart来恢复的。

下面我们通过我们系统中几种操作来看看如何工作的：

**Merge 操作**

- 多个节点都可以参与Merge，Merge的时候，先选择要Merge的Part，生成一个VolatilePart，写入Manifest。
- 当前节点以及其他replay操作的副本都更新VolatilePartVector
- 后续的读取操作都按照VolatilePartVector来读取，当然此时数据一定不存在，会读取对应的NormalPart的数据。
- 每个副本都可以拿到一个VolatilePart，如果对应的数据不存在，那么就在本地生成这个数据，然后上传到COS，上传到COS 之后，把VolatilePart更新为NormalPart。
- manifest的snapshot操作时跟现有的保持不变

**Mutation 操作**

- 选择Part列表的时候，从VolatilePart内选择
- 执行的时候，直接写COS，所以生成的全部为Normal Part。
- 判断一个mutation是否执行完毕的时候，要拿出NormalPartsVector来比较判断。

**Grab Old Parts**

- 直接遍历NormalPartsVector


**Read操作**

- 获取VolatilePartVector来操作

**Detach Attach操作**

- 暂时不支持

**Truncate Table操作**


	


