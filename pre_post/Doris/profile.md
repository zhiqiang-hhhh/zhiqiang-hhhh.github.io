[TOC]

## profile
```java
public class StmtExecutor {
    ...
    public void executeByLegacy(queryid) {
        ...
        if (!context.isTxnModel()) {
            ...
            try (Scope ignored = queryAnalysisSpan.makeCurrent()) {
                // analyze this query
                analyze(context.getSessionVariable().toThrift());
            }
        }
        ...
        else if (parsedStmt instanceof ShowStmt) {
            handleShow();
        }
        ...
    }

    private void handleShow() throws IOException, AnalysisException, DdlException {
        ShowExecutor executor = new ShowExecutor(context, (ShowStmt) parsedStmt);
        ShowResultSet resultSet = executor.execute();
        if (resultSet == null) {
            // state changed in execute
            return;
        }
        if (isProxy) {
            proxyResultSet = resultSet;
            return;
        }

        sendResultSet(resultSet);
    }

}

public class ShowQueryProfileStmt {
    ...
    // 用于检查 SHOW PROFILE 语法是否正确，确定 SHOW 的类型是全部/Fragment/Instance
    pubcli void analyze(Analyzer analyzer) {
        ...
    }
}

public class ShowExecutor {
    ...
    private void handleShowQueryProfile() {
        ...
        ProfileTreeNode treeRoot = ProfileManager.getInstance().getFragmentProfileTree(showStmt.getQueryId(), showStmt.getQueryId());
        List<String> row = Lists.newArrayList(ProfileTreePrinter.printFragmentTree(treeRoot));
        rows.add(row);
        break;
    }
}

public class ProfileManager {
    ...
    public ProfileTreeNode getFragmentProfileTree(String queryID, String executionId) {
        ProfileElement element = queryIdToProfileMap.get(queryID);
        builder = element.builder;
        return builder.getFragmentTreeRoot(executionId);
    }
}
```

FE 上 profile 的收集:
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
    + addExecutionProfile(ExecutionProfile) : void
    + update(startTime, Map<String, String> summaryInfo, isFinished) : void
}

Profile o-- RuntimeProfile
Profile o-- SummaryProfile

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
class PipelineTask {
    
}
```
