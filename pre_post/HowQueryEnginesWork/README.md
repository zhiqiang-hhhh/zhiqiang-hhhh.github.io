# Apache Arrow
Apache Arrow 开创了一种对列存数据的内存中的统一表示格式。这种内存格式可以被高效地用于在支持SIMD的CPU上进行数据的向量化处理。
标准化的内存数据格式的好处：
* High-level language 通过将指向数据的指针传递给 Rust 或者 C++ 代码，就可以实现更加高效的数据处理工作。减少数据拷贝。
* 无需序列化的代价，数据便可以在不同的进程之间传递。

# Choosing a Type System
构建一个查询引擎的第一步是构建一个类型系统，用于表示不同的数据类型。
## Row-Based or Columnar?
Query engine 是一行一行地处理数据还是列式处理数据。现今很多查询引擎都是基于火山查询计划的，火山查询计划中的每个步骤中，物理计划将会在数行数据上进行迭代。这种模型实现起来比较简单，但是当行数比较多时，per-row overheads 将会变得非常大。这种代价可以通过把按行迭代改为按batch迭代来降低，不过，如果能够将一个 batch 中的数据都由行式数据转为列式数据，那么我们就可以使用“向量化”（vectorized processing）技术，利用 SIMD （Same Instruction Multiple Data) 来在一条CPU指令里处理多条数据。甚至在未来可以考虑使用GPU来处理这些数据。

## Interoperability
是否支持用户使用其他语言访问查询引擎。

## Type System
使用 Apache Arrow 作为系统类型的基础。在 Apache Arrow 之上抽象出了下面的类

* Schema provides meta data for a data source or the results from a query. A schema consists of one or more fields.
* Field provides the name and data type for a field within a schema.
* FieldVector provides columnar storage for data for a field.
* ArrowType represents a data type.

# Data Sources
## Data Source Interface
在查询规划期间，我们需要知道数据源的 schema，这样查询计划才能够确定目标列存在，并且数据类型是相互兼容的。在有些情况下，我们可能无法获得数据源的 schema，因为数据源本身可能没有固定的schema，这种数据源被称为是 “schema-less"，比如 JSON 文档。

在查询执行期间我们需要从数据源获取数据，并且需要说明我们要将哪些列夹在进内存。

## Dataa Source Examples
* Comma-Separated Values(CSV)
  文本文件，一条记录一行，每个字段用逗号分开。CSV 文件不包含任何的 schema 信息。
* Parquet
  Parquet 的作用是提供压缩后的，高效的，列存数据表达方式，并且是一种 Hadoop 生态系统下非常流行的文件格式。
  Parquet files contain schema information and data is stored in batches (referred to as “row groups”) where each batch consists of columns.
* Orc
  Optimized Row Columnar (ORC) 格式与 Parquet 文件类似。数据以 columnar batch 为单位保存，每个存储单位称为 stripes

# Logical Plans & Expressions
## Printing Logical Plans
## Serialization
## Logical Expressions
Expression that can be evaluated against data at runtime.
|Expression|Examples|
|--|--|
|Literal Value|"hello", 12.34|
|Column Reference|user_id,first_name|
|Math Expression|salary * state_tax|
|Comparison Expression|x >= y|
|Boolean Expression|birthday = today() AND age >= 21|
|Aggregate Expression|MIN(salary),MAX(salary),SUM(salary),AVG(salary),COUNT(*)|
|Scalar Function|CONCAT(first_name, "", last_name)|
|Aliased Expression|salary * 0.02 AS pay_increase|

When we are planning queries we will need to know some basic meta-data about the output of an expression. Specifically we need to have a name for the expression so that other expressions can
reference it and we need to know the data type of the values that the expression will produce when evaluated so that we can validate that the query plan is valid.

## Column Expressions
## Literal Expressions
## Binary Expressions
## Comparison Expressions
## Boolean Expressions
## Math Expressions


## Aggregate Expressions
Aggregate expressions perform an aggregate function such as MIN, MAX, COUNT, SUM, or AVG on an input expression.
## Logical Plans
## Scan
The Scan logical plan represents fetching data from a DataSource with an optional projection. Scan is the only logical plan in our query engine that does not have another logical plan as an input. It is a leaf node in the query tree.