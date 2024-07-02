```sql
select count(distinct number) from numbers("number" = "100");
```
```text
+----------------------------------------------------------+
| Explain String(Nereids Planner)                          |
+----------------------------------------------------------+
| PLAN FRAGMENT 0                                          |
|   OUTPUT EXPRS:                                          |
|     count(number)[#4]                                    |
|   PARTITION: UNPARTITIONED                               |
|                                                          |
|   VRESULT SINK                                           |
|      MYSQL_PROTOCAL                                      |
|                                                          |
|   6:VAGGREGATE (merge finalize)(167)                     |
|   |  output: count(partial_count(number)[#3])[#4]        |
|   |  group by:                                           |
|   |  distribute expr lists:                              |
|   |                                                      |
|   5:VEXCHANGE                                            |
|      offset: 0                                           |
|                                                          |
| PLAN FRAGMENT 1                                          |
|                                                          |
|   PARTITION: HASH_PARTITIONED: number[#1]                |
|                                                          |
|   STREAM DATA SINK                                       |
|     EXCHANGE ID: 05                                      |
|     UNPARTITIONED                                        |
|                                                          |
|   4:VAGGREGATE (update serialize)(161)                   |
|   |  output: partial_count(number[#2])[#3]               |
|   |  group by:                                           |
|   |  distribute expr lists: number[#2]                   |
|   |                                                      |
|   3:VAGGREGATE (merge serialize)(158)                    |
|   |  group by: number[#1]                                |
|   |  distribute expr lists: number[#1]                   |
|   |                                                      |
|   2:VEXCHANGE                                            |
|      offset: 0                                           |
|                                                          |
| PLAN FRAGMENT 2                                          |
|                                                          |
|   PARTITION: RANDOM                                      |
|                                                          |
|   STREAM DATA SINK                                       |
|     EXCHANGE ID: 02                                      |
|     HASH_PARTITIONED: number[#1]                         |
|                                                          |
|   1:VAGGREGATE (update serialize)(152)                   |
|   |  STREAMING                                           |
|   |  group by: _table_valued_function_numbers.number[#0] |
|   |                                                      |
|   0:VDataGenScanNode(149)                                |
|      table value function: NUMBERS                       |
+----------------------------------------------------------+
63 rows in set (0.02 sec)
```

```text
PLAN FRAGMENT 0

fragment = TPlanFragment {
    plan = TPlan {
        nodes = {
            [0] = TPlanNode {
                node_id = 6,
                node_type = AGGREGATION_NODE,
                agg_node = TAggregationNode {
                    aggregate_functions = list {
                        [0] = TExpr {
                            nodes = list {
                                [0] = TExprNode {
                                    node_type = AGG_EXPR,
                                    agg_expr = TAggregateExpr {
                                        is_merge_add = true,
                                    },
                                    fn = TFunction {
                                        name = count,
                                        signature = "count(BIGINT)",
                                        aggregate_fn = TAggregateFunction {
                                            ...
                                        }}},
                                [1] = TExprNode {
                                    node_type = SLOT_REF,
                                },}}}}},
            [1] = TPlanNode {
                node_id = 5,
                node_type = EXCHANGE_NODE = 9
                exchange_node = TExchangeNode {
                    ...
                }}}},
    output_sink = TDataSink {
        type = RESULT_SINK = 1,
        result_sink = TResultSink {
            type = 0,
        }},
    partition = TDataPartition {
        type = 0,
    }
}

PLAN FRAGMENT 1

fragment = TPlanFragment {
    plan = TPlan {
        nodes = {
            [0] = TPlanNode {
                node_id = 4,
                node_type = AGGREGATION_NODE
                num_children = 1,
                agg_node = TAggregationNode {
                    aggregate_functions = {
                        [0] = TExpr {
                            nodes = list {
                                [0] = TExprNode {
                                    node_type = AGG_EXPR,
                                    num_children = 1,
                                    agg_expr = TAggregateExpr {
                                        is_merge_agg = false,
                                    },
                                    fn = TFunction {
                                        name = count
                                        signature = "count(BIGINT)",
                                        aggregate_fn = TAggregateFunction {
                                            ...
                                        }}},
                                [1] = TExprNode {
                                    node_type = SLOT_REF,
                                    slot_ref = TSlotRef {
                                        slot_id = 2,
                                        }}}},}},
                distribute_expr_lists = list {
                    [0] = list {
                        [0] = TExpr {
                            node = list {
                                [0] = TExprNode {
                                    node_type = SLOT_REF
                                    },}}}}},
            [1] = TPlanNode {
                node_id = 3,
                node_type = AGGREGATION_NODE = 6,
                num_children = 1,
                grouping_exprs = list {
                    [0] = TExpr {
                        nodes = list {
                            [0] = TExprNode {
                                node_type = SLOT_REF,
                                slot_ref = TSlotRef {
                                    slot_id = 1,
                                    }}}}},
                distribute_expr_lists = list {
                    [0] = list {
                        [0] = TExpr {
                            nodes = list {
                                [0] = TExprNode {
                                    node_type = SLOT_REF = 16,
                                    slot_ref = TSlotRef {
                                        slot_id = 1,
                                        }}}}}}},
            [2] = TPlanNode {
                node_id = 2,
                node_type = EXCHANGE_NODE = 9,
                num_children = 0,
                exchange_node = TExchangeNode {
                    ...
                }}}},
    output_sink = TDataSink {
        type = DATA_STREAM_SINK = 0,
        stream_sink = TDataStreamSink {
            dest_node_id = 5,
            output_partition = TDataPartition {
                type = 0,
                partition_exprs = {
                    ...
                }}}},
    partition = TDataPartition {
        type = HASH_PARTITIONED = 2,
        partition_exprs = {
            [0] = TExpr {
                nodes = {
                    [0] = TExprNode {
                        node_type = SLOT_REF = 16,
                        slot_ref = TSlotRef {
                            slot_id = 1
                        }}}}}}
}

PLAN FRAGMENT 2

fragment = TPlanFragment {
    plan = TPlan {
        nodes = {
            [0] = TPlanNode {
                node_id = 1,
                node_type = AGGREGATION_NODE = 6,
                num_children = 1,
                agg_node = TAggregationNode {
                    grouping_exprs = {
                        [0] = TExpr {
                            nodes = {
                                [0] = TExprNode {
                                    node_type = SLOT_REF = 16,
                                    slot_ref = TSlotRef {
                                        slot_id = 0,
                                    }}}}},
                    use_streaming_preaggregation = true,
                }},
            [1] = TPlanNode {
                node_id = 0,
                node_type = DATA_GEN_SCAN_NODE = 28,
                }}},
    output_sink = TDataSink {
        type = DATA_STREAM_SINK = 0,
        stream_sink = TDataStreamSink {
            dest_node_id = 2,
            output_partition = TDataPartition {
                type = HASH_PARTITIONED = 2,
                partition_exprs = {
                    [0] = TExpr {
                        nodes = {
                            [0] = TExprNode {
                                node_type = SLOT_REF = 16,
                                slot_ref = TSlotRef {
                                    slot_id = 1,
                                }}}}
                    ...
                }}}},
    partition = TDataPartition {
        type = RANDOM = 1,
    }
}
```
