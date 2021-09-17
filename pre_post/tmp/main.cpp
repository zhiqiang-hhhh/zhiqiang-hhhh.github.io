#include <iostream>

using namespace std;

class Foo {
    public:
        Foo() { std::cout << "Foo::Foo()\n"; }
        Foo(const Foo& foo1_) { std::cout << "Foo::Foo(const Foo&)\n"; }
        const Foo& operator=(const Foo&) {
            std::cout << "Foo::operator=(const Foo&)\n"; 
            return *this;
        }

};

class Bar {
    private:
        Foo foo;
    public:
        Bar() { std::cout << "Bar::Bar()\n"; }
        Bar(const Foo& foo_) : foo(foo_) {}
        // Bar(const Foo& foo_) {
        //    foo(foo_);
        // }
};

int main(){
    Foo foo;
    Bar bar(foo);
    return 0;
}