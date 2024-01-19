
<!-- @import "[TOC]" {cmd="toc" depthFrom=1 depthTo=6 orderedList=false} -->

<!-- code_chunk_output -->

- [Physical plan to PlanFragment](#physical-plan-to-planfragment)

<!-- /code_chunk_output -->



### Physical plan to PlanFragment

```java
public class StmtExecutor {
    private void executeByNereids(queryId) throws Exception {
        ...
        planner = new NereidsPlanner(statementContext);
        try {
            planner.plan(parsedStmt, context.getSessionVariable().toThrift());
            checkBlockRules();
        }
        ...
        handleQueryWithRetry(queryId);
    }
}

public class NereidsPlanner extends Planner {
    ...
    Plan resultPlan = plan(parsedPlan, requireProperties, explainLevel);
}

```



```java{.line-numbers}
public class NereidsPlanner extends Planner {
    ...
    @Override
    public void plan(StatementBase queryStmt, org.apache.doris.thrift.TQueryOptions queryOptions) {
        ...
        PlanFragment root = physicalPlanTranslator.translatePlan(physicalPlan);
        ...
    }
}
```
上面第 6 行完成物理执行计划到 PlanFramgnet 的转换。也就是在下面的函数里完成的
```java
public class PhysicalPlanTranslator extends DefaultPlanVisitor<PlanFragment, PlanTranslatorContext> {
    ...
    public PlanFragment translatePlan(PhysicalPlan physicalPlan) {
        PlanFragment rootFragment = physicalPlan.accept(this, context);
        ...
        return rootFragment;
    }
    ...
}
```
这里 PhysicalPlan 是一个树状的数据结构，它继承自 Plan:
```java
public interface Plan extends TreeNode<Plan> {
    ...
    <R, C> R accept(PlanVisitor<R, C> visitor, C context);
    ...
}

public interface PhysicalPlan extends Plan {
    ...
}

public PhysicalXXX extends PhysicalPlan {
    @Override
    public <R, C> R accept(PlanVisitor<R, C> visitor, C context) {
        return visitor.visitXXX(this, context);
    }
}
```
每个 Plan 有一个 accept 方法，这是一个模板函数。
对于每个 PhysicalXXX，其 accept 函数里会调用 visitor 的 visitXXX 方法，visitXXX 方法将会把当前的 PhysicalPlan 转成一个 PlanFragment。

比如 PhysicalOlapScan:
```java
public interface LeafPlan extends Plan, LeafNode<Plan> {
    ...
}

public abstract class PhysicalLeaf extends AbstractPhysicalPlan implements LeafPlan {
    ...
}

public abstract class PhysicalRelation extends PhysicalLeaf implements Relation {
    ...
}

public abstract class PhysicalCatalogRelation extends PhysicalRelation implements CatalogRelation {
    ...
}

public class PhysicalOlapScan implements PhysicalCatalogRelation {
    ...
    @Override
    public <R, C> R accept(PlanVisitor<R, C> visitor, C context) {
        return visitor.visitPhysicalOlapScan(this, context);
    }
    ...
}
```
PhysicalOlapScan 的 accept 方法被实现成调用传入的 PlanVisitor 的 visitPhysicalOlapScan 方法。

PhysicalPlanTranslator 就是一个 PlanVisiter 对象，它实现了 visitPhysicalOlapScan 
```java
@Override
    public PlanFragment visitPhysicalOlapScan(PhysicalOlapScan olapScan, PlanTranslatorContext context) {
        ...
        OlapScanNode olapScanNode = new OlapScanNode(olapScan.translatePlanNodeId(), tupleDescriptor, "OlapScanNode");
        
        // TODO: maybe we could have a better way to create fragment
        PlanFragment planFragment = createPlanFragment(olapScanNode, dataPartition, olapScan);
        context.addPlanFragment(planFragment);
        updateLegacyPlanIdToPhysicalPlan(planFragment.getPlanRoot(), olapScan);
        return planFragment;
    }
```
