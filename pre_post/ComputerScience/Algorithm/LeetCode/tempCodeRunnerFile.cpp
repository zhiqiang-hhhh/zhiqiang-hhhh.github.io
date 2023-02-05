#include <iostream>
#include <memory>
#include <vector>

std::shared_ptr<std::vector<int>> input(std::istream& in) {
  size_t s;
  in >> s;
  // std::cout << s << std::endl;
  if (s == 0)
    return std::make_shared<std::vector<int>>();
  else {
    // std::cout << s << std::endl;
    auto v_sptr = std::make_shared<std::vector<int>>(s);
    // std::cout << s << std::endl;
    auto i = v_sptr->begin();
    while (in >> *(i++)) {
      if (i == v_sptr->end()) break;
    }
    return v_sptr;
  }
}

struct memo_elem {
  int sum = 0;
  int first;
  int last;
};

memo_elem dp_BCS(std::shared_ptr<std::vector<int>> vp) {
  std::vector<int> res;
  std::vector<memo_elem> memo;
  for (const auto& i : *vp) {
    if(memo.size() != 0 && memo.back().sum >= 0 ){
      memo_elem temp;
      temp.sum = i + memo.back().sum;
      temp.last = i;
      temp.first = memo.back().first;
      memo.push_back(temp);
    }
    else{
      memo_elem temp;
      temp.sum = i;
      temp.first = i;
      temp.last = i;
      memo.push_back(temp);
    }

  }
  auto b = memo.begin();
  for (auto i = memo.begin(); i < memo.end(); i++) {
    if (i->sum > b->sum) b = i;
  }
  return *b;
}

int main() {
  while (true) {
    auto vp = input(std::cin);
    if (vp->size() == 0) {
      //std::cout << "quie";
      break;
    }
    auto ans = dp_BCS(vp);
    std::cout << ans.sum << ' ' << ans.first << ' ' << ans.last << std::endl;
  }
  return 0;
}