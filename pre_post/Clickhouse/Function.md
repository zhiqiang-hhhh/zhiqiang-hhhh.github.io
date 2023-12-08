#### IFunctionBase

```cpp
/** Function with known arguments and return type (when the specific overload was chosen).
  * It is also the point where all function-specific properties are known.
  */
class IFunctionBase : public IResolvedFunction
{
public:
    ~IFunctionBase() override = default;

    virtual ColumnPtr execute( /// NOLINT
        const ColumnsWithTypeAndName & arguments, const DataTypePtr & result_type, size_t input_rows_count, bool dry_run = false) const
    {
        return prepare(arguments)->execute(arguments, result_type, input_rows_count, dry_run);
    }

    /// Get the main function name.
    virtual String getName() const = 0;

    const Array & getParameters() const final
    {
        throw Exception(ErrorCodes::NOT_IMPLEMENTED, "IFunctionBase doesn't support getParameters method");
    }

    /// Do preparations and return executable.
    /// sample_columns should contain data types of arguments and values of constants, if relevant.
    virtual ExecutableFunctionPtr prepare(const ColumnsWithTypeAndName & arguments) const = 0;
    ...
}
```

每次 `IFunctionBase::execute` 被执行时都会创建一个 IExecutableFunction，执行 `IExecutableFunction::execute` 方法


#### IExecutableFunction
```cpp
/// The simplest executable object.
/// Motivation:
///  * Prepare something heavy once before main execution loop instead of doing it for each columns.
///  * Provide const interface for IFunctionBase (later).
///  * Create one executable function per thread to use caches without synchronization (later).
class IExecutableFunction
{
public:

    virtual ~IExecutableFunction() = default;

    /// Get the main function name.
    virtual String getName() const = 0;

    ColumnPtr execute(const ColumnsWithTypeAndName & arguments, const DataTypePtr & result_type, size_t input_rows_count, bool dry_run) const;

protected:

    virtual ColumnPtr executeImpl(const ColumnsWithTypeAndName & arguments, const DataTypePtr & result_type, size_t input_rows_count) const = 0;

    virtual ColumnPtr executeDryRunImpl(const ColumnsWithTypeAndName & arguments, const DataTypePtr & result_type, size_t input_rows_count) const
    {
        return executeImpl(arguments, result_type, input_rows_count);
    }

    /** Default implementation in presence of Nullable arguments or NULL constants as arguments is the following:
      *  if some of arguments are NULL constants then return NULL constant,
      *  if some of arguments are Nullable, then execute function as usual for columns,
      *   where Nullable columns are substituted with nested columns (they have arbitrary values in rows corresponding to NULL value)
      *   and wrap result in Nullable column where NULLs are in all rows where any of arguments are NULL.
      */
    virtual bool useDefaultImplementationForNulls() const { return true; }

    /** Default implementation in presence of arguments with type Nothing is the following:
      *  If some of arguments have type Nothing then default implementation is to return constant column with type Nothing
      */
    virtual bool useDefaultImplementationForNothing() const { return true; }

    /** If the function have non-zero number of arguments,
      *  and if all arguments are constant, that we could automatically provide default implementation:
      *  arguments are converted to ordinary columns with single value, then function is executed as usual,
      *  and then the result is converted to constant column.
      */
    virtual bool useDefaultImplementationForConstants() const { return false; }

    /** If function arguments has single low cardinality column and all other arguments are constants, call function on nested column.
      * Otherwise, convert all low cardinality columns to ordinary columns.
      * Returns ColumnLowCardinality if at least one argument is ColumnLowCardinality.
      */
    virtual bool useDefaultImplementationForLowCardinalityColumns() const { return true; }

    /** If function arguments has single sparse column and all other arguments are constants, call function on nested column.
      * Otherwise, convert all sparse columns to ordinary columns.
      * If default value doesn't change after function execution, returns sparse column as a result.
      * Otherwise, result column is converted to full.
      */
    virtual bool useDefaultImplementationForSparseColumns() const { return true; }

    /** Some arguments could remain constant during this implementation.
      */
    virtual ColumnNumbers getArgumentsThatAreAlwaysConstant() const { return {}; }

    /** True if function can be called on default arguments (include Nullable's) and won't throw.
      * Counterexample: modulo(0, 0)
      */
    virtual bool canBeExecutedOnDefaultArguments() const { return true; }

private:

    ColumnPtr defaultImplementationForConstantArguments(
            const ColumnsWithTypeAndName & args, const DataTypePtr & result_type, size_t input_rows_count, bool dry_run) const;

    ColumnPtr defaultImplementationForNulls(
            const ColumnsWithTypeAndName & args, const DataTypePtr & result_type, size_t input_rows_count, bool dry_run) const;

    ColumnPtr defaultImplementationForNothing(
            const ColumnsWithTypeAndName & args, const DataTypePtr & result_type, size_t input_rows_count) const;

    ColumnPtr executeWithoutLowCardinalityColumns(
            const ColumnsWithTypeAndName & args, const DataTypePtr & result_type, size_t input_rows_count, bool dry_run) const;

    ColumnPtr executeWithoutSparseColumns(
            const ColumnsWithTypeAndName & arguments, const DataTypePtr & result_type, size_t input_rows_count, bool dry_run) const;
};
```
`IExecutableFunction` 的声明如上，关注其 execute 方法
每个 Function 在 execute 的时候，针对不同类型的列，有不同的实现。比如，有个函数实现针对 ColumnConst 做了优化，有个函数则没有优化，那么对于做了优化的函数，我们就会直接把 ColumnConst 传递给它的具体实现(defaultImpl)，那么对于没有优化的函数，我们就需要把 ColumnConst 转为普通列（FullColumn），然后再把普通列传递给其具体的执行方法。
在这个过程中，首先处理 SparseColumn
* TODO:What is sparse column, why it is processed first
```cpp
ColumnPtr IExecutableFunction::execute(const ColumnsWithTypeAndName & arguments, const DataTypePtr & result_type, size_t input_rows_count, bool dry_run) const
{
    if (useDefaultImplementationForSparseColumns())
    {
        ...
        convertSparseColumnsToFull(columns_without_sparse);
        return executeWithoutSparseColumns(columns_without_sparse, result_type, input_rows_count, dry_run);
    } 
    
    return executeWithoutSparseColumns(arguments, result_type, input_rows_count, dry_run);
}
```
检查上述代码的逻辑，`useDefaultImplementationForSparseColumns()` 其实是控制我们是否需要进行 `convertSparseColumnsToFull` 这一步。
所以当该函数返回为 true 时，表示 `IExecutableFunction` 的实现无法直接处理 `SparseColumn`，需要在执行函数之前将输入的 `Column`` 转为 `FullColumn`。
因此也许该函数名应该改为 `canProcessSparseColumnsDirectly()`，实现变为
```cpp
ColumnPtr IExecutableFunction::execute(const ColumnsWithTypeAndName & arguments, const DataTypePtr & result_type, size_t input_rows_count, bool dry_run) const
{
    if (canProcessSparseColumnsDirectly())
    {
        return executeWithoutSparseColumns(arguments, result_type, input_rows_count, dry_run);
    }

    ...

    convertSparseColumnsToFull(columns_without_sparse);
    return executeWithoutSparseColumns(columns_without_sparse, result_type, input_rows_count, dry_run);
}
```
感觉上述的逻辑更加清楚一些？
在处理完 SparseColumn 之后，继续处理 LowCardinalityColumn
```cpp
ColumnPtr IExecutableFunction::executeWithoutSparseColumns(const ColumnsWithTypeAndName & arguments, const DataTypePtr & result_type, size_t input_rows_count, bool dry_run) const
{
    ColumnPtr result;
    if (useDefaultImplementationForLowCardinalityColumns())
    {
        // 到这个分支说明该函数的实现不能直接作用在 LowCardinalityColumns 上，需要进行转换
        ColumnsWithTypeAndName columns_without_low_cardinality = arguments;
        if (const auto * res_low_cardinality_type = typeid_cast<const DataTypeLowCardinality *>(result_type.get()))
        {
            ...
        }
        else
        {
            convertLowCardinalityColumnsToFull(columns_without_low_cardinality);
            result = executeWithoutLowCardinalityColumns(columns_without_low_cardinality, result_type, input_rows_count, dry_run);
        }
    }
    else
    {
        result = executeWithoutLowCardinalityColumns(arguments, result_type, input_rows_count, dry_run);
    }
    return result;
}
```
* TODO: What is low cardinality column, why it is processed like this?

在对 LowCardinalityColumn 完成转换之后，继续处理 NullColumn & ConstColumn
```cpp
ColumnPtr IExecutableFunction::executeWithoutLowCardinalityColumns(
    const ColumnsWithTypeAndName & args, const DataTypePtr & result_type, size_t input_rows_count, bool dry_run) const
{
    if (auto res = defaultImplementationForNothing(args, result_type, input_rows_count))
        return res;

    if (auto res = defaultImplementationForConstantArguments(args, result_type, input_rows_count, dry_run))
        return res;

    if (auto res = defaultImplementationForNulls(args, result_type, input_rows_count, dry_run))
        return res;

    ColumnPtr res;
    if (dry_run)
        res = executeDryRunImpl(args, result_type, input_rows_count);
    else
        res = executeImpl(args, result_type, input_rows_count);

    if (!res)
        throw Exception(ErrorCodes::LOGICAL_ERROR, "Empty column was returned by function {}", getName());

    return res;
}
```
不过这里的实现没有再像之前那样写了多个 if 判断，而是把 `if(useDefaultImpleForXXX)` 的调用放在了 `defaultImpleForXXX` 里，比如 ColumnConst：
```cpp {.line-numbers}
ColumnPtr IExecutableFunction::defaultImplementationForConstantArguments(
    const ColumnsWithTypeAndName & args, const DataTypePtr & result_type, size_t input_rows_count, bool dry_run) const
{
    ColumnNumbers arguments_to_remain_constants = getArgumentsThatAreAlwaysConstant();
    ...
    if (args.empty() || !useDefaultImplementationForConstants() || !allArgumentsAreConstants(args))
        return nullptr;

    ...
}
```
这里第 4 行的逻辑比较绕。
如果入参column为空，或者当前函数实现的`IExecutableFunction::executeImpl`能够直接作用在 `ConstantArguments` 上，或者当前的入参并不全都是 ColumnConst，那么直接返回。直接返回的含义是去执行`IExecutableFunction::executeImpl`。
否则，说明当前函数不能直接处理 `ColumnConst`，我们需要进行转换。
```cpp {.line-numbers}
ColumnPtr IExecutableFunction::defaultImplementationForConstantArguments(
    const ColumnsWithTypeAndName & args, const DataTypePtr & result_type, size_t input_rows_count, bool dry_run) const
{
    ColumnNumbers arguments_to_remain_constants = getArgumentsThatAreAlwaysConstant();

    /// Check that these arguments are really constant.
    for (auto arg_num : arguments_to_remain_constants)
        if (arg_num < args.size() && !isColumnConst(*args[arg_num].column))
            throw Exception(ErrorCodes::ILLEGAL_COLUMN,
                "Argument at index {} for function {} must be constant",
                arg_num,
                getName());

    if (args.empty() || !useDefaultImplementationForConstants() || !allArgumentsAreConstants(args))
        return nullptr;

    ColumnsWithTypeAndName temporary_columns;
    bool have_converted_columns = false;

    size_t arguments_size = args.size();
    temporary_columns.reserve(arguments_size);
    for (size_t arg_num = 0; arg_num < arguments_size; ++arg_num)
    {
        const ColumnWithTypeAndName & column = args[arg_num];

        if (arguments_to_remain_constants.end() != std::find(arguments_to_remain_constants.begin(), arguments_to_remain_constants.end(), arg_num))
        {
            temporary_columns.emplace_back(ColumnWithTypeAndName{column.column->cloneResized(1), column.type, column.name});
        }
        else
        {
            have_converted_columns = true;
            temporary_columns.emplace_back(
                ColumnWithTypeAndName{assert_cast<const ColumnConst *>(column.column.get())->getDataColumnPtr(), column.type, column.name });
        }
    }
 
    /** When using default implementation for constants, the function requires at least one argument
      *  not in "arguments_to_remain_constants" set. Otherwise we get infinite recursion.
      */
    if (!have_converted_columns)
        throw Exception(ErrorCodes::NUMBER_OF_ARGUMENTS_DOESNT_MATCH,
            "Number of arguments for function {} doesn't match: the function requires more arguments",
            getName());

    ColumnPtr result_column = executeWithoutLowCardinalityColumns(temporary_columns, result_type, 1, dry_run);

    /// extremely rare case, when we have function with completely const arguments
    /// but some of them produced by non isDeterministic function
    if (result_column->size() > 1)
        result_column = result_column->cloneResized(1);

    return ColumnConst::create(result_column, input_rows_count);
}
```
* L4：如果当前函数有参数必须为 Const，那么就会得到一个 ColumnNumbers 记录其应该为 InputBlock 的第几列
* L7~L12：确保必须为 Const 的 Column 在 InputBlock 中确实为 Const
* L14: 如果输入的列为空，或者函数要求不使用默认转换，或者输入列中包含非const列，那么直接返回
否则，将会对输入的Column进行转换
* L26: 如果某个列被当前函数期望为 Const，那么创建一个行数为 1 的普通列
* L34: 如果函数并没有严格要求某个列为 ColumnConst，那么将其转为一个行数为 1 的普通列
* L46: 把所有的列都转为 Const 之后，把临时 Block 传给 executeWithoutLowCardinalityColumns

`executeWithoutLowCardinalityColumns`实现如下
```cpp {.line-numbers}
ColumnPtr IExecutableFunction::executeWithoutLowCardinalityColumns(
    const ColumnsWithTypeAndName & args, const DataTypePtr & result_type, size_t input_rows_count, bool dry_run) const
{
    if (auto res = defaultImplementationForNothing(args, result_type, input_rows_count))
        return res;

    if (auto res = defaultImplementationForConstantArguments(args, result_type, input_rows_count, dry_run))
        return res;

    if (auto res = defaultImplementationForNulls(args, result_type, input_rows_count, dry_run))
        return res;

    ColumnPtr res;
    if (dry_run)
        res = executeDryRunImpl(args, result_type, input_rows_count);
    else
        res = executeImpl(args, result_type, input_rows_count);

    if (!res)
        throw Exception(ErrorCodes::LOGICAL_ERROR, "Empty column was returned by function {}", getName());

    return res;
}
```
注意第 7 行会再次调用 `defaultImplementationForConstantArguments`，当我们再次执行该函数时，我们传入的 args 是之前创建的 temporary_columns，注意当时构造该变量的时候，对于函数要求必须为 Const 的列，执行的是
```cpp
temporary_columns.emplace_back(ColumnWithTypeAndName{column.column->cloneResized(1), column.type, column.name});
```
因此这些列依然是 ColumnConst，对于函数并没有要求一定为 Const 的列，我们执行的是
```cpp
temporary_columns.emplace_back(
                ColumnWithTypeAndName{assert_cast<const ColumnConst *>(column.column.get())->getDataColumnPtr(), column.type, column.name });
```
这些列将会在 temporary_columns 失去 Const 属性。

### defaultImplementationForNulls
对 Null 值的默认处理
```cpp {.null-numbers}
ColumnPtr IExecutableFunction::defaultImplementationForNulls(
    const ColumnsWithTypeAndName & args, const DataTypePtr & result_type, size_t input_rows_count, bool dry_run) const
{
    if (args.empty() || !useDefaultImplementationForNulls())
        return nullptr;

    NullPresence null_presence = getNullPresense(args);
    ...
}

NullPresence getNullPresense(const ColumnsWithTypeAndName & args)
{
    NullPresence res;

    for (const auto & elem : args)
    {
        res.has_nullable |= elem.type->isNullable();
        res.has_null_constant |= elem.type->onlyNull();
    }

    return res;
}

ColumnPtr IExecutableFunction::defaultImplementationForNulls(
    const ColumnsWithTypeAndName & args, const DataTypePtr & result_type, size_t input_rows_count, bool dry_run) const
{
    ...
    
    if (null_presence.has_null_constant)
    {
        // Default implementation for nulls returns null result for null arguments,
        // so the result type must be nullable.
        if (!result_type->isNullable())
            throw Exception(
                ErrorCodes::LOGICAL_ERROR,
                "Function {} with Null argument and default implementation for Nulls "
                "is expected to return Nullable result, got {}",
                getName(),
                result_type->getName());

        return result_type->createColumnConstWithDefaultValue(input_rows_count);
    }
}
```
如果某个列是 NullConst，那么默认情况下将会返回得到一个 NullConst 列
```cpp
if (null_presence_.has_nullable)
{
    ColumnsWithTypeAndName temporary_columns = createBlockWithNestedColumns(args);
    auto temporary_result_type = removeNullable(result_type);

    auto res = executeWithoutLowCardinalityColumns(temporary_columns, temporary_result_type, input_rows_count, dry_run);
    return wrapInNullable(res, args, result_type, input_rows_count);
}
```
如果某个列为 nullable，那么默认情况下会把该列的 Nullable 属性去掉，然后再递归执行 `executeWithoutLowCardinalityColumns` 