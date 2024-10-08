#include "utils.h"
#include <vector>

std::unordered_map<std::string, std::string> PinyinUtil::sm_keymaps{{"sh", "u"}, {"ch", "i"}, {"zh", "v"}};

std::unordered_map<std::string, std::string> PinyinUtil::sm_keymaps_reversed{{"u", "sh"}, {"i", "ch"}, {"v", "zh"}};
std::unordered_map<std::string, std::string> PinyinUtil::zero_sm_keymaps{{"a", "aa"}, {"ai", "ai"}, {"ao", "ao"}, {"ang", "ah"}, {"e", "ee"}, {"ei", "ei"}, {"en", "en"}, {"eng", "eg"}, {"er", "er"}, {"o", "oo"}, {"ou", "ou"}};
std::unordered_map<std::string, std::string> PinyinUtil::zero_sm_keymaps_reversed{{"aa", "a"}, {"ai", "ai"}, {"ao", "ao"}, {"ah", "ang"}, {"ee", "e"}, {"ei", "ei"}, {"en", "en"}, {"eg", "eng"}, {"er", "er"}, {"oo", "o"}, {"ou", "ou"}};
std::unordered_map<std::string, std::string> PinyinUtil::ym_keymaps{{"iu", "q"}, {"ei", "w"}, {"e", "e"}, {"uan", "r"}, {"ue", "t"}, {"ve", "t"}, {"un", "y"}, {"u", "u"}, {"i", "i"}, {"uo", "o"}, {"o", "o"}, {"ie", "p"}, {"a", "a"}, {"ong", "s"}, {"iong", "s"}, {"ai", "d"}, {"en", "f"}, {"eng", "g"}, {"ang", "h"}, {"an", "j"}, {"uai", "k"}, {"ing", "k"}, {"uang", "l"}, {"iang", "l"}, {"ou", "z"}, {"ua", "x"}, {"ia", "x"}, {"ao", "c"}, {"ui", "v"}, {"v", "v"}, {"in", "b"}, {"iao", "n"}, {"ian", "m"}};
std::unordered_map<std::string, std::string> PinyinUtil::ym_keymaps_reversed{{"q", "iu"}, {"w", "ei"}, {"e", "e"}, {"r", "uan"}, {"t", "ve"}, {"y", "un"}, {"u", "u"}, {"i", "i"}, {"o", "o"}, {"p", "ie"}, {"a", "a"}, {"s", "iong"}, {"d", "ai"}, {"f", "en"}, {"g", "eng"}, {"h", "ang"}, {"j", "an"}, {"k", "ing"}, {"l", "iang"}, {"z", "ou"}, {"x", "ia"}, {"c", "ao"}, {"v", "v"}, {"b", "in"}, {"n", "iao"}, {"m", "ian"}};

std::unordered_set<std::string> &initialize() {
  static std::unordered_set<std::string> tmp_set;
  std::ifstream pinyin_path("/home/sonnycalcr/EDisk/CppCodes/IMECodes/fcitx5-FanIME/assets/pinyin.txt");
  std::string line;
  while (std::getline(pinyin_path, line)) {
    line.erase(std::remove_if(line.begin(), line.end(), [](unsigned char x) { return std::isspace(x); }), line.end());
    tmp_set.insert(line);
  }
  return tmp_set;
}

std::unordered_set<std::string> &PinyinUtil::quanpin_set = initialize();

/*
  把小鹤双拼转换为拼音(全拼)
  目前针对 402 个拼音是一对一的方案
*/
std::string PinyinUtil::cvt_single_sp_to_pinyin(std::string sp_str) {
  if (zero_sm_keymaps_reversed.count(sp_str) > 0) {
    return zero_sm_keymaps_reversed[sp_str];
  }
  if (sp_str.size() != 2)
    return "";
  std::string res = "";
  std::string sm;
  std::vector<std::string> ym_list;

  if (sm_keymaps_reversed.count(sp_str.substr(0, 1)) > 0) {
    sm = sm_keymaps_reversed[sp_str.substr(0, 1)];
  } else {
    sm = sp_str.substr(0, 1);
  }

  for (const auto &pair : ym_keymaps) {
    if (pair.second == sp_str.substr(1, 1)) {
      ym_list.push_back(pair.first);
    }
  }
  if (sm == "" || ym_list.size() == 0) {
    return "";
  }
  for (const auto &ym : ym_list) {
    if (quanpin_set.count(sm + ym) > 0) {
      res = sm + ym;
    }
  }
  return res;
}

/*
  切割双拼字符串，使用单引号 ' 作为切割符号
  使用正向最大划分来进行切割，也可以说是贪心法
*/
std::string PinyinUtil::pinyin_segmentation(std::string sp_str) {
  if (sp_str.size() == 1) {
    return sp_str;
  }
  std::string res("");
  std::string::size_type range_start = 0;
  while (range_start < sp_str.size()) {
    if ((range_start + 2) <= sp_str.size()) {
      // 先切两个字符看看
      std::string cur_sp = sp_str.substr(range_start, 2);
      if (quanpin_set.count(cvt_single_sp_to_pinyin(cur_sp)) > 0) {
        res = res + "'" + cur_sp;
        range_start += 2;
      } else {
        res = res + "'" + cur_sp.substr(0, 1);
        range_start += 1;
      }
    } else {
      res = res + "'" + sp_str.substr(sp_str.size() - 1, 1);
      range_start += 1;
    }
  }
  while (!res.empty() && res[0] == '\'') {
    res.erase(0, 1);
  }
  while (!res.empty() && res[res.size()] == '\'') {
    res.erase(res.size() - 1, 1);
  }
  return res;
}
