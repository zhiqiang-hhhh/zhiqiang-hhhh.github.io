
每个 PlanFragment 里面有一部分用来描述这个 Fragment 有哪些 PlanNode，PlanNode 之间需要有个数据结构去描述他们之间是怎么连接的，这个数据结构就是 `Descriptors.TDescriptorTable desc_tbl`。


```text
fragment id 2
desc_tbl = {
	...
	TSlotDescriptor {
		id = 8
		parent = 4
		scalar_type = 8 // double
		nullIndicatorBit = -1 // not nullable
	},
	...
}

[1] = TExpr {
        01: nodes (list) = list<struct>[1] {
          [0] = TExprNode {
            01: node_type (i32) = 16,
            02: type (struct) = TTypeDesc {
              01: types (list) = list<struct>[1] {
                [0] = TTypeNode {
                  01: type (i32) = 0,
                  02: scalar_type (struct) = TScalarType {
                    01: type (i32) = 8,
                  },
                },
              },
              03: byte_size (i64) = -1,
            },
            04: num_children (i32) = 0,
            15: slot_ref (struct) = TSlotRef {
              01: slot_id (i32) = 8,
              02: tuple_id (i32) = 4,
              03: col_unique_id (i32) = -1,
            },
            20: output_scale (i32) = -1,
            29: is_nullable (bool) = false,
            36: label (string) = "stddev",
          },
        },
      },
```
```plantuml
class DescriptorTbl {
    - Map<TableId, TableDescriptor*> _tbl_desc_map
    - Map<TupleId, TupleDescriptor*> _tuple_desc_map
    - Map<SlotId, SlotDescriptor*> _slot_desc_map
    - std::vector<TTupleId> _row_tuples
}

class TableDescriptor {
    - doris::TTableType::type _table_type;
    - std::string _name;
    - std::string _database;
    - int64_t _table_id;
    - int _num_cols;
    - int _num_clustering_cols;
}

class TupleDescriptor {
    - const TupleId _id;
    - TableDescriptor* _table_desc = nullptr;
    - int _num_materialized_slots;
    - std::vector<SlotDescriptor*> _slots; // contains all slots
    - bool _has_varlen_slots;
    - bool _own_slots = false;
}

class SlotDescriptor {
    - SlotId _id;
    - TypeDescriptor _type;
    - TupleId _parent;
    - int _col_pos;
    - bool _is_nullable;
    - std::string _col_name;
    - std::string _col_name_lower_case;
    - int32_t _col_unique_id;
    - PrimitiveType _col_type;

    // the idx of the slot in the tuple descriptor (0-based).
    // this is provided by the FE
    const int _slot_idx;
    const std::vector<std::string> _column_paths;
    ...
    const std::string _col_default_value;
}

DescriptorTbl *-- TableDescriptor
DescriptorTbl *-- TupleDescriptor
DescriptorTbl *-- SlotDescriptor
```
```cpp
Status DescriptorTbl::create(ObjectPool* pool, const TDescriptorTable& thrift_tbl,
                             DescriptorTbl** tbl) {
    *tbl = pool->add(new DescriptorTbl());

    // deserialize table descriptors first, they are being referenced by tuple descriptors
    for (const auto& tdesc : thrift_tbl.tableDescriptors) {
        TableDescriptor* desc = nullptr;

        switch (tdesc.tableType) {
            ...
            case TTableType::OLAP_TABLE:
                desc = pool->add(new OlapTableDescriptor(tdesc));
                break;
            ...
        }

        (*tbl)->_tbl_desc_map[tdesc.id] = desc;
    }

    for (const TTupleDescriptor& tdesc : thrift_tbl.tupleDescriptors) {
        TupleDescriptor* desc = pool->add(new TupleDescriptor(tdesc));

        // fix up table pointer
        if (tdesc.__isset.tableId) {
            desc->_table_desc = (*tbl)->get_table_descriptor(tdesc.tableId);
            DCHECK(desc->_table_desc != nullptr);
        }

        (*tbl)->_tuple_desc_map[tdesc.id] = desc;
        (*tbl)->_row_tuples.emplace_back(tdesc.id);
    }

    for (const TSlotDescriptor& tdesc : thrift_tbl.slotDescriptors) {
        SlotDescriptor* slot_d = pool->add(new SlotDescriptor(tdesc));
        (*tbl)->_slot_desc_map[tdesc.id] = slot_d;

        // link to parent
        auto entry = (*tbl)->_tuple_desc_map.find(tdesc.parent);

        if (entry == (*tbl)->_tuple_desc_map.end()) {
            return Status::InternalError("unknown tid in slot descriptor msg");
        }
        entry->second->add_slot(slot_d);
    }

    return Status::OK();
}


TupleDescriptor::TupleDescriptor(const TTupleDescriptor& tdesc, bool own_slots)
        : _id(tdesc.id),
          _num_materialized_slots(0),
          _has_varlen_slots(false),
          _own_slots(own_slots) {}

SlotDescriptor::SlotDescriptor(const TSlotDescriptor& tdesc)
        : _id(tdesc.id),
          _type(TypeDescriptor::from_thrift(tdesc.slotType)),
          _parent(tdesc.parent),
          _col_pos(tdesc.columnPos),
          _is_nullable(tdesc.nullIndicatorBit != -1), 
          ... {
            ...
          }
```

```cpp
OperatorXBase::OperatorXBase(ObjectPool* pool, const TPlanNode& tnode, const int operator_id, const DescriptorTbl& descs)
  ... 
  _row_descriptor(descs, tnode.row_tuples, tnode.nullable_tuples),
  ... {
}

RowDescriptor::RowDescriptor(const DescriptorTbl& desc_tbl, const std::vector<TTupleId>& row_tuples,
                             const std::vector<bool>& nullable_tuples)
        : _tuple_idx_nullable_map(nullable_tuples) {
    DCHECK(nullable_tuples.size() == row_tuples.size())
            << "nullable_tuples size " << nullable_tuples.size() << " != row_tuples size "
            << row_tuples.size();
    DCHECK_GT(row_tuples.size(), 0);
    _num_materialized_slots = 0;
    _num_slots = 0;

    for (int row_tuple : row_tuples) {
        TupleDescriptor* tupleDesc = desc_tbl.get_tuple_descriptor(row_tuple);
        _num_materialized_slots += tupleDesc->num_materialized_slots();
        _num_slots += tupleDesc->slots().size();
        _tuple_desc_map.push_back(tupleDesc);
        DCHECK(_tuple_desc_map.back() != nullptr);
    }

    init_tuple_idx_map();
    init_has_varlen_slots();
}
```