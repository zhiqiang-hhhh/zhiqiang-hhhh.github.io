#include <iostream>
#include <vector>
using namespace std;

int main() {
  vector<int> nums{5, 1, 2, 3};
  vector<int> nums2{4, 5, 6, 1, 6, 7, 9};
  auto pos = nums.size();
  nums.insert(nums.end(), nums2.begin(), nums2.end());

  for (auto item : nums) {
    cout << item << " ";
  }
  cout << "\n========" << endl;
  // nums.at()的参数只能是 std::size_t 类型，不能是迭代器
  // nums.at();
  auto before_begin = nums.begin();
  before_begin -= 1;
  before_begin += 1;
  swap(*before_begin, *(nums.end() - 1));
  for (auto i : nums) {
    cout << i << " ";
  }
  cout << endl;

  cout << "\n========" << endl;
#define parent(i) ((i + 1) % 2) ? (i >> 1) : (i >> 1 - 1)
#define left(i) (i << 1) + 1
#define right(i) (i << 1) + 2
  int i = 5;
  int r1 = (i % 2) ? (i >> 1) : (i >> 1) - 1;
  int r2 = i >> 1;
  cout << r1 << " " << r2 << endl;
  //cout << parent(i);
}