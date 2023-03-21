```plantuml
class Service {
   + {abstract} ~Service()
}
```
thrift service：用户继承 thrift::xxxxServiceIf，实现业务逻辑

thrift processor：apply thrift service on input protocol, return output protocol.

一个 thrfit server 可以有多个 service
```cpp
namespace COMMON {

struct Service {
  virtual ~Service() = default;
};

typedef std::shared_ptr<Service> ServicePtr;

class Server {
 public:
  virtual ~Server() = default;

  virtual int Init() = 0;
  virtual int RegisterService(ServicePtr service) = 0;
  virtual int Start() = 0;
  virtual int Stop() = 0;
  virtual int Join() = 0;
};

typedef std::shared_ptr<Server> ServerPtr;

struct ThriftService : public Service {
 public:
  ThriftService(const std::string& service_name, TProcessPtr processor)
      : service_name(service_name), processor(processor) {}

  std::string service_name;
  TProcessPtr processor;
};

};
```
### common_service.h
```thrift
struct EnableNodeRequest {
  1: string attribute;
}

struct EnableNodeResponse {
  1: i32 code;
  2: string attribute;
}

struct DisableNodeRequest {

}

struct DisableNodeResponse {
  1: i32 code;
}

struct PingRequest {
  1: i64 term; // A self-increasing number, used to make sure the newest response will be accepted
  2: node.NodeId node_id; // Dest node_id
}

struct PingResponse {
  1: i32 code;  // Decided by modules self
  2: node.NodeId node_id; // Indicate the dest node_id
  3: i64 term;
  4: list<disk.DiskLoad> disk_loads; // Will fill diskloads when disk ioload > 5 or disk health status is not ok
}

struct ExecuteCommandRequest {
  1: binary command;
}

struct ExecuteCommandResponse {
  1: i32 code;
  2: optional binary return_message;
}

service CommonService {
  EnableNodeResponse EnableNode(1: EnableNodeRequest request);
  DisableNodeResponse DisableNode(1: DisableNodeRequest request);
  PingResponse Ping(1: PingRequest request);
  ExecuteCommandResponse ExecuteCommand(1: ExecuteCommandRequest request);
}
```

```cpp
namespace NODEMANAGER {
class NodeManagerClient;
}

namespace COMMON {

class CommonServiceImplBase;

class CommonService : public thrift::CommonServiceIf {
 public:
  CommonService(std::shared_ptr<CommonServiceImplBase>);
  ~CommonService() = default;
  void EnableNode(thrift::EnableNodeResponse& response, const thrift::EnableNodeRequest& request) override;
  void DisableNode(thrift::DisableNodeResponse& response, const thrift::DisableNodeRequest& request) override;
  void Ping(thrift::PingResponse& response, const thrift::PingRequest& request) override;
  void ExecuteCommand(thrift::ExecuteCommandResponse& response, const thrift::ExecuteCommandRequest& request) override;

  static std::string GetServiceName();

 private:
  std::shared_ptr<CommonServiceImplBase> common_service_impl_{nullptr};
};

}
```