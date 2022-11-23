#include <iostream>
using namespace std;

class Base {
 public:
  virtual int fcn();  // 虚函数
};

class D1 : Base {
 public:
  int fcn(int);       // 非虚函数
  virtual void f2();  // 虚函数
};

class D2 : D1 {
 public:
  int fcn(int);  // 非虚函数，隐藏掉 D1::fcn(int)
  int fcn();     // 虚函数，覆盖掉 Base::fcn()
  void f2();     // 虚函数，覆盖掉 D1::f2()
};

Base bobj;
D1 d1obj;
D2 d2obj;
Base *bp1 = &bobj, *bp2 = &d1obj, *bp3 = &d2obj;

bp1->fcn();
bp2->fcn();
bp3->fcn();

D1 *d1p = &d1obj;
D2 *d2p = &d2obj;

bp2->f2();  // 名字查找以静态类型的作用域为起点
            // 错误：基类无法调用派生类的成员
d1p->f2();  // 虚函数，动态绑定至 D1::f2()
d2p->f2()   // 虚函数，动态绑定至 D2::f2()

    Base *p1 = &d2obj;
D1 *p2 = &d2obj;
D2 *p2 = &d2obj;

p1->fcn(42);  // 错误，Base 中没有接受int的fcn
p2->fcn(42);  // 正确，静态绑定，调用 D1::fcn(int)
p3->fcn(42);  // 正确，静态绑定，调用 D2::fcn(int)
