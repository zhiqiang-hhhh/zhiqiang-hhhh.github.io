
<!-- @import "[TOC]" {cmd="toc" depthFrom=1 depthTo=6 orderedList=false} -->

<!-- code_chunk_output -->

- [profile on FE](#profile-on-fe)
  - [Profile on BE](#profile-on-be)

<!-- /code_chunk_output -->
## profile on FE

```plantuml
class StmtExecutor {
    - profile : Profile
}
StmtExecutor o-- Profile

class Profile {
    - rootProfile : RuntimeProfile
    - summaryProfile : SummaryProfile
    - executionProfiles : List<ExecutionProfile>
    - isFinished : boolean
    + addExecutionProfilwe(ExecutionProfile) : void
    + update(startTime, Map<String, String> summaryInfo, isFinished) : void
}

Profile o-- RuntimeProfile
Profile o-- SummaryProfile
Profile o-- ExecutionProfile

class RuntimeProfile {
    ...
    - infoStrings : map<String, String>
    - childMap : map<String, RuntimeProfile>
    - childList : list<pair<RuntimeProfile, Boolean>>
    - name : String
    + update(TRuntimeProfileTree) : void
}

class SummaryProfile {
    ...
    - summaryProfile : RuntimeProfile
    - executionSummaryProfile : RuntimeProfile
}

class ExecutionProfile {
    - execProfile : RuntimeProfile
    - fragmentProfiles : List<RuntimeProfile>
    - loadChannelProfile : RuntimeProfile
}

```







### Profile on BE
每个 fragment instance 对应一个 be 上的 PipelineFragmentContext 对象，执行 PipelineFragmentContext::prepare 的时候会创建成员变量 RuntimeProfile，以及 RuntimeState，后者内部也有一个 RuntimeProfile 对象，prepare 阶段还会创建 ExecNode 对象，ExecNode 对象里也有 RuntimeProfile 对象，不过该 RuntimeProfile 对象是在 `ExecNode::init_runtime_profile` 中创建的，
```plantuml
class FragmentMgr
{
    - context : PipelineFragmentContext
}

FragmentMgr o-- PipelineFragmentContext
PipelineFragmentContext o-- RuntimeProfile
PipelineFragmentContext o-- RuntimeState

RuntimeState o-- RuntimeProfile
PipelineFragmentContext o-- ExecNode
ExecNode o-- RuntimeProfile

class PipelineFragmentContext {
    - _runtime_profile : RuntimeProfile
    - _runtime_state : RuntimeState
    - _root_plan : ExecNode
}

class RuntimeProfile {
    - _name : string
    - _is_sink : bool
    - _counter_map : map<string, Counter>
    - _child_map : map<std::string, RuntimeProfile*>
    + to_thrift() : void
    + add_child(RuntimeProfile* child, RuntimeProfile* parent);
}

```

```txt
PipelineFragmentContext::prepare
    ExecNode::create_tree
        ExecNode::create_tree_helper
            ExecNode::init
                ExecNode::init_runtime_profile
                    ExecNode._runtime_profile.reset(new RuntimeProfile(ss.str()));

    ExecNode::prepare
        // ExecNode._runtime_profile.add_counte ...
        for (child : childer)
            ExecNode::prepare
    PipelineFragmentContext::_build_pipelines
    PipelineFragmentContext::_build_pipeline_tasks
        for (pipeline : PipelineFragmentContext._pipelines)
            PipelineFragmentContext._runtime_profile->add_child(pipeline->pipeline_profile(), true, nullptr);
```

```cpp
void PipelineTask::_init_profile() {
    _task_profile =
            std::make_unique<RuntimeProfile>(fmt::format("PipelineTask (index={})", _index));

    _parent_profile->add_child(_task_profile.get(), true, nullptr);
    _task_cpu_timer = ADD_TIMER(_task_profile, "TaskCpuTime");
    ...
}
```
-----------

insert into select 的结束以来于 BE 的汇报进度。

```java
class ExecutionProfile {
    // A countdown latch to mark the completion of each instance.
    // instance id -> dummy value
    private MarkedCountDownLatch<TUniqueId, Long> profileDoneSignal;

    public void markOneInstanceDone(TUniqueId fragmentInstanceId) {
        if (profileDoneSignal != null) {
            profileDoneSignal.markedCountDown(fragmentInstanceId, -1L);
        }
    }

    public boolean awaitAllInstancesDone(long waitTimeS) throws InterruptedException {
        if (profileDoneSignal == null) {
            return true;
        }
        return profileDoneSignal.await(waitTimeS, TimeUnit.SECONDS);
    }
}

class Coordinator {
    void updateFragmentExecStatus(TReportExecStatusParams params) {
        executionProfile.markOneInstanceDone(params.getFragmentInstanceId());
    }
}

class QeImpl {
    public TReportExecStatusResult reportExecStatus(TReportExecStatusParams params, TNetworkAddress beAddr) {
        final QueryInfo info = coordinatorMap.get(params.query_id);
        if (info != null) {
            info.getCoord().updateFragmentExecStatus(params);
            if (params.isSetProfile()) {
                writeProfileExecutor.submit(new WriteProfileTask(params, info));
            }
        }
    }
}
```
所以说，只要 INSERT INTO SELECT 返回成功，说明：
1. profileDoneSignal 一定为空
2. 等待超时
