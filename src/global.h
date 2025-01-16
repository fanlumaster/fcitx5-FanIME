#include <string>

namespace GlobalIME {
inline std::string pinyin = "";            // 拼音
inline std::string jp = "";                // 简拼
inline bool need_to_update_weight = false; // 如果是空格键上屏第一个候选项的，就不用更新 weight
inline bool firstSingleQuotation = 1;      // 单引号
inline bool firstDoubeQuotation = 1;       // 双引号
} // namespace GlobalIME
