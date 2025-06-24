```cpp
Status OlapScanLocalState::hold_tablets() {
    ...
    _tablets.resize(_scan_ranges.size());

    ...
    for (size_t i = 0; i < _scan_ranges.size(); i++) {
            int64_t version = 0;
            std::from_chars(_scan_ranges[i]->version.data(),
                            _scan_ranges[i]->version.data() + _scan_ranges[i]->version.size(),
                            version);
            auto tablet = DORIS_TRY(ExecEnv::get_tablet(_scan_ranges[i]->tablet_id));
            _tablets[i] = {std::move(tablet), version};
        }
}


Status ParallelScannerBuilder::_build_scanners_by_rowid(std::list<ScannerSPtr>& scanners) {
    for (auto&& [tablet, version] : _tablets) {

    }
}
```

```cpp
BetaRowsetReader::get_segment_iterators(...) {
    ...
    _read_options.version = _rowset->version();
}

```



### light schema change

`BaseRowsetBuilder::_build_current_tablet_schema` 这里去更新 tablet schema