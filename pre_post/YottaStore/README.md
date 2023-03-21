### Cloud Object Storage

Vid - Volume ID，由Allocator分配
Vbid - Volume 内代表 blob 的 ID
VletId：Vid+Seq（唯一代表历史上的一个vlet，seq始终加）
Bid: Vid + Vbid
Vlet Index: Vlet 的 shard index

SpaceId to ClusterId
10280(6666): [15183,15184,15185,15186,15191,15192,15193,15194,15204,15205,15206,15211,15212,15213,15214,15234,15243,15244,15522,15523,15784,15785,15937,16428,16429,16430,16431,16470]


#### Columbus
```
struct Cluster {
    1. cluster_id uint64;
    2. cluster_mata {
       1. region string;
       2. address string; // NodeManager ServiceName
    };
    3. space_id_list list<SpaceId>
}
```

map<ClusterId, Cluster>

#### NodeManager
