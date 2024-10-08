#include <vector>
#include <unordered_map>
#include <string>
#include <fstream>
#include <sqlite3.h>
#include <memory>

#include "log.h"

class DictionaryUlPb {
public:
  std::vector<std::string> generate(const std::string code);

  DictionaryUlPb();
  ~DictionaryUlPb();

private:
  std::ifstream inputFile;
  std::string db_path = "/home/sonnycalcr/EDisk/PyCodes/IMECodes/FanyDictForIME/makecikudb/xnheulpb/makedb/jp_version/out/flyciku_with_jp.db";
  sqlite3 *db = nullptr;
  std::unordered_map<std::string, std::vector<std::string>> dict_map;
  std::string log_path = "/home/sonnycalcr/.local/share/fcitx5-fanyime/app.log";
  std::unique_ptr<Log> logger;

  static std::vector<std::string> alpha_list;
  static std::vector<std::string> single_han_list;

  std::vector<std::string> select_data(std::string sql_str);
  std::vector<std::pair<std::string, std::string>> select_key_and_value(std::string sql_str);
  std::pair<std::string, bool> build_sql(std::string sp_str, std::vector<std::string> pinyin_list);
};
