### 使用 CRTP 包装你的大脑
1995 年，`James Coplien` 在 `C++ Report` 上的一篇文章第一次引入 CRTP 这个名字。
CRTP 最初
#### virtual function 的问题
性能开销是虚函数的最大问题。虚函数的调用成本可能数倍于非虚函数，对于那些很简单的可以被 inline 的方法来说，这种代价可能更高（虚函数无法被 inline）。
#### Introducing CRTP
```cpp
template <typename D> class B {
    ...
};

class D : public B<D> {
    ...
}
```
基类被改成了一个 class template，子类依然继承自基类，但是继承的是基类的一个特殊实例化，这个特殊实例是用子类类型进行实例化的。

Class B is instantiated on class D, and class D inherits from that instantiation of class B, which
is instantiated on class D, which inherits from class B, which... that’s recursion in action.

```cpp
// Example 01
template <typename D> class B {
    public:
    B() : i_(0) {}
    void f(int i) { static_cast<D*>(this)->f(i); }
    int get() const { return i_; }
    protected:
    int i_;
};
class D : public B<D> {
    public:
    void f(int i) { i_ += i; }
};
```

The main restriction on the CRTP is that the size of the base class, B, cannot depend on its template
parameter, D. More generally, the template for class B has to instantiate with type D being an incomplete
type. For example, this will not compile:
```cpp
template <typename D> class B {
    using T = typename D::T;
    T* p_;
};
class D : public B<D> {
    using T = int;
};
```

Page 203