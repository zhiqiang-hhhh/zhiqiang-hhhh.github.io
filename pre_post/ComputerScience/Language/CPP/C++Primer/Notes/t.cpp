#include <unistd.h>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace std;
int main() {
  unique_ptr<int[]> up(new int[10]);
  auto data = up.release();
  auto end = data + 10;
  for (auto s = data; s != end; s++) {
    cout << *s << endl;
  }

  int i = 100;
  const int ci = i, &ri = i;
  cout << ri << endl;
  i = 101;
  cout << ri << endl;
  cout << ci << endl;

  unsigned int cn = 100;
  std::cout << std::to_string(cn) << std::endl;
  cout << "=======\n";
  vector<int> v1{1, 2, 3, 5};
  cout << "*v1.begin: " << *v1.begin() << endl;
  vector<int> v2{*v1.begin()};
  for (auto i : v2) {
    cout << i << " " << endl;
  }

  auto res = make_shared<vector<int>>();
  res->push_back(1);
  
}
