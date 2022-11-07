* 2022/11/7
计划：
    白天：沟通 BlobStore 调研具体应该怎么做（性能测试要不要做，什么时候做）BlobStore 的实现需要参考 COS 现在对各个Client的调用逻辑。
    晚上：阅读 MapReduce 论文，看 6824 Lec1 后半部分视频。20:30去健身。

* 2022/11/4
为什么 inline 可以修复 ODR?
`inline`关键字的作用：
    * a suggestion by the programmer to the compiler, to make calls to this function fast(inline expansion).
    * 

Cgo 封装 NodeManagerClient 不搞了，弄一个更大的模块出来，BlobStorage

* 2022/10/20
  
折腾了一天 thrift echo demo，终于完成了 thfit cpp/go echo server demo.
期间遇到很多坑。加深(复习)了对 libc 的认知，对编译器寻找头文件过程的理解，成果：用cmake管理cpp/go/thrift混合项目。
明天：cgo 调用 thrift cpp client。

* 2022/10/19

折腾了一天 thrift demo ，终于编译完成了 thrift server。
明天目标：thrift echo client， cgo 调用 thrift client。


* 2022/10/18

想要自动化测试 cgo ，想要一条龙搞定结果可视化，所以尝试了 Go 画图表，Go 生成 Excel，进度：跑通 demo。

尝试编译 trpc-cpp 失败，😊
