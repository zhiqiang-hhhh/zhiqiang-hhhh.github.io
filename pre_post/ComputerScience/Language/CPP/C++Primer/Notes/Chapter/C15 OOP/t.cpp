#include <iostream>
#include <string>

class base {
 public:
  std::string name() { return basename; }
  virtual void print(std::ostream& os) { os << basename; }

 private:
  std::string basename;
};

class dirved : public base {
 public:
  void print(std::ostream& os) {
    print(os);
    os << " " << i;
  }

 private:
  int i;
};

int main() {
  base* b = new base();
  b->print(std::cout);
  b = new dirved();
  b->print(std::cout);
}