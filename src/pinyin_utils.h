#ifndef PINYIN_UTILS_H
#define PINYIN_UTILS_H
#include <sstream>
#include <string>
#include <unordered_set>
#include <fstream>
#include <algorithm>
#include <cctype>
#include <unordered_map>

class PinyinUtil {
public:
  static std::unordered_map<std::string, std::string> sm_keymaps;
  static std::unordered_map<std::string, std::string> sm_keymaps_reversed;
  static std::unordered_map<std::string, std::string> zero_sm_keymaps;
  static std::unordered_map<std::string, std::string> zero_sm_keymaps_reversed;
  static std::unordered_map<std::string, std::string> ym_keymaps;
  static std::unordered_map<std::string, std::string> ym_keymaps_reversed;

  static std::unordered_set<std::string> &quanpin_set;
  static std::string cvt_single_sp_to_pinyin(std::string sp_str);
  static std::string pinyin_segmentation(std::string sp_str);
};

#endif // PINYIN_UTILS_H
