#include <iostream>

using namespace std;
class Base
{
public:

    void NormalFunc() {
        std::cout << "Base::NormalFunc()\n";
    }

    virtual void VirtualFunc() {
        std::cout << "Base::Virtual()\n";
    }

    virtual void PureVirtualFunc() = 0;
};

class Drive1 : public Base
{
public:

    void NormalFunc() {
        std::cout << "Drive1::NormalFunc()\n";
    }

    void VirtualFunc() override {
        std::cout << "Drive1::VirtualFunc()\n";
    }

    void PureVirtualFunc() override {
        std::cout << "Drive1::PureVirtualFunc()\n";
    }
};

class Drive2 : public Base
{
public:

    void NormalFunc() {
        std::cout << "Drive2::NormalFunc()\n";
    }

    void VirtualFunc() override {
        std::cout << "Drive2::VirtualFunc()\n";
    }

    void PureVirtualFunc() override {
        std::cout << "Drive2::PureVirtualFunc()\n";
    }
};


int main ()
{
    Base * base_ptr;
    base_ptr = new Drive1();
    base_ptr->NormalFunc();
    base_ptr->VirtualFunc();
    base_ptr->PureVirtualFunc();

    Drive1 * d1_ptr = dynamic_cast<Drive1 *>(base_ptr);

    d1_ptr->NormalFunc();

    delete base_ptr;

    base_ptr = new Drive2();
    base_ptr->NormalFunc();
    base_ptr->VirtualFunc();
    base_ptr->PureVirtualFunc();

    delete base_ptr;

    return 0;
}