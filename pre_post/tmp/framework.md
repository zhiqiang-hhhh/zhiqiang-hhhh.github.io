[TOC]
# NodeFrame 使用指南

NodeFrame 设计实现的背景：
1. 现有整个YottaStore系统内，在多个不同的module中，对于基础组件的使用有相似的重复性的代码，希望通过NodeFrame来将这部分代码统一实现，减少后续新系统的开发成本
2. 其他COS生态下的模块（比如DPS？）在未来与YottaStore系统混合部署时，可能也会希望使用YottaStore的一些基础架构，同样通过NodeFrame来帮助他们快速接入。


前面提到多个不同module对于基础组件有相似的使用模式。NodeFrame研究了各个module的对基础组件的使用方式后，按照使用模式划分出了4个NodeFrame的[使用者角色](https://iwiki.woa.com/pages/viewpage.action?pageId=4007971145)。

|用户角色|NodeManagerClient|NodeReceptor|Audience|NodeOperator|NodeMonitorLib|Cayman(client/listener)|Mutong|BroadcasterClient|
|--|--|--|--|--|--|--|--|--|
|访客|Guest|无|无|无|无|无|无|无|
|注册用户|User|必有|按需|无|按需|按需|按需|按需|按需|
|管理员|Master|必有|按需|必有|按需|按需|按需|按需|按需|
|超级管理员|Admin|必有|按需|必有|按需|按需|按需|按需|按需|

本实用指南从代码实战角度进行使用介绍，对于各个基础服务的作用与功能，参考[服务框架概要设计](https://iwiki.woa.com/pages/viewpage.action?pageId=4007971145)。

## BlobNode 改造实战

BlobNode 是 yotta store 的核心单机存储模块，使用到了众多的基础服务组件。以BlobNode主进程启动作为例子解释服务框架的作用。（注释：早期版本，细节可能不一致）

https://git.woa.com/qiangfu/ServiceFrameworkExample/merge_requests/1?view=parallel

总结BlobNode使用服务框架的例子：
1. 命令行解析 和 信号处理函数均通过调用服务框架的公共函数实现
2. NodeNamangerClient 以及 CommmonServiceServer 的初始化在`FRAMEWORK::Error err = FRAMEWORK::Init(framework_config_path, node_receptpr);`中完成
3. 其他的基础组件server由用户按需启动。
4. 增加新的framework_user.yaml配置文件

## 单独的 user example
https://git.woa.com/YottaBase/NodeFrame/blob/master/example/user

选择 user 角色作为 example 是因为系统中的绝大多数module都是user角色。

```cpp
/***************************************************************************
 * Copyright (c) 2023 Tencent.com, Inc. All Rights Reserved.
 *
 * Description :
 *  Created on : 2023-04-06
 *      Author : roanhe
 **************************************************************************/

#include <csignal>
#include <iostream>
#include <memory>
#include <string>

#include "common/common_enums/msg_type.h"
#include "framework.h"
#include "framework_error.h"
#include "log.h"
#include "node_manager_client.h"

/// A. 作为 user，需要使用 NodeReceptor 完成节点上下架操作
class ExampleNodeReceptor : public FRAMEWORK::NodeReceptor {
 public:
  void EnableNode(FRAMEWORK::EnableNodeResponse& _return, const FRAMEWORK::EnableNodeRequest& request) override {
    request.printTo(std::cout);
  }
  void DisableNode(FRAMEWORK::DisableNodeResponse& _return, const FRAMEWORK::DisableNodeRequest& request) override {
    request.printTo(std::cout);
  }
  void Ping(FRAMEWORK::PingResponse& _return, const FRAMEWORK::PingRequest& request) override {
    request.printTo(std::cout);
  }
  void ExecuteCommand(FRAMEWORK::ExecuteCommandResponse& _return,
                      const FRAMEWORK::ExecuteCommandRequest& request) override {
    request.printTo(std::cout);
  }
};

/// B. 传统 nodemonitor client 在新的namespace环境下的平替
class UserNamespaceWarenessEventReceiver : public NODEMONITOR::NamespaceAwarenessEventReceiver {
 public:
  UserNamespaceWarenessEventReceiver(int cluster_id, NODEMANAGER::Namespace namespace_id,
                                     std::shared_ptr<COMMON::NodeHealthBoard> node_health_board)
      : node_health_board_(node_health_board), NamespaceAwarenessEventReceiver(cluster_id, namespace_id) {}

  void OnBlackEvent(MODEL::NodeId node_id, bool is_black, bool timeout) override {
    if (is_black) {
      node_health_board_->Black(node_id);
    } else {
      node_health_board_->UnBlack(node_id);
    }
  }

  void OnNodeEvent(MODEL::NodeId node_id, MODEL::HealthStatus::type status, bool timeout) override {
    node_health_board_->Set(node_id, status);
  }

 private:
  std::shared_ptr<COMMON::NodeHealthBoard> node_health_board_;
};

int main(int argc, char** argv) {
  // ============================Argument parser============================
  std::string framework_config_path("./framework_user.yaml");
  auto parser = FRAMEWORK::GetArgumentParser();
  assert(parser->Register("config", 'c', "", &framework_config_path) == 0);
  assert(parser->Register("version", 'v', UTILS::DefaultDisplayVersion) == 0);
  assert(parser->Register("dependence", 'd', UTILS::DefaultDependenceList) == 0);
  assert(parser->ParseArgument(argc, argv) == 0);
  if (int ret = parser->ParseArgument(argc, argv); ret != kSuccess) {
    std::cout << "Parse argument failed";
    return -1;
  }

  // ============================Log============================
  // log instance 初始化之后，所有subclient的日志都会正常打印
  LOGGER::LogInstance normal_log_instance;
  LOGGER::LogConfig normal_log_config("framework");
  normal_log_config.log_dir = "./log";
  int ret = normal_log_instance.InitNormalLog(normal_log_config);
  if (ret != kSuccess) {
    std::cout << "InitNormalLog failed, ret: " << ret << std::endl;
    return -1;
  }

  // ============================SignalHandler============================
  UTILS::SigHandler sigterm_handler = [](int x) { return; };
  auto signal_handler = FRAMEWORK::GetSignalHandler();
  signal_handler->AddSignalHandler({SIGTERM}, sigterm_handler);
  signal_handler->Run();

  // ============================Framework init============================
  /// C. Framework Init，完成 node manager client + node receptor 的启动
  /// 并且完成服务框架配置文件的解析
  auto node_receptpr = std::make_shared<ExampleNodeReceptor>();
  // 非 Guest 模式下，一定会启动 NodeReceptorServer，不需要用户显式 InitNodeReceptor
  FRAMEWORK::Error err = FRAMEWORK::Init(framework_config_path, node_receptpr);

  if (err.code != kSuccess) {
    std::cout << err << std::endl;
    return -1;
  }

  // =========================Set common protocol node id===============================
  MODEL::NodeId node_id;
  ret = FRAMEWORK::GetNodeManagerClient()->GetMyNodeId(&node_id);
  apache::thrift::protocol::setNodeId(node_id);
  MODEL::DeployStatus::type deploy_status;
  ret = FRAMEWORK::GetNodeManagerClient()->GetMyServerDeployStatus(&deploy_status);

  // ============================Audience============================
  /// D. Audience 的使用例子
  FRAMEWORK::SetNodeHealthBoard(std::make_shared<COMMON::NodeHealthBoard>());
  std::string cluster_id = FRAMEWORK::GetFrameworkConfig().cluster_id;
  NODEMANAGER::Namespace user_namespace = FRAMEWORK::GetFrameworkConfig().user_namespace;
  auto event_receiver = std::make_shared<UserNamespaceWarenessEventReceiver>(std::stoi(cluster_id), user_namespace,
                                                                             FRAMEWORK::GetNodeHealthBoard());
  auto error = FRAMEWORK::AddRegularEventReceiver(COMMON::MsgType::kNodeMonitorClassicMessage, event_receiver);

  FRAMEWORK::AudienceMsgHandlerMapPtr audience_handler_map = std::make_shared<FRAMEWORK::AudienceMsgHandlerMap>();
  error = FRAMEWORK::AddMsgHandlerMap(audience_handler_map);

  if (error.code != kSuccess) {
    std::cout << error << std::endl;
    return -1;
  }

  error = FRAMEWORK::StartAudienceServer();
  if (error.code != kSuccess) {
    std::cout << error << std::endl;
    return -1;
  }

  // ============================WatchService============================
  /// E. WatchService 需要用户传入 WatchService 的实现，因此需要用户显示启动
  uint64_t sample_interval_s = 100;  // 非典型
  auto metric_manager = std::make_shared<COMMON::MetricManager>(sample_interval_s);
  auto watch_service = std::make_shared<COMMON::WatchService>(metric_manager);
  error = FRAMEWORK::InitWatchService(watch_service);
  if (error.code != kSuccess) {
    std::cout << error << std::endl;
    return -1;
  }

  FRAMEWORK::ShutDown();
}
```
/// A. 作为 user，需要使用 NodeReceptor 完成节点上下架操作
/// B. 传统 nodemonitor client 在新的namespace环境下的平替
/// C. Framework Init，完成 node manager client + node receptor 的启动
/// D. Audience 的使用例子
/// E. WatchService 需要用户传入 WatchService 的实现，因此需要用户显示启动

cmake：

```CMAKE
cmake_minimum_required(VERSION 3.17)
project(UserExample)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD_REQUIRED 17)

add_subdirectory(${DEPS_DIR}/nodeframe ${CMAKE_CURRENT_BINARY_DIR}/nodeframe)

add_executable(user-example src/user_main.cc)
target_link_libraries(user-example PRIVATE service_framework)
```
对应的配置文件
```yaml
root:
#server_info
  cluster_id     : 1
  token_seed     : 6666
  # 优先使用 server_ip, server_ip 与 server_nic 必须设置其一
  server_ip      : 127.0.0.1
  main_server_port    : 19000
  accept_modules : [kCenter]

  node_manager:
    #possible roles: guest/user/master/admin
    client_role: user
    user_module_id : kInterface
    access_modules : [kCenter, kColumbus, kCayman]
    user_namespace : kYottaStore
    across_namespace_access_modules :
      - namespace: kYottaIndex
        access_modules: [kBroadcaster, kPascalServer]
    # 如果 columbus.enabled == false node_manager 的 service_name
    service_name: yotta_namespace_node_manager_1_test
    service_namespace: Test
    # cache_update_interval_ms : 5000
    # az_idc_update_interval_s : 600
    # meta_file_path : "."
    # emergency_ip_path: "../../emergency_ip_list.json"
    # disabled_before_warn_interval : 3600 # 1 hour
    # polaris_api_timeout_ms : 10000 # 10s
    # polaris_connect_timeout_ms : 500
    # polaris_message_timeout_ms : 2000
    # polaris_loadbalancer_type : 0
    # rpc_connection_timeout : 1000
    # rpc_send_timeout : 1000
    # rpc_revc_timeout : 1000
    # rpc_retry_count : 2
    # get_primary_interval :  5000
        
  columbus:
    enabled: false
    columbus_update_interval_s: 1800
    # 64194753:262144(dev)
    service_name: 64194753:262144
    service_namespace: Production

  node_receptor:
    work_thread_count: 1
    io_thread_count: 1
    max_connection_count: 10

  audience:
    enabled: true
    work_thread_count: 1
    io_thread_count: 1
    max_connection_count: 10
    target_namespace : "kYottaIndex"

  cayman:
    enabled: true
    # rpc_connection_timeout : 1000
    # rpc_send_timeout : 1000
    # rpc_revc_timeout : 1000
    # rpc_retry_count : 2
    # get_primary_interval :  5000
    # care_slot_missing : false

  mutong:
    enabled: true
    module_policy_config : {"kInterface" : "AzRandom"} # {target_module : route_policy}
    # refresh_interval_s : 60
    # max_complain_node_count : uint32_t 的最大值

  watch_service:
    enabled: true
    work_thread_count: 1
    io_thread_count: 1
    max_connection_count: 10

  node_operator:
    enabled: false
```