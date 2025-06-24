```cpp

do {
    std::vector<TabletSharedPtr> tablets_compaction =
                    _generate_compaction_tasks(compaction_type, data_dirs, check_score);
    for (const auto& tablet : tablets_compaction) {
        ...
        } else if (compaction_type == CompactionType::FULL_COMPACTION) {
                    tablet->set_last_full_compaction_schedule_time(UnixMillis());
        }
        ...
        Status st = _submit_compaction_task(tablet, compaction_type, false);
}



StorageEngine::_generate_compaction_tasks
```