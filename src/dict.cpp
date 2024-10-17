#include "dict.h"
#include "pinyin_utils.h"
#include <sqlite3.h>
#include <tuple>
#include <utility>
#include <regex>

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
  logger = std::make_unique<Log>(log_path);
  const char *homeDir = getenv("HOME");
  if (!homeDir) {
    // logger->error("Cannot get home directory.");
  }

  int exit = sqlite3_open(db_path.c_str(), &db);
  if (exit != SQLITE_OK) {
    // logger->error("Failed to open db.");
  }
}

std::vector<std::string> DictionaryUlPb::generate(const std::string code) {
  std::vector<std::string> candidate_list;
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
      auto key_value_list = select_key_and_value(sql_str);
      filter_key_value_list(candidate_list, pinyin_list, key_value_list);
    } else {
      candidate_list = select_data(sql_str);
    }
  }
  return candidate_list;
}

void DictionaryUlPb::generate_for_single_char(std::vector<std::string> &candidate_list, std::string code) {
  std::string s = single_han_list[code[0] - 'a'];
  for (size_t i = 0; i < s.length();) {
    size_t cplen = PinyinUtil::get_first_char_size(s.substr(i, s.size() - i));
    candidate_list.push_back(s.substr(i, cplen));
    i += cplen;
  }
}

void DictionaryUlPb::filter_key_value_list(std::vector<std::string> &candidate_list, const std::vector<std::string> &pinyin_list, const std::vector<std::pair<std::string, std::string>> &key_value_list) {
  std::string regex_str("");
  for (const auto &each_pinyin : pinyin_list) {
    if (each_pinyin.size() == 2) {
      regex_str += each_pinyin;
    } else {
      regex_str = regex_str + each_pinyin + "[a-z]";
    }
  }
  std::regex pattern(regex_str);
  for (const auto &each_pair : key_value_list) {
    if (std::regex_match(each_pair.first, pattern)) {
      candidate_list.push_back(each_pair.second);
    }
  }
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
        std::string(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0))),
        std::string(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2))),
        sqlite3_column_int(stmt, 3)
      )
    );
    // clang-format on
  }
  sqlite3_finalize(stmt);
  return candidateList;
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

std::pair<std::string, bool> DictionaryUlPb::build_sql(std::string sp_str, std::vector<std::string> pinyin_list) {
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
  bool need_filtering = false;
  if (all_entire_pinyin) { // 拼音分词全部是全拼
    sql = "select * from xiaoheulpbtbl where key = '" + sp_str + "' order by weight desc limit 80;";
  } else if (all_jp) { // 拼音分词全部是简拼
    sql = "select * from xiaoheulpbtbl where jp = '" + sp_str + "' order by weight desc limit 80;";
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
    sql = "select * from xiaoheulpbtbl where key >= '" + sql_param0 + "' and key <= '" + sql_param1 + "' order by  length(key) asc, weight desc limit 80;";
  } else { // 既不是纯粹的完整的拼音，也不是纯粹的简拼，并且简拼的数量严格大于 1
    need_filtering = true;
    std::string sql_param("");
    for (std::string &cur_pinyin : pinyin_list) {
      sql_param += cur_pinyin.substr(0, 1);
    }
    sql = "select * from xiaoheulpbtbl where jp = '" + sql_param + "';"; // 不能用 limit，要全部取出之后会有过滤
  }
  return std::make_pair(sql, need_filtering);
}
