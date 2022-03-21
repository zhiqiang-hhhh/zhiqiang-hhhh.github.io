* ApusData MPP Engine

** 问题背景

ApusData 目前的 MPP 执行模式使用 Doris Vectorized Plan + ClickHouse ScanNode 实现。Doris Vectorized 版本相比 ClickHouse 社区版在单机的查询性能与功能两方面均有一定差距，主要体现在 1. Fragment 并发度不足，2. 使用了旧版本的 ClickHouse 代码库，缺失了许多优化（如 Pipeline 执行引擎），3. 类型系统无法对齐，缺失高阶类型（Array，Map）、AggregateFunction，4. 缺少复杂分析能力，如聚合组合子（-If，-Distinct），序列分析与留存分析等。为了提升 ApusData 的 MPP 查询性能，我们将谓词过滤与聚合计算进行了下推，并在大部分类型上对齐了底层数据结构（PODArray），实现了 ScanNode 的零拷贝，同时对 Fragment 的并行执行做了优化（共享哈希表）。然而这些优化方案只能覆盖部分场景，且 Case-By-Case 的优化 ROI 较低，可维护性较差。

Doris 与 ClickHouse 的优缺点罗列如下，

|                   | Doris | ClickHouse |
|-------------------+-------+------------|
| MPP 查询计划      | ✓     |            |
| MPP 引擎          | ✓     |            |
| Pipeline 执行引擎 |       | ✓          |
| 复杂类型系统      |       | ✓          |
| 存储引擎          |       | ✓          |

** 优化思路

Doris 的 MPP 执行计划由一系列 Fragment 构成，每个 Fragment 实例在单个 BE 上运行，并通过 MPP 引擎进行数据交互。可以看出其执行逻辑是相对独立的。因此，我们可使用 ClickHouse 执行引擎替换 Doris 的 PlanFragmentExecutor，将单个 BE 上单个 Query 的所有 Fragments 汇总执行，以达到比肩 ClickHouse 的单机执行性能。该方案有如下三个优势：

1. 单 Query 单 BE 的 Fragments 可全量并行以及 Pipeline 执行，理论上能取得最高的 CPU 利用率。
2. 存储计算一体化能有效减少代码冗余与数据交互开销，降低研发成本。
3. 该方案可逐步演进，Fragments 可部分使用 Pipeline 或 PlanFragmentExecutor 执行，兼容性好。
4. 可保留 ClickHouse 的分布式执行引擎，用以支撑复杂分析需求。

** 具体方案

1. Fragment 翻译

   1.1. FE 将 Fragment 转换成语义相同的 SQL 语句，其中 FROM [TABLE] 分两类：1. ScanNode 对应 ClickHouse 表，2. Exchange 节点由 ExchangeTableFunction 替换，包含对应的 Exchange 参数列表。SELECT list 对应 OUTPUT EXPRS。
   1.2. BE 收到 Fragment 后，如包含 SQL，构建 ClickHouse QueryPlan，否则退化为 PlanFragmentExecutor 执行。

2. 执行调度器

   3.1. MPPQueryCoordinator 向所有参与计算的 BE 发起连接，打包发送所有的 Fragments；
   3.2. BE 对收到的 Fragments 进行 prepare 操作，完成 Exchange/Sink 注册，同时为每个 Fragment 构建 QueryPlan，最终合成一个 QueryPipeline；
   3.3. MPPQueryCoordinator 发起执行命令，之后对 ResultSink 进行 Polling，同时收集执行过程中的 Progress/Trace/Log 事件；
   3.4. BE 收到执行指令，使用 PipelineExecutor 对所有 Fragment 统一进行调度执行；
   3.5. MPPQueryCoordinator 可发起 Cancel 指令，或等待执行完毕对资源进行统一销毁。

3. MPP 数据交互

4.1 DataSink 适配 ClickHouse Block 结构，并适配 hash 分发。

PipelineExecutor -> DataStreamSender ---> Channel -> Block serialize to WriteBuffer(butil::IOBuf)
                                      |
                            partition |-> Channel
                                      |
                                      |-> Channel

4.2 Exchange 改造。构建 ExchangeTableFunction 接收 Block，需实现 sort merge。

ExchangeTableFunction <- DataStreamRecvr <---- sender_queue[0] <- Block deserialize from ReadBuffer(butil::IOBuf)
                                                                           |
                                                               is_merging? |-- sender_queue[1]
                                                                           |
                                                                           |-- sender_queue[2]


** 工作计划

执行调度器的工作有一部分是通用的，包括 Cancellation，Metric 收集等等，可高优推进。

Fragment 翻译工作可循序渐进，执行调度器需支持 PipelineExecutor + PlanFragmentExecutor 混合执行。

MPP 数据交互部分基础工作较少，但包含额外的可选工作：
 1. 使用 brpc 的流式接口投递 block，本地投递可考虑使用 Processor Port 直连优化。
 2. Runtime Filter 分发至 ClickHouse QueryPipeline。
