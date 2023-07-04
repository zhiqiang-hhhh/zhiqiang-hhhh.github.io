# 虚继承时对象的内存布局
## 多层普通继承时的问题
```cpp
class Grand {
public:
    int g_i;    // 4 bytes
};

class A1 : public Grand {
public:
    int a1_i;   // 4 + 4 bytes
};

class A2 : public Grand {
    int a2_i;   // 4 + 4 bytes
};

class C : public A1, A2 {
public:
    int c_i;    // 4 + 4 + 4 + 4 + 4 = 20 bytes
};

int main() {
    Grand g;
    A1 a1;
    A2 a2;
    C c;

    std::cerr << "sizeof Grand: "  << sizeof(g) << std::endl;
    std::cerr << "sizeof A1: "  << sizeof(a1) << std::endl;
    std::cerr << "sizeof A2: "  << sizeof(a2) << std::endl;
    std::cerr << "sizeof C: "  << sizeof(c) << std::endl;
}
```
```bash
[hzq@VM-85-83-centos examples]$ clang -Xclang -fdump-record-layouts temp.cpp

*** Dumping AST Record Layout
         0 | class Grand
         0 |   int g_i
           | [sizeof=4, dsize=4, align=4,
           |  nvsize=4, nvalign=4]

*** Dumping AST Record Layout
         0 | class A1
         0 |   class Grand (base)
         0 |     int g_i
         4 |   int a1_i
           | [sizeof=8, dsize=8, align=4,
           |  nvsize=8, nvalign=4]

*** Dumping AST Record Layout
         0 | class A2
         0 |   class Grand (base)
         0 |     int g_i
         4 |   int a2_i
           | [sizeof=8, dsize=8, align=4,
           |  nvsize=8, nvalign=4]

*** Dumping AST Record Layout
         0 | class C
         0 |   class A1 (base)
         0 |     class Grand (base)
         0 |       int g_i
         4 |     int a1_i
         8 |   class A2 (base)
         8 |     class Grand (base)
         8 |       int g_i
        12 |     int a2_i
        16 |   int c_i
           | [sizeof=20, dsize=20, align=4,
           |  nvsize=20, nvalign=4]

[hzq@VM-85-83-centos examples]$ ./temp
sizeof Grand: 4
sizeof A1: 8
sizeof A2: 8
sizeof C: 20
```
从打印出来的 memory layout 中，可以清晰看到，孙子类 C 中包含了两个祖父类 Grand，因此多层普通继承的最直接问题就是**导致孙子类中出现多个祖父类**。进而导致空间效率问题、执行效率问题以及二义性问题。
空间效率指 sizeof(C) 膨胀，包含了两个祖父类（有时候是没必要的）。空间膨胀后，孙子类对象的拷贝效率降低。二义性问题指，由于对象C中有两个Grand类，那么就有两个名字为 g_i 的数据成员，那么当我们执行如下的代码`c.g_i = 100;`时，编译器就会报错，因为编译器不知道这个 g_i 是哪一个 g_i。


### 通过虚继承解决上述问题
```cpp
class Grand {
public:
    int g_i;
};

class A1 : virtual public Grand {
public:
    int a1_i;
};

class A2 : virtual public Grand {
    int a2_i;
};

class C : public A1, A2 {
public:
    int c_i;
};

int main() {
    Grand g;
    A1 a1;
    A2 a2;
    C c;
    std::cerr << "sizeof Grand: "  << sizeof(g) << std::endl;
    std::cerr << "sizeof A1: "  << sizeof(a1) << std::endl;
    std::cerr << "sizeof A2: "  << sizeof(a2) << std::endl;
    std::cerr << "sizeof C: "  << sizeof(c) << std::endl;
}
```
通过虚继承，能够确保在孙子类中只包含一个 Grand 类。
```bash
[hzq@VM-85-83-centos examples]$ ./temp
sizeof Grand: 4
sizeof A1: 16
sizeof A2: 16
sizeof C: 40
```
编译器实现虚继承的关键概念是：虚基类表和虚基类表指针。
当 A1 和 A2 这两个父类虚继承祖父类 Grand 时，编译器会为 A1 和 A2 的对象各自添加一个虚基类指针，因此这里导致 sizeof(A1) 变为 16，等于两个 int + 一个指针。
而孙子类 C 的大小变为 40，实际上是因为 C 中包含了 4 个 int 成员变量 + 2 个指针，大小为 4 * 4 + 2 * 8 = 32，再加在 64 位操作系统上为了内存对齐增加的 8 字节空间。具体可以看 clang 打印出来的 memory layout
```
*** Dumping AST Record Layout
         0 | class Grand
         0 |   int g_i
           | [sizeof=4, dsize=4, align=4,
           |  nvsize=4, nvalign=4]

*** Dumping AST Record Layout
         0 | class A1
         0 |   (A1 vtable pointer)
         8 |   int a1_i
        12 |   class Grand (virtual base)
        12 |     int g_i
           | [sizeof=16, dsize=16, align=8,
           |  nvsize=12, nvalign=8]

*** Dumping AST Record Layout
         0 | class A2
         0 |   (A2 vtable pointer)
         8 |   int a2_i
        12 |   class Grand (virtual base)
        12 |     int g_i
           | [sizeof=16, dsize=16, align=8,
           |  nvsize=12, nvalign=8]

*** Dumping AST Record Layout
         0 | class C
         0 |   class A1 (primary base)
         0 |     (A1 vtable pointer)
         8 |     int a1_i
        16 |   class A2 (base)
        16 |     (A2 vtable pointer)
        24 |     int a2_i
        28 |   int c_i
        32 |   class Grand (virtual base)
        32 |     int g_i
           | [sizeof=40, dsize=36, align=8,
           |  nvsize=32, nvalign=8]
```
注意最后一部分，class C 的第一个 8 字节是 class A1 的 vtable pointer，第二个 8 字节的前 4 字节是 A1::a1_i，后 4 字节是为了内存对齐补充的 4 字节，第三个 8 字节是 class A2 的 vtable pointer，第四个 8 字节的前 4 字节是 A2::a2_i，同样后 4 字节是 C::c_i，最后一个 8 字节的前 4 字节是 Grand::g_i，最后 4 字节是内存对齐补充。因此一共补充了 8 字节，总空间为 40 字节。

对于虚继承的类 A1 和 A2，称这个类为虚基类。编译器会在虚基类的构造函数中，为虚基类表指针赋值，虚基类表的地址同样是在编译器决定的。


## 虚基类表
前面我们看到了虚基类表指针，但是并没有深入了解虚基类表中的内容。这部分内容目前也不打算深如研究了，只需要知道其大概作用：用于访问虚基类成员。换而言之，相比直接继承，虚继承情况下访问基类的数据成员需要增加一步访问虚基类表的步骤，因此性能上会稍微慢一些。