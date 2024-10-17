#include <vector>
#include <tuple>
#include <unordered_map>
#include <string>
#include <fstream>
#include <sqlite3.h>
#include <memory>
#include <boost/algorithm/string.hpp>

#include "log.h"

class DictionaryUlPb {
public:
  using WordItem = std::tuple<std::string, std::string, int>;

  /*
    Return: simple value list of data stored in database table
  */
  std::vector<std::string> generate(const std::string code);
  /*
    Return: list of complete item data of database table
  */
  std::vector<WordItem> generate_tuple(const std::string code);

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

  /*
    generate list for single char
  */
  void generate_for_single_char(std::vector<std::string> &candidate_list, std::string code);
  void filter_key_value_list(std::vector<std::string> &candidate_list, const std::vector<std::string> &pinyin_list, const std::vector<std::pair<std::string, std::string>> &key_value_list);
  /*
    Return: list of value data in database table
  */
  std::vector<std::string> select_data(std::string sql_str);
  /*
    Return: list of complete item data in database table
  */
  std::vector<WordItem> select_complete_data(std::string sql_str);
  /*
    Return: list of key and value data in database table
  */
  std::vector<std::pair<std::string, std::string>> select_key_and_value(std::string sql_str);
  /*
    Return:
      - generated sql
      - whether needed to filter
  */
  std::pair<std::string, bool> build_sql(std::string sp_str, std::vector<std::string> pinyin_list);
};
