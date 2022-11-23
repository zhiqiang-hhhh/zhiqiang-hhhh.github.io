#include <iostream>
#include <stack>
#include <string>
#include <vector>

int main() {
  std::vector<std::string> numMap{"ling", "yi",  "er", "san", "si",
                                  "wu",   "liu", "qi", "ba",  "jiu"};
  enum num { ling, yi, er, san, si, wu, liu, qi, ba, jiu };
  std::stack<int> spell;
  std::string nstr;
  getline(std::cin, nstr);
  int sum = 0;
  for (auto c : nstr) {
    sum += atoi(&c);
  }
  std::cout << sum << std::endl;
  
  while (sum >= 10) {
    spell.push(sum % 10);
    sum = sum / 10;
  }

  while (spell.size() > 1) {
    std::cout << numMap[spell.top()] << " ";
    spell.pop();
  }
  std::cout << numMap[spell.top()];
  return 0;
}