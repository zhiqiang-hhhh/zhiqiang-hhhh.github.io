# Doris Heartbeats
心跳管理是Doris实现集群管理的基础。
相关的Thrift 定义：
```thrift
...
struct TMasterInfo {
    1: required Types.TNetworkAddress network_address
    2: required Types.TClusterId cluster_id
    3: required Types.TEpoch epoch
    4: optional string token 
    5: optional string backend_ip
    6: optional Types.TPort http_port
    7: optional i64 heartbeat_flags
    8: optional i64 backend_id
}

struct TBackendInfo {
    1: required Types.TPort be_port
    2: required Types.TPort http_port
    3: optional Types.TPort be_rpc_port
    4: optional Types.TPort brpc_port
    5: optional Types.TPort ch_tcp_port
    6: optional Types.TPort ch_http_port
    7: optional string version
    8: optional i64 be_start_time
}

struct THeartbeatResult {
    1: required Status.TStatus status 
    2: required TBackendInfo backend_info
}

service HeartbeatService {
    THeartbeatResult heartbeat(1:TMasterInfo master_info);
}
```
从HeartbeatService的定义可以看出来，heartbeat服务的调用者是FE，而执行者是BE。

```thrift
struct TFrontendPingFrontendResult {
    1: required TFrontendPingFrontendStatusCode status
    2: required string msg
    3: required i32 queryPort
    4: required i32 rpcPort
    5: required i64 replayedJournalId
    6: required string version
}

struct TFrontendPingFrontendRequest {
   1: required i32 clusterId
   2: required string token
}

service FrontendService{
    ...
    TFrontendPingFrontendResult ping(1: TFrontendPingFrontendRequest request)
    ...
}
```
同时FE在自己的Service中还提供了ping方法。明显该方法用于FE之间的心跳探测。

## FE
我们先从FE开始看，需要关注：
1. FE 何时调用 hearbeat 服务
2. BE 信息是如何被加入到 FE 元数据中的

```java
    public void addBackends(String tenantName, String clusterName, List<Pair<String, Integer>> hosts, int shardId) throws UserException {
    	// add backends to system info service to track heartbeat and report version info
    	systemInfoService.addBackends(hosts, true, tenantName);
    	CHTenant chTenant = nameToTenant.get(tenantName);
    	...
    	CHCluster chCluster = chTenant.getCluster(clusterName);
    	...
    	for (int i = 0; i < hosts.size(); ++i) {
    	    Backend backend = systemInfoService.getBackendByHostPort(tenantName, hosts.get(i));
    	    if (backend == null) {
              throw new UserException("Could not find backend " + hosts.get(i));
            }
            chCluster.addBackendToShard(shardId, backend.getId());
            editLog.logAddCHBackend(new BackendOpLog(tenantName, clusterName, backend.getId(), shardId));
    	}
    }

    public void dropBackends(String tenantName, String clusterName, List<Pair<String, Integer>> hosts) throws UserException {
    	// Not 
    	CHTenant chTenant = nameToTenant.get(tenantName);
    	if (chTenant == null) {
    		throw new UserException("Tenant " + tenantName + " not exist");
    	}
    	CHCluster chCluster = chTenant.getCluster(clusterName);
    	if (chCluster == null) {
    		throw new UserException("Cluster " + clusterName + " not exist");
    	}

    	for (int i = 0; i < hosts.size(); ++i) {
    		Backend backend = systemInfoService.getBackendByHostPort(tenantName, hosts.get(i));
    		if (backend == null) {
        		continue;
    		}
        	chCluster.dropBackend(backend.getId());
        	// TODO(ygl) check if other cluster have this backend, if not then remove it from system info service
        	editLog.logDropCHBackend(new BackendOpLog(tenantName, clusterName, backend.getId(), -1));
        }
    }
```