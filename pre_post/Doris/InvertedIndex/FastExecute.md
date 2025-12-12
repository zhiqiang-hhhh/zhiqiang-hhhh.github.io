```sql
SELECT
    count()
FROM
    mapleleaf_100801.bklog_BCS_K8S_40510_stdout_all_analysis_100801
WHERE
    `thedate` = 20251206
    AND (
        (
            (
                (
                    (log MATCH_PHRASE 'register_time')
                    AND (log MATCH_PHRASE 'openid')
                )
                OR (
                    log MATCH_PHRASE 'process  cronevent  success'
                )
            )
            AND (
                CAST(__ext ['io_kubernetes_pod'] AS TEXT) = 'chatsvr-8444b5d946-d5dh'
            )
        )
        OR (
            (
                CAST(__ext ['io_kubernetes_pod'] AS TEXT) = 'fightsvr-act-6d6b85bd46-d9kbt'
            )
            AND (
                log MATCH_PHRASE 'before:  gchub:master-test-main:latest'
            )
        )
    )
limit
    50
```
谓词中有 `OR`，`AND` 组成的复合谓词，复合谓词中有些谓词可以走倒排索引，比如这里的 `log MATCH_PHRASE 'before:  gchub:master-test-main:latest'`，有些谓词所在的列没有倒排或者谓词本身不能走索引，比如`CAST(__ext ['io_kubernetes_pod'] AS TEXT) = 'fightsvr-act-6d6b85bd46-d9kbt'`，Doris 会在索引分析阶段 DFS 遍历表达式树，对于能走索引的部分子表达式，走索引，并且把结果保存在 `ExprContext::IndexExecContext` 里面。
由于整个表达式里只有部分表达式可以通过索引过滤，还有部分谓词需要在 batch read 阶段计算，因此完整的 Expr 不能被删除。

在 batch read 阶段，执行到 MATCH_PHRASE 这个表达式的时候，其内部有一个判断能否 `fast_execute` 的短路计算逻辑，可以直接返回结果
```cpp
bool VExpr::fast_execute(VExprContext* context, ColumnPtr& result_column) const {
    if (context->get_index_context() &&
        context->get_index_context()->get_index_result_column().contains(this)) {
        // prepare a column to save result
        result_column = context->get_index_context()->get_index_result_column()[this];
        if (_data_type->is_nullable()) {
            result_column = make_nullable(result_column);
        }
        return true;
    }
    return false;
}
```