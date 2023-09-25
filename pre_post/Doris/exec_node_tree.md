create_tree_helper creates exec node tree in a recursive manner

```cpp
ExecNode::create_tree_helper(
    RuntimeState* runtime_state, 
    ObjectPool* object_pool,
    const std::vector<TPlanNode>& thrift_plan_nodes,
    const DescriptorTbl& descs,
    ExecNode* parent,
    int* node_idx,
    ExecNode** root
)
{
    const TPlanNode& curr_plan_node = thrift_plan_nodes[*node_idx];
    int num_children = curr_plan_node.num_children;

    // Step 1 Create current ExecNode according to current thrift plan node.
    ExecNode* node = nullptr;
    create_node(state, pool, curr_plan_node, descs, &node);

    // Step 1.1
    // Record current node if we have parent
    if (parent != nullptr) {
        parent->_children.push_back(node);
    } else {
        *root = node;
    }

    // Step.2 Create child ExecNode tree in a recursive manner
    for (int i = 0; i < num_children; i++) {
        ++*node_idx;
        create_tree_helper(state, pool, thrift_plan_nodes, descs, node, node_idx, nullptr);
    }

    // Step.3 Init myself after sub ExecNode tree is created and initialized
    node->init(curr_plan_node, state)


}
```

// runtime_state  is owned by PipelineFragmentContext
// object_pool is owned by RuntimeState

```cpp

```

#### vunion_node
##### init
```cpp
Status VUnionNode::init(const TPlanNode& tnode, RuntimeState* state) {
    ExecNode::init(tnode, state));
    
    // Create const_expr_ctx_lists_ from thrift exprs.
    const auto& const_texpr_lists = tnode.union_node.const_expr_lists;
    for (const auto& texprs : const_texpr_lists) {
        VExprContextSPtrs ctxs;
        RETURN_IF_ERROR(VExpr::create_expr_trees(texprs, ctxs));
        _const_expr_lists.push_back(ctxs);
    }

    // Create result_expr_ctx_lists_ from thrift exprs.
    auto& result_texpr_lists = tnode.union_node.result_expr_lists;
    for (auto& texprs : result_texpr_lists) {
        VExprContextSPtrs ctxs;
        RETURN_IF_ERROR(VExpr::create_expr_trees(texprs, ctxs));
        _child_expr_lists.push_back(ctxs);
    }
    return Status::OK();
}
```
What is const_expr_lists & result_expr_lists?

`select round(250/100)`

```txt
VUNION
    constExprLists
        FunctionCallExpr
            fnName: round
            fnParams:
                exprs:
                    FloatLiteral: 2.5
            type: DOUBLE
            ScalarFunction:
                name: round
                retType: DOUBLE
                argTypes: DOUBLE
            children:
                FloatLiteral
                    value: 2.5
    children: []
```
250/100=2.5 是在 FE 计算出来的

`select round(2.5)`
```txt
VUNION
    constExprLists
        FunctionCallExpr
            fnName: round
            fnParams:
                exprs:
                    DecimalLiteral
                        value: 2.5
                        type: DECIMALV3(2,1)
```

传到 be 的时候字面值都是 2.5 可是类型不一样。
对比一下代码：

```cpp
DoubleRoundOneImpl<round>



FunctionRounding<DoubleRoundOneImpl<round>, RoundingMode::round>
FunctionRounding<DecimalRoundOneImpl<round>, RoundingMode::round>

auto call = [&](const auto& types) -> bool {
    using Types = std::decay_t<decltype(types)>;
    using DataType = typename Types::LeftType;

    if constexpr (IsDataTypeNumber<DataType> || IsDataTypeDecimal<DataType>) {
        using FieldType = typename DataType::FieldType;
        res = Dispatcher<FieldType, rounding_mode, tie_breaking_mode>::apply(
                column.column.get(), scale_arg);
        return true;
    }
    return false;
}

if (!call_on_index_and_data_type<void>(column.type->get_type_id(), call)) {
            return Status::InvalidArgument("Invalid argument type {} for function {}",
                                           column.type->get_name(), name);
}

block.replace_by_position(result, std::move(res));
return Status::OK();

template <typename T, typename F>
bool call_on_index_and_data_type(TypeIndex number, F&& f) {
    switch(number) {
        ...
        case TypeIndex::Float32:
            return f(TypePair<DataTypeNumber<Float32>, T>());
        case TypeIndex::Float64:
            return f(TypePair<DataTypeNumber<Float64>, T>());
        ...
        case TypeIndex::Decimal32:
            return f(TypePair<DataTypeDecimal<Decimal32>, T>());
        case TypeIndex::Decimal64:
            return f(TypePair<DataTypeDecimal<Decimal64>, T>());
        ...
    }
}
```
这里看，模板展开后对于 `DoubleRoundOneImpl<round>` 和 `DecimalRoundOneImpl<round>`, call 变为：
```cpp
(TypePair<DataTypeNumber<Float64>, T>())
{
    using Types = std::decay_t<decltype<DataTypeNumber<Float64>, T>()>>
}
```

Double


FunctionImpl = FloatRoundImpl<Float64, Round, Zero>

FloatRoundImpl<Float64, Round, Zero>::apply(column, 0, res_col);
