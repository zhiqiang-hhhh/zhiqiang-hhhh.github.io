```plantuml
class BaseCompaction {
    Status prepare_compact() override;

    Status execute_compact() override;
}
class CompactionMixin
class SingleReplicaCompaction
class Compaction {
    # Status prepare_compact()
    # Status execute_compact()
}


CompactionMixin -up-> Compaction
BaseCompaction -up-> CompactionMixin
SingleReplicaCompaction -up-> CompactionMixin
```

```cpp
StorageEngine::_submit_compaction_task(...) {
    Status st = Tablet::prepare_compaction_and_calculate_permits(compaction_type, tablet,
                                                                 compaction, permits);
    ...
    auto status = thread_pool->submit_func([tablet, compaction = std::move(compaction),
                                                compaction_type, permits, force, this]() {
                                                    ...});
}

Tablet::prepare_compaction_and_calculate_permits (...) {
    compaction = std::make_shared<CumulativeCompaction>(tablet->_engine, tablet);
    Status res = compaction->prepare_compact();
}
```