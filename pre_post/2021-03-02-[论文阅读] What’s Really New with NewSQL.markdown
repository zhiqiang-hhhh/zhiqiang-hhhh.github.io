论文名称：What‘s Really New with NewSQL 
作者：Andrew Pavlo & Matthew Aslett

本文的目标：针对各种新式NewSQL，宣称其具有处理传统系统无法处理的大规模OLTP负载的能力，本文将会分析这种说法是确有其事还是只是一个营销噱头（marketing）；如果确有其事，那么让他们具有此能力的关键技术是什么。

本文先讨论数据库系统的历史，以分析NewSQL的出现原因，然后对NewSQL一词到底具有什么含义给出详细解释，最后对满足该定义的各种系统进行分类。

## DBMS的简要历史

1960s，出现了第一批DBMS。比如IBM’s IMS。

然后在这基础上，1970s，出现了IBM‘s System R，INGRES，Oracle

1980s，Sybase，Informix，IBM's DB2。

1980s后期出现了面向对象的数据库。这类数据库由于缺少类似SQL的统一接口标准而失败。
但是他们的很多思想被后来的主要供应商学习，添加了对对象以及XML的支持，后来的No SQL数据库也借鉴了其面向文档的设计。

1990s出现了当今两大开源DBMS项目：MySQL以及PostgreSQL。

2000s互联网开始爆炸，传统单机架构无法满足要求。互联网公司开始使用中间件：将单节点DMBS扩展为集群，数据库集群对于应用来说与单个数据库无差。两个著名的例子：eBay‘s Oracle-based cluster 以及 Google‘s MySQL-based cluster。

中间件能够满足如读取以及更新单个记录的操作的要求。然而对于以事物方式更新多个记录或者JOIN操作的需求，中间件无法满足。事实上中间件不会允许此类操作。要求应用在应用层代码中自己完成这些动作。

最终，各大公司开始开发分布式DBMS。三个原因：
1. 当时传统DBMS专注于以牺牲可用性和性能为代价，去实现数据一致性以及数据正确性。但是这种trade-off不满足Web-based application的要求。
2. it was thought that there was too much overhead in using a fullfeatured DBMS like MySQL as a “dumb” data store.
3. 传统关系模型不是表现应用数据的最好方式。

这些导致2000s中期NoSQL的出现。这类系统的典型特点是其放松了对事务的保证，放弃了关系模型，以支持最终一致性和更多的数据模型（KV，图，文档）。两个著名系统：Google‘s BigTable，Amazon’s Dynamo。起初这俩系统不对外服务，因此其他公司开发了对这两个系统的克隆：Facebook‘s Cassandra 以及 PowerSet’s Hbase。

在2000s后期，已经有很多著名的分布式DBMS出现。然而，在享受NOSQL带来的高可扩展性同时，Google等大公司发现其开发人员不得不花费很多时间来处理不一致的数据，如果能够支持事物将会极大提高工作效率。同时，有一些系统，比如订单与交易系统等，依然再使用传统DBMS，因为其对事务要求较高。以上两点，促成了NewSQL的出现。

## NewSQL 的出现
对NewSQL的定义：一类现代关系型DBMS，目标是为OLTP的读写负载提供与NoSQL一样的可扩展性能，因此同时保持对事务的ACID保证。