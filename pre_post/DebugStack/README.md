add_custom_command 提示 `TARGET 'mytarget' was not created in this directory.`

https://stackoverflow.com/questions/40294146/alternative-to-cmake-post-build-command-when-target-is-in-subdirectory


连接器选项：

-Wl,--trace-symbol=<SYM> 对于只能打印 main 中的符号，不能打印出引用的其他库中的符号
--cref 可以把可执行文件里的所有符号，以及其在哪里被定义打印出来

https://stackoverflow.com/questions/55359700/trace-gcc-linker-linking-process

```
add_executable(main main.cpp)
target_link_libraries(main PRIVATE B)
target_link_options(main PUBLIC -Wl,--trace-symbol=main -Wl,--trace-symbol=FuncB2 -Wl,--Map=lmap -Wl,--cref)
```
链接器生成可执行文件时进行无用符号删除操作的粒度是编译单元，即可重定位目标文件。如果某个 .cc 中的符号没有被外部引用，那么这整个 .cc 将不会出现在最终的可执行目标文件中。

https://csstormq.github.io/blog/%E8%AE%A1%E7%AE%97%E6%9C%BA%E7%B3%BB%E7%BB%9F%E7%AF%87%E4%B9%8B%E9%93%BE%E6%8E%A5%EF%BC%884%EF%BC%89%EF%BC%9A%E7%AC%A6%E5%8F%B7%E8%A7%A3%E6%9E%90#jump%E9%AA%8C%E8%AF%81%E8%BF%87%E7%A8%8B6