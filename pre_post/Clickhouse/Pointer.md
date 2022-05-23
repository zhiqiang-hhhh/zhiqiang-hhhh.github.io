```c++
class MergeTreeData
{
    ...
    using DataPart = IMergeTreeDataPart;
    using DataPartPtr = std::shared_ptr<const DataPart>;
    using MutableDataPartPtr = std::shared_ptr<DataPart>;
    ...
};

```
