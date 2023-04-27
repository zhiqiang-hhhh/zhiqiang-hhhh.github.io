## Goalng Client
client 的内存中为每个 module 维护一个 deploy_tree
```golang
type deployTreeCache struct {
	module_deploy_tree_map map[model.ModuleId]deployTreeCacheEntry
	cache_mutex            sync.RWMutex
}
```
```plantuml
@startuml
class DeployNode {
    + node_id : NodeId
    + endpoint : Endpoint
    + deploy_path : DeployPath
    + deploy_status : DeployStatus
    + total_size : int64
    + used_size : int64
    + disk_statistic_map : map<disk.DiskTag, i64>
    + service_status : ServiceStatus
    + node_meta : map<string, string> # user define
}

class AvailabilityZone {
    + az_name : string
    + mz_map : map<int8, ManagementZone>
    + total_size : int64
    + used_size : int64
    + disk_statistic_map : map<disk.DiskTag, i64>
}

DeployTree *-- AvailabilityZone

class ManagementZone {
    + mz_id : int8
    + node_map : map<NodeId, DeployNode>
    + total_size : int64
    + used_size : int64
    + disk_statistic_map : map<disk.DiskTag, i64>
}

AvailabilityZone *--  ManagementZone
ManagementZone *--  DeployNode 

class DeployTree {
    + deploy_tree_id : string
    + module_id : ModuleId
    + description : string
    + region : string
    + az_map : map<string, AvailabilityZone>
    + total_size : int64
    + used_size : int64
    + disk_statistic_map : map<disk.DiskTag, i64>
}

@enduml

```
nmc 启动时从 nms 获取deploy tree
```cpp
int NodeManagerClientCache::PullModule(MODEL::ModuleId::type module_id) {
    /// A. 根据 module id 获取 deploy tree id
  std::string deploy_tree_id;
  int ret = node_manager_client_impl_->GetDeployTreeId(module_id, &deploy_tree_id);
  
  ...

  WriteLock wlock(deploy_tree_cache_.module_deploy_tree_mutex);
  auto& deploy_tree_cache_entry = deploy_tree_cache_.module_deploy_tree_map[module_id];
  deploy_tree_cache_entry.module_id = module_id;
  deploy_tree_cache_entry.deploy_tree_id = deploy_tree_id;
  deploy_tree_cache_entry.version = -1;
  deploy_tree_cache_entry.rpc_mutex.lock();
  wlock.unlock();

    /// B. 执行 rpc 操作，从 nms 获取 deploy tree
  ret = PullDeployTreeNoLock(module_id, deploy_tree_id, -1);
  if (ret != kSuccess) {
    NM_LOGF(NM_WARN, "PullDeployTreeNoLock failed, ret = {} deploy_tree_id = {}, module_id = {}",
             GetErrorCodeStr(ret), deploy_tree_id, module_id);
  }

  deploy_tree_cache_entry.rpc_mutex.unlock();
  return ret;
}

```

```plantuml
@startuml
participant client
participant server

client -> server : GetLatestVersionsAndCheck
activate client
server -> client : ModuleIds -> DeployTreeIds


client -> server : ModuleId, DeployTreeId

server -> client : DeployTree

deactivate client

client -> client : cache start
activate client

deactivate client

@enduml
```

```plantuml
interface TClient {
  + error Call(Context, method_str, args, result)
}
class NodeManagerServiceClient {
  + c thrfit.TClient
}

NodeManagerServiceClient *-- TClient

interface NodeManagerService {
  + (ListHostsResponse, error) ListHosts(context) 
  + ... xxx (...)
}
```