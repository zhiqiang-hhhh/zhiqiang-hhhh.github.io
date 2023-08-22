precondition：fragment 节点的数量需要大于 1（WHY？），因此最后收集结果的节点一定有一个 ResultSink 和一个 VExchange 节点。
```sql
mysql [(none)]>explain select * from demo.example_tbl;
+---------------------------------------------------------------------------------------------------------------+
| Explain String                                                                                                |
+---------------------------------------------------------------------------------------------------------------+
| PLAN FRAGMENT 0                                                                                               |
|   OUTPUT EXPRS:                                                                                               |
|     user_id[#0]                                                                                               |
|     date[#1]                                                                                                  |
|     city[#2]                                                                                                  |
|     age[#3]                                                                                                   |
|     sex[#4]                                                                                                   |
|     last_visit_date[#5]                                                                                       |
|     cost[#6]                                                                                                  |
|     max_dwell_time[#7]                                                                                        |
|     min_dwell_time[#8]                                                                                        |
|   PARTITION: UNPARTITIONED                                                                                    |
|                                                                                                               |
|   VRESULT SINK                                                                                                |
|                                                                                                               |
|   1:VEXCHANGE                                                                                                 |
|      offset: 0                                                                                                |
|                                                                                                               |
| PLAN FRAGMENT 1                                                                                               |
|                                                                                                               |
|   PARTITION: HASH_PARTITIONED: user_id[#0]                                                                    |
|                                                                                                               |
|   STREAM DATA SINK                                                                                            |
|     EXCHANGE ID: 01                                                                                           |
|     UNPARTITIONED                                                                                             |
|                                                                                                               |
|   0:VOlapScanNode                                                                                             |
|      TABLE: default_cluster:demo.example_tbl(example_tbl), PREAGGREGATION: OFF. Reason: No aggregate on scan. |
|      partitions=1/1, tablets=1/1, tabletList=10085                                                            |
|      cardinality=7, avgRowSize=1045.0, numNodes=1                                                             |
|      pushAggOp=NONE                                                                                           |
+---------------------------------------------------------------------------------------------------------------+
31 rows in set (0.02 sec)
```

```txt
PlanFragment
    PlanFragmentId: 01
    PlanNode(ExchangeNode):   // rootNode
        planNodeName: "VEXCHANGE"
        Id: "01"
        PlanFragment: 指向包含该 Node 的 PlanFragment
    ExchangeNode: null
    DataSink(ResultSink):
        PlanNodeId: "01"    // 说明这个 sink 节点对应的 exchange 节点的 Id 为 01
        PlanFragment: 指向包含该 Sink 的 PlanFragment
    ParallelExecNum : 2

PlanFragment
    PlanFragmentId: 00
    PlanNode(OlapScanNode):
        planNodeName: "OlapScanNode"
        Id: "00"
        PlanFragment: 指向包含该 Node 的 PlanFragment
        OlapTable: 指向 ScanNode 的目标 table
        IndexId: 10084
        PartitionIds: [1]
        TotalBytes:  1463
        Childern: null
    DataSink(DataStreamSink):
        ExchangeNodeId: 01
        PlanNodeId: "01"    // 说明这个 sink 节点对应的 exchange 节点的 Id 为 01
        PlanFragment: 指向包含该 Sink 的 PlanFragment
    ParallelExecNum : 2
```

* 注意，planRoot 不包含 sink 节点，sink 单独作为一个字段


```java
private void sendPipelineCtx() {
    ...

    for (PlanFragment fragment : fragments) {
        FragmentExecParams params = fragmentExecParamsMap.get(fragment.getFragmentId());
        ...
        Map<TNetworkAddress, TPipelineFragmentParams> tParams = params.toTPipelineParams(backendIdx);
    }
}
```
`fragmentExecParamsMap` 是在 Coordinator::prepare 中构造的，其实就是一个内存中的索引数据结构，方便寻找参数。

FragmentExecParams 是 pipeline 中的一个类型，这个类型由 pipeline 的代码从 PlanFragment 构造，**后者是 Planner 负责构造的**。

注意这段代码 `Map<TNetworkAddress, TPipelineFragmentParams> tParams = params.toTPipelineParams`，tParams 是一个 Map，

TODO: 这里的 TPipelineFragmentParams 应该是对应一个 fragment，那为啥这里会有一个 Map<Address, TPipelineFragmentParams> 呢？按理说应该就一个 pair<Address, TPipelineFragmentParams> 啊。只能说明一个 fragment 有可能会发给多个 BE 去执行

```cpp
class TPipelineFragmentParams {
    ...
    TPlanFragment fragment;
    TPipelineInstanceParam local_params;
    ...
}
```




### be
```plantuml
class PipelineFragmentContext {
    - _query_id : TUniqueId
    - _fragment_instance_id : TUniqueId
    - _fragment_id : int
    - _backend_num : int
    - _exec_env : ExecEnv
    - _exec_status : Status
    - _cancel_msg : string
    - _pipelines : List<Pipeline>
}

PipelineFragmentContext o-- Pipeline

class Pipeline {
    - _operator_builders : OperatorBuilders
    - _sink : OperatorBuilder
    - _parents : List<Pipeline>
    - _dependencies : List<Pipeline>
    - _pipeline_id : PipelineId
    - _context : PipelineFragmentContext
    - _pipeline_profile : RuntimeProfile
}
```



```cpp

```

be 上，先读 TP
