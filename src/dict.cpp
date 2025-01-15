#include "dict.h"
#include "log.h"
#include "pinyin_utils.h"
#include <sqlite3.h>
#include <string>
#include <tuple>
#include <utility>
#include <regex>
#include <cstdlib>
#include <codecvt>
#include <locale>
#include "../googlepinyinime-rev/src/include/pinyinime.h"

std::vector<std::string> DictionaryUlPb::alpha_list{"a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m", "n", "o", "p", "q", "r", "s", "t", "u", "v", "w", "x", "y", "z"};
// clang-format off
std::vector<std::string> DictionaryUlPb::single_han_list{
  "啊按爱安暗阿案艾傲奥哎唉岸哀挨埃矮昂碍俺熬黯敖澳暧凹懊嗷癌肮蔼庵",
  "把被不本边吧白别部比便并变表兵半步百办般必帮保报备八北包背布宝爸",
  "从才此次错曾存草刺层参村藏菜彩采财操残惨策材餐侧词苍测猜肏擦匆粗",
  "的到大地地但得得对多点当动打定第等东带电队倒道代弟度底答断达单德",
  "嗯嗯而儿二尔饿呃恶耳恩额俄愕鹅噩娥厄峨鄂遏扼鳄蛾摁饵婀讹阿迩锷贰",
  "放发法分风飞反非服房夫父复饭份佛福否费府防副负翻烦方付封凡仿富纷",
  "个过国给高感光果公更关刚跟该工干哥告怪管功根各敢够官格攻古鬼观赶",
  "或好会还后和很话回行候何海活黑红花孩火乎合换化哈华害喝黄呼皇怀忽",
  "成长出处常吃场车城传冲楚沉陈朝持穿产除程差床初称查春察充超承船窗",
  "就级集家经见间几进将觉军及叫机接今加解金惊竟姐剑结紧记教季击急静",
  "看开口快空可刻苦克客况肯恐靠块狂哭卡科抗控课困孔康酷颗凯宽括款亏",
  "来里老啦了两力连理脸龙李林路立离量流利冷落令灵刘领罗留乐梨论亮乱",
  "吗没面明门名马美命目满魔们每妈民忙慢母梦木妹密米莫买毛默迷猛秘模",
  "那年女难内你男哪拿南脑娘念您怒弄宁牛闹娜尼奶纳奈凝农努诺呢鸟扭耐",
  "哦噢欧偶呕殴鸥藕区怄瓯讴沤耦喔𠙶𬉼㒖㭝㰶㸸㼴䉱䌂䌔䙔䥲䧢區吘吽嘔",
  "平怕片跑破旁朋品派皮排拍婆飘普盘陪配扑漂碰牌偏凭批判爬拼迫骗胖炮",
  "请去起前气其却全轻清亲强且钱奇青切千求确球期七取群器区枪权骑情秦",
  "人然如让日入任认容若热忍仍肉弱软荣仁瑞绕扔融染惹扰燃锐润辱饶柔刃",
  "所三色死四思算虽似斯随司送诉丝速散苏岁松孙索素赛宋森碎私塞扫宿损",
  "他她天头同听太特它通突提题条体停团台痛调谈跳铁统推退态图叹堂土逃",
  "是说上时神深手生事声晒实十少水师山使受屎世始失士删湿书谁谁双数啥",
  "这中只知真长正种主住张战直重着者找转至之指站周终值整制阵准众章装",
  "我为无问外王位文望完物万五往微武哇晚未围玩务卫威味温忘屋闻舞维吴",
  "下小想些笑行向学新相像西先心信性许现喜象星系血血息形兴雪消显响修",
  "一有也要以样已又意于眼用因与应原由远云音越影言衣业员夜友阳语亿元",
  "在子自做走再最怎作总早坐字嘴则组足左造资族座责紫宗咱罪尊择昨增祖"
};
// clang-format on

DictionaryUlPb::DictionaryUlPb() {

  ime_pinyin::im_set_max_lens(64, 32);
  if (!ime_pinyin::im_open_decoder((PinyinUtil::get_home_path() + "/.local/share/fcitx5-fanime/dict_pinyin.dat").c_str(), (PinyinUtil::get_home_path() + "/.local/share/fcitx5-fanime/user_dict.dat").c_str())) {
    // std::cout << "fany bug.\n";
  }

  const char *username = getenv("USER");
  if (username == nullptr) {
    username = getenv("LOGNAME");
  }
  std::string usernameStr(username);
  db_path = PinyinUtil::get_home_path() + "/.local/share/fcitx5-fanime/cutted_flyciku_with_jp.db";
  log_path = PinyinUtil::get_home_path() + "/.local/share/fcitx5-fanime/app.log";
  logger = std::make_unique<Log>(log_path);
  const char *homeDir = getenv("HOME");
  if (!homeDir) {
    // logger->error("Cannot get home directory.");
  }

  int exit = sqlite3_open(db_path.c_str(), &db);
  if (exit != SQLITE_OK) {
    // logger->error("Failed to open db.");
  }

  logger->info("usename: " + PinyinUtil::home_path);
  logger->info("usename: " + PinyinUtil::get_home_path());
  logger->info("db path: " + db_path);
  logger->info("log path: " + log_path);
}

std::vector<DictionaryUlPb::WordItem> DictionaryUlPb::generate(const std::string code) {
  std::vector<DictionaryUlPb::WordItem> candidate_list;
  if (code.size() == 0) {
    return candidate_list;
  }
  std::vector<std::string> code_list;
  if (code.size() == 1) {
    generate_for_single_char(candidate_list, code);
  } else {
    // segmentation first
    std::string pinyin_with_seg = PinyinUtil::pinyin_segmentation(code);
    std::vector<std::string> pinyin_list;
    boost::split(pinyin_list, pinyin_with_seg, boost::is_any_of("'"));
    // build sql for query
    auto sql_pair = build_sql(code, pinyin_list);
    std::string sql_str = sql_pair.first;
    if (sql_pair.second) { // need to filter
      auto key_value_weight_list = select_complete_data(sql_str);
      filter_key_value_list(candidate_list, pinyin_list, key_value_weight_list);
    } else {
      candidate_list = select_complete_data(sql_str);
    }
  }
  return candidate_list;
}

void DictionaryUlPb::generate_for_single_char(std::vector<DictionaryUlPb::WordItem> &candidate_list, std::string code) {
  std::string s = single_han_list[code[0] - 'a'];
  for (size_t i = 0; i < s.length();) {
    size_t cplen = PinyinUtil::get_first_char_size(s.substr(i, s.size() - i));
    candidate_list.push_back(std::make_tuple(code, s.substr(i, cplen), 1));
    i += cplen;
  }
}

void DictionaryUlPb::filter_key_value_list(std::vector<DictionaryUlPb::WordItem> &candidate_list, const std::vector<std::string> &pinyin_list, const std::vector<DictionaryUlPb::WordItem> &key_value_weight_list) {
  std::string regex_str("");
  for (const auto &each_pinyin : pinyin_list) {
    if (each_pinyin.size() == 2) {
      regex_str += each_pinyin;
    } else {
      regex_str = regex_str + each_pinyin + "[a-z]";
    }
  }
  std::regex pattern(regex_str);
  for (const auto &each_tuple : key_value_weight_list) {
    if (std::regex_match(std::get<0>(each_tuple), pattern)) {
      candidate_list.push_back(each_tuple);
    }
  }
}

std::vector<DictionaryUlPb::WordItem> DictionaryUlPb::generate_for_creating_word(const std::string code) { return select_complete_data(build_sql_for_creating_word(code)); }

int DictionaryUlPb::create_word(std::string pinyin, std::string word) {
  std::string jp;
  for (size_t i = 0; i < pinyin.size(); i += 2)
    jp += pinyin[i];
  if (!do_validate(pinyin, jp, word))
    return ERROR;
  insert_data(build_sql_for_inserting_word(pinyin, jp, word));
  return OK;
}

// generate_with_seg_pinyin

DictionaryUlPb::~DictionaryUlPb() {
  if (db) {
    sqlite3_close(db);
  }
}

std::vector<std::string> DictionaryUlPb::select_data(std::string sql_str) {
  std::vector<std::string> candidateList;
  // logger->error(sql_str);
  sqlite3_stmt *stmt;
  int exit = sqlite3_prepare_v2(db, sql_str.c_str(), -1, &stmt, 0);
  if (exit != SQLITE_OK) {
    // logger->error("sqlite3_prepare_v2 error.");
  }
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    candidateList.push_back(std::string(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2))));
  }
  sqlite3_finalize(stmt);
  return candidateList;
}

std::vector<DictionaryUlPb::WordItem> DictionaryUlPb::select_complete_data(std::string sql_str) {
  std::vector<DictionaryUlPb::WordItem> candidateList;
  sqlite3_stmt *stmt;
  int exit = sqlite3_prepare_v2(db, sql_str.c_str(), -1, &stmt, 0);
  if (exit != SQLITE_OK) {
    // logger->error("sqlite3_prepare_v2 error.");
  }
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    // clang-format off
    candidateList.push_back(
      std::make_tuple(
        std::string(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0))), // key
        std::string(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2))), // value
        sqlite3_column_int(stmt, 3)                                                // weight
      )
    );
    // clang-format on
  }
  sqlite3_finalize(stmt);
  return candidateList;
}

std::vector<std::pair<std::string, std::string>> DictionaryUlPb::select_key_and_value(std::string sql_str) {
  std::vector<std::pair<std::string, std::string>> candidateList;
  // logger->error(sql_str);
  sqlite3_stmt *stmt;
  int exit = sqlite3_prepare_v2(db, sql_str.c_str(), -1, &stmt, 0);
  if (exit != SQLITE_OK) {
    // logger->error("sqlite3_prepare_v2 error.");
  }
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    candidateList.push_back(std::make_pair(std::string(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0))), std::string(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2)))));
  }
  sqlite3_finalize(stmt);
  return candidateList;
}

int DictionaryUlPb::insert_data(std::string sql_str) {
  sqlite3_stmt *stmt;
  int exit = sqlite3_prepare_v2(db, sql_str.c_str(), -1, &stmt, 0);
  if (exit != SQLITE_OK) {
    // logger->error("sqlite3_prepare_v2 error.");
  }
  exit = sqlite3_step(stmt);
  if (exit != SQLITE_DONE) {
    // log
  }
  return 0;
}

std::pair<std::string, bool> DictionaryUlPb::build_sql(const std::string &sp_str, std::vector<std::string> &pinyin_list) {
  bool all_entire_pinyin = true;
  bool all_jp = true;
  std::vector<std::string>::size_type jp_cnt = 0; // 简拼的数量
  for (std::vector<std::string>::size_type i = 0; i < pinyin_list.size(); i++) {
    std::string cur_pinyin = pinyin_list[i];
    if (cur_pinyin.size() == 1) {
      all_entire_pinyin = false;
      jp_cnt += 1;
    } else {
      all_jp = false;
    }
  }
  std::string sql;
  std::string base_sql("select * from %1% where %2% = '%3%' order by weight desc limit %4%;");
  std::string table = choose_tbl(sp_str, pinyin_list.size());
  bool need_filtering = false;
  if (all_entire_pinyin) { // 拼音分词全部是全拼
    sql = boost::str(boost::format(base_sql) % table % "key" % sp_str % default_candicate_page_limit);
  } else if (all_jp) { // 拼音分词全部是简拼
    sql = boost::str(boost::format(base_sql) % table % "jp" % sp_str % default_candicate_page_limit);
  } else if (jp_cnt == 1) { // 拼音分词只有一个是简拼
    std::string sql_param0("");
    std::string sql_param1("");
    for (std::vector<std::string>::size_type i = 0; i < pinyin_list.size(); i++) {
      if (pinyin_list[i].size() == 1) {
        sql_param0 = sql_param0 + pinyin_list[i] + "a";
        sql_param1 = sql_param1 + pinyin_list[i] + "z";
      } else {
        sql_param0 += pinyin_list[i];
        sql_param1 += pinyin_list[i];
      }
    }
    sql = boost::str(boost::format("select * from %1% where key >= '%2%' and key <= '%3%' order by weight desc limit %4%;") % table % sql_param0 % sql_param1 % default_candicate_page_limit);
  } else { // 既不是纯粹的完整的拼音，也不是纯粹的简拼，并且简拼的数量严格大于 1
    need_filtering = true;
    std::string sql_param("");
    for (std::string &cur_pinyin : pinyin_list) {
      sql_param += cur_pinyin.substr(0, 1);
    }
    // TODO: not adding weight desc
    sql = boost::str(boost::format("select * from %1% where jp = '%2%';") % table % sql_param); // do not use limit, we need retrive all data and then filter
  }
  return std::make_pair(sql, need_filtering);
}

std::string DictionaryUlPb::build_sql_for_creating_word(const std::string &sp_str) {
  std::string base_sql = "select * from(select * from %1% where key = '%2%' order by weight desc limit %3%)";
  std::string res_sql = boost::str(boost::format(base_sql) % choose_tbl(sp_str.substr(0, 2), 1) % sp_str.substr(0, 2) % default_candicate_page_limit);
  std::string trimed_sp_str = sp_str.substr(0, 8); // 最多只到 4 字词语
  for (size_t i = 4; i <= sp_str.size(); i += 2) {
    res_sql = boost::str(boost::format(base_sql) % choose_tbl(sp_str.substr(0, i), i / 2) % sp_str.substr(0, i) % default_candicate_page_limit) + " union all " + res_sql;
  }
  return res_sql;
}

std::string DictionaryUlPb::build_sql_for_inserting_word(std::string key, std::string jp, std::string value) {
  std::string table = choose_tbl(key, jp.size());
  std::string base_sql = "insert into %1% (key, jp, value, weight) values ('%2%', '%3%', '%4%', '%5%');";
  return boost::str(boost::format(base_sql) % table % key % jp % value % 10000); // 默认权重 weight 是 10,000
}

std::string DictionaryUlPb::choose_tbl(const std::string &sp_str, size_t word_len) {
  std::string base_tbl("tbl_%1%_%2%");
  if (word_len >= 8)
    return boost::str(boost::format(base_tbl) % "others" % sp_str[0]);
  return boost::str(boost::format(base_tbl) % word_len % sp_str[0]);
}

bool DictionaryUlPb::do_validate(std::string key, std::string jp, std::string value) {
  if (key.size() % 2 || jp.size() != key.size() / 2 || key.size() != PinyinUtil::cnt_han_chars(value) * 2)
    return false;
  return true;
}

std::string fromUtf16(const ime_pinyin::char16 *buf, size_t len) {
  std::u16string utf16Str(reinterpret_cast<const char16_t *>(buf), len);
  std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> convert;
  return convert.to_bytes(utf16Str);
}

std::string DictionaryUlPb::search_sentence_from_ime_engine(const std::string &user_pinyin) {
  std::string pinyin_str = user_pinyin;
  const char *pinyin = pinyin_str.c_str();
  size_t cand_cnt = ime_pinyin::im_search(pinyin, strlen(pinyin));
  std::string msg;
  cand_cnt = cand_cnt > 0 ? 1 : 0;
  for (size_t i = 0; i < cand_cnt; ++i) {
    ime_pinyin::char16 buf[256] = {0};
    ime_pinyin::im_get_candidate(i, buf, 255);
    size_t len = 0;
    while (buf[len] != 0 && len < 255)
      ++len;
    msg = fromUtf16(buf, len);
  }
  return msg;
}
