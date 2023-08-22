buildConnectContext 这个函数会设置两个参数：
```java
sessionVariable.parallelExecInstanceNum = Config.statistics_sql_parallel_exec_instance_num;
sessionVariable.parallelPipelineTaskNum = Config.statistics_sql_parallel_exec_instance_num;
```
