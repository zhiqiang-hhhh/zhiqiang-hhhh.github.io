#include <iostream>
#include <string>
#include <vector>

int main() {
  std::vector<std::vector<int>> vv;
  std::cout << vv.size() << std::endl;
  std::vector<int> temp;
  vv.push_back(temp);
  std::cout << vv.size() << std::endl;
}