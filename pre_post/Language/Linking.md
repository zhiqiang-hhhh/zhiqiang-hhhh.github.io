# Linking
什么是链接：Linking is a process of collecting and combining various pieces of code and data into a single file that can be loaded (copied) into memory and executed.
更加朴素的说法，链接是将输入的一组代码和数据的集合重新整理成一个文件，该文件可以被加载进内存并且运行。为什么单纯的代码和数据就不能被运行呢？因为通常来说，这些文件的格式无法被操作系统识别，操作系统把这些文件加载进内存后，按照自己的想法去内存的指定位置获取指令时就傻眼了，这指令我完全不认识啊，所以他们通常不能被执行。

为了规范描述，我们这里将链接器的输入文件称为可重定位目标文件，将链接器的输出文件称为可执行目标文件，有时会忽略两者的目标二字。

## 静态链接

静态链接：Static linking。静态链接得到的目标文件被称为全链接可执行目标文件（Fully linked executable object file）。

链接器为了得到一个可执行文件，需要完成两个主要任务：
1. Symbol resolution（符号解析）
2. Relocation（重定位）

第一个任务通俗来说，就是为每个符号找到其定义。比如我们在 main.cpp 中有这样的代码：
```c++
#include "myheader.h"

int main()
{
    ...
    auto result = myheader::Func();
    ...
    return 0;
}
```
编译器编译的时候在 myheader.h 中只找到了 `myheader::Func()` 的声明，没找到定义啊，定义在 myheader.cpp 里呢，编译器把 myheader.cpp 编译后得到了 myheader.o。main.o 跟 myheader.o 输入给链接器以后，链接器就去找到 myheader.o 中 Func 的位置，填到 main.o 中合适的位置。这个过程就是 symbol resolution，当然我这里说的很粗糙，但是就这么个整体意思。

第二个任务，重定位，则是说，main.o 既然要跟 myheader.o 合并到一起，那么新文件里，已有的符号怎么安排呢？谁在前谁在后呢？这就是重定位做的事。

## 目标文件
目标文件分为三类：
1. Relocatable object file
2. Executable object file
3. Shared object file

所有的这三类目标文件都具有相同的文件格式，称为对象文件格式。在现代的 x86-64 Linux 系统以及 UNIX 系统上，使用 Executable and Linkable Format(ELF)，其他系统使用不同的对象文件格式。目标文件中的内容，包含三部分，指令，数据以及格式信息，格式信息告诉使用者（链接器与加载器）如何解读指令与数据。

<center>
<img alt="picture 1" src="../../images/17954b2887a9861eaa0812ae67097555e4fd158dbb6d298072352b0853528d48.png" height="300px" />  
</center>
上图中描述了一个典型的 ELF 格式的可重定位目标文件，具体每个部分的作用不详细说了只需要知道：

1. ELF 描述了该文件的元信息（哪个平台产生的、字节序等）以及 Section header table 在哪，怎么解读其内容
2. 节头部表（Section header table）描述了图里其他各个section的偏移量
3. 其余所有section记录了指令、数据以及符号信息  

## 符号与符号表
每一个可重定位目标文件的模块 m 均包含一个符号表。该符号表记录了模块 m 定义以及引用的所有符号信息。在链接器的语境下，符号被分为三类：

* 由模块 m 定义，且可以被其他模块引用的符号的全局符号。比如模块 m 中定义的 nonstatic function 以及 global variables
* 被模块 m 引用，但是由其他模块定义的全局符号。比如 m1 引用了 m2 定义的 nonstatic function。
* 由模块 m 定义并且只可以被模块 m 引用的本地符号。比如模块 m 中的 static function 以及 static global variables

第三类中提到的本地符号并不包括函数的局部变量，链接器并不关系这类变量的地址。

符号表被保存在目标文件的 .symtab 段内，符号表的每个项具有如下信息：
```c
typedef struct {
    int name;
    char type:4,
         binding:4;
    char reserved;
    short section;
    long value;
    long size;
} Elf64_Symbol;
```
name 字段可以简单理解为是符号的名称（实际上有区别），不过注意，这里符号名称并不一定就是符号在源码中的命名，通常是经过编码或者改写后的名字，目的是为了防止重复。对于可重定位目标文件，value 通常是该对象与其所在section开始位置的偏移量；对于可执行目标文件（链接后），value 通常是一个运行时的绝对地址。

其它字段具体含义不细说。

使用 GNU readelf 程序可以检查对象文件的内容。

## 符号解析