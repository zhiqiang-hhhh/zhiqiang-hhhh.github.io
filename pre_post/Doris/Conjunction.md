`select count(CounterID) from hits_10m where CounterID in (4679,75171)`

上述 sql 中 `OLAP_SCAN_NODE` 对应在 BE 上看到的 Plan 精简后如下所示：

```text
[1] = TPlanNode {
          01: node_id = 0,
          02: node_type = OLAP_SCAN_NODE,
          07: conjuncts = {
            [0] = TExpr {
              01: nodes = {
                [0] = TExprNode {
                  01: node_type = IN_PRED,
                  02: TTypeDesc {
                    01: types = {
                      [0] = TTypeNode {
                        02: scalar_type = TScalarType {
                          01: type = BOOLEAN,
                        },
                      },
                    },
                  },
                  03: opcode = FILTER_IN,
                  04: num_children = 3,
                  11: in_predicate (struct) = TInPredicate {
                    01: is_not_in (bool) = false,
                  },
                },
                [1] = TExprNode {
                  01: node_type = SLOT_REF,
                  02: TTypeDesc {
                    01: types {
                      [0] = TTypeNode {
                        02: scalar_type (struct) = TScalarType {
                          01: type = INT,
                        },
                      },
                    },
                  },
                  04: num_children (i32) = 0,
                  15: slot_ref (struct) = TSlotRef {
                    01: slot_id (i32) = 0,
                    02: tuple_id (i32) = 0,
                    03: col_unique_id (i32) = 0,
                  },
                  36: label (string) = "CounterID",
                },
                [2] = TExprNode {
                  01: node_type = INT_LITERAL,
                  02: type (struct) = TTypeDesc {
                    01: types = TScalarType {
                      [0] = TTypeNode {
                        02: scalar_type (struct) = TScalarType {
                          01: type = INT,
                        },
                      },
                    },
                  },
                  04: num_children (i32) = 0,
                  10: int_literal = TIntLiteral {
                    01: value (i64) = 4679,
                  },
                },
                [3] = TExprNode {
                  01: node_type (i32) = INT_LITERAL,
                  02: type (struct) = TTypeDesc {
                    01: types (list) = list<struct>[1] {
                      [0] = TTypeNode {
                        02: scalar_type (struct) = TScalarType {
                          01: type = INT,
                        },
                      },
                    },
                  },
                  04: num_children (i32) = 0,
                  10: int_literal (struct) = TIntLiteral {
                    01: value (i64) = 75171,
                  },
                },
              },
            },
          },
}
```
`Operator` 对 conjunct 的处理是公用的代码如下：
```cpp
Status OperatorXBase::init(const TPlanNode& tnode, RuntimeState* /*state*/) {
    ...
    if (tnode.__isset.vconjunct) {
        vectorized::VExprContextSPtr context;
        RETURN_IF_ERROR(vectorized::VExpr::create_expr_tree(tnode.vconjunct, context));
        _conjuncts.emplace_back(context);
    } else if (tnode.__isset.conjuncts) {
        for (auto& conjunct : tnode.conjuncts) {
            vectorized::VExprContextSPtr context;
            RETURN_IF_ERROR(vectorized::VExpr::create_expr_tree(conjunct, context));
            _conjuncts.emplace_back(context);
        }
    }
    ...
}
```
逻辑很简单，就是把上述的 PlanNode 转成 `OperatorXBase._conjuncts` 数据结构，在 BE 上每个 `_conjuncts` 相当于是一个表达式。



```cpp
template <typename Derived>
Status ScanLocalState<Derived>::_normalize_conjuncts(RuntimeState* state) {
    ...
    for (auto it = _conjuncts.begin(); it != _conjuncts.end();) {
        auto& conjunct = *it;
        // A. If this conjunct has an expression, normally true
        if (conjunct->root()) {
            vectorized::VExprSPtr new_root;
            RETURN_IF_ERROR(_normalize_predicate(conjunct->root(), conjunct.get(), new_root));
            if (new_root) {
                conjunct->set_root(new_root);
                if (_should_push_down_common_expr() &&
                    vectorized::VExpr::is_acting_on_a_slot(*(conjunct->root()))) {
                    _common_expr_ctxs_push_down.emplace_back(conjunct);
                    it = _conjuncts.erase(it);
                    continue;
                }
            } else { // All conjuncts are pushed down as predicate column
                _stale_expr_ctxs.emplace_back(conjunct);
                it = _conjuncts.erase(it);
                continue;
            }
        }
        ++it;
    }
    ...
}

template <typename Derived>
Status ScanLocalState<Derived>::_normalize_predicate(
    const vectorized::VExprSPtr& conjunct_expr_root, vectorized::VExprContext* context,
        vectorized::VExprSPtr& output_expr) {
    // bool is_and_expr() const { return _fn.name.function_name == "and"; }
    static constexpr auto is_leaf = [](auto&& expr) { return !expr->is_and_expr(); };

}
```
先对这里的名词作解释：`conjuncts` 是包含了一组表达式的集合，这个集合中，不会包含 or 表达式，也不会包含括号。FE 在 Plan 阶段会先把 where 条件后的表达式根据语意进行拆分。等到 BE 这里处理的时候，看到的就已经是不包含 `or` 的表达式集合了。比如 `(A and B) or (C and D)` 就是两个 conjunct。

`predicate` 指的是构成 `conjunct` 的表达式。比如 `A and B` 这个 conjunct 就包含两个 predicate。

----
```plantuml
class ColumnValueRange {
  - _column_name : string
  - _column_type : PrimitiveType    // Column type eg: TINYINT,SMALLINT,INT,BIGINT
  - _low_value : CppType ;         // Column's low value, closed interval at left
  - _high_value : CppType ;        // Column's high value, open interval at right
  - _low_op : SQLFilterOp ;
  - _high_op : SQLFilterOp ;
  - _fixed_values : set<CppType> ; // Column's fixed int value
  - _is_nullable_col : bool
  - _contain_null : bool  
  - _precision : int
  - _scale : int
  - _marked_runtime_filter_predicate : bool
  + to_olap_filter(vector<TCondition>& filters) : void
}

class TCondition {
  - column_name : string
  - condition_op : string
  - condition_values : vector<string>
  - column_unique_id : int
  - marked_by_runtime_filter : bool
  - compound_type : TCompoundType
}

class OlapScanKeys {
  - vector<OlapTuple> _begin_scan_keys;
  - vector<OlapTuple> _end_scan_keys;
  - bool _has_range_value;
  - bool _begin_include;
  - bool _end_include;
  - bool _is_convertible;
}

OlapScanKeys *-- OlapTuple

class OlapTuple {
  std::vector<std::string> _values;
  std::vector<bool> _nulls;
}

struct KeyRange {
  - const RowCursor* lower_key = nullptr;
  - bool include_lower;
  - const RowCursor* upper_key = nullptr;
  - bool include_upper;
}

class StorageReadOptions {
  - std::vector<KeyRange> key_ranges;
}

StorageReadOptions *-right- KeyRange

class SegmentIterator {
  StorageReadOptions _opts;
  std::vector<ColumnPredicate*> _col_predicates;
  Status SegmentIterator::init(const StorageReadOptions& opts);
}

class BetaRowsetReader {
  - RowsetReaderContext* _read_context
  - std::unique_ptr<RowwiseIterator> _iterator;
  - StorageReadOptions _read_options;
}

BetaRowsetReader *-down- StorageReadOptions
BetaRowsetReader *-down- RowwiseIterator
SegmentIterator -up-> RowwiseIterator
SegmentIterator *-right- StorageReadOptions
```

```cpp
BetaRowsetReader::_init_iterator()


/// Here do not use the argument of `opts`,
/// see where the iterator is created in `BetaRowsetReader::get_segment_iterators`
Status LazyInitSegmentIterator::init(const StorageReadOptions& /*opts*/) {
    _need_lazy_init = false;
    if (_inner_iterator) {
        return Status::OK();
    }

    RETURN_IF_ERROR(_segment->new_iterator(_schema, _read_options, &_inner_iterator));
    return _inner_iterator->init(_read_options);
}
```