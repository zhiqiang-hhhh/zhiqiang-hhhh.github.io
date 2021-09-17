# Doris Tasks

核心类 AgentBatchTask 

AgentBatchTask.run() 方法遍历当前Job的所有Task，通过
`Thrift Client.submitTasks(agentTaskRequests)`发送Task。