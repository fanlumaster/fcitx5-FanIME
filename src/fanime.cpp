#include "fanime.h"
#include "dict.h"
#include "log.h"
#include "pinyin_utils.h"
#include <cctype>
#include <fcitx-utils/i18n.h>
#include <fcitx-utils/utf8.h>
#include <fcitx/candidatelist.h>
#include <fcitx/inputpanel.h>
#include <fcitx/instance.h>
#include <fcitx/userinterfacemanager.h>
#include <punctuation_public.h>
#include <quickphrase_public.h>
#include <string>
#include <utility>
#include <vector>
#include <memory>
#include <boost/locale.hpp>
#include <boost/range/algorithm/count.hpp>
#include <boost/circular_buffer.hpp>
#include "./global.h"

#ifdef FAN_DEBUG
#include <chrono>
#endif

namespace {

static const int CANDIDATE_SIZE = 8; // 候选框默认的 size，不许超过 9，不许小于 4

bool checkAlpha(const std::string &s) { return s.size() == 1 && isalpha(s[0]); }

// Template to help resolve iconv parameter issue on BSD.
template <class T> struct function_traits;

// partial specialization for function pointer
template <class R, class... Args> struct function_traits<R (*)(Args...)> {
  using result_type = R;
  using argument_types = std::tuple<Args...>;
};

template <class T> using second_argument_type = typename std::tuple_element<1, typename function_traits<T>::argument_types>::type;

static const std::array<fcitx::Key, 11> selectionKeys = {fcitx::Key{FcitxKey_1}, fcitx::Key{FcitxKey_2}, fcitx::Key{FcitxKey_3}, fcitx::Key{FcitxKey_4}, fcitx::Key{FcitxKey_5}, fcitx::Key{FcitxKey_6}, fcitx::Key{FcitxKey_7}, fcitx::Key{FcitxKey_8}, fcitx::Key{FcitxKey_9}, fcitx::Key{FcitxKey_0}, fcitx::Key{FcitxKey_space}};

class FanimeCandidateWord : public fcitx::CandidateWord {
public:
  FanimeCandidateWord(FanimeEngine *engine, std::string text) : engine_(engine) { setText(fcitx::Text(std::move(text))); }

  void select(fcitx::InputContext *inputContext) const override {
    std::string text_to_commit = text().toString();
    size_t start_pos = text_to_commit.find('(');
    if (start_pos != std::string::npos) {
      text_to_commit.erase(start_pos, text_to_commit.size() - start_pos + 1);
    }
    auto state = inputContext->propertyFor(engine_->factory());

    auto committed_han_size = PinyinUtil::cnt_han_chars(text_to_commit);
    // 如果是前面的拼音子串对应的汉字(词)上屏
    if (FanimeEngine::can_create_word && committed_han_size < FanimeEngine::supposed_han_cnt) {
      // 无论是否是辅助码辅出来的结果，都要去尾
      if (FanimeEngine::seg_pinyin[FanimeEngine::seg_pinyin.size() - 2] == '\'')
        FanimeEngine::seg_pinyin = FanimeEngine::seg_pinyin.substr(0, FanimeEngine::seg_pinyin.size() - 2);
      // 使用完整辅助码的情况下时的去尾
      if (engine_->get_use_fullhelpcode()) {
        FanimeEngine::seg_pinyin = PinyinUtil::pinyin_segmentation(engine_->get_raw_pinyin());
        engine_->set_use_fullhelpcode(false);
      }
      std::string tmp_seg_pinyin = FanimeEngine::seg_pinyin;
      size_t cur_index = 0;
      while (cur_index < committed_han_size) {
        size_t pos = tmp_seg_pinyin.find('\'');
        FanimeEngine::word_pinyin += tmp_seg_pinyin.substr(0, 2);
        tmp_seg_pinyin = tmp_seg_pinyin.substr(pos + 1, tmp_seg_pinyin.size() - (pos + 1));
        cur_index += 1;
      }
      std::string pure_pinyin = boost::algorithm::replace_all_copy(tmp_seg_pinyin, "'", "");
      FanimeEngine::word_to_be_created += text_to_commit;
      FanimeEngine::during_creating = true;
      // continue querying
      state->setCode(pure_pinyin);
    } else {
      if (FanimeEngine::word_to_be_created != "") {
        // 无论是否是辅助码辅出来的结果，都要去尾
        if (FanimeEngine::seg_pinyin[FanimeEngine::seg_pinyin.size() - 2] == '\'')
          FanimeEngine::seg_pinyin = FanimeEngine::seg_pinyin.substr(0, FanimeEngine::seg_pinyin.size() - 2);
        // 使用完整辅助码的情况下时的去尾
        if (engine_->get_use_fullhelpcode()) {
          FanimeEngine::seg_pinyin = PinyinUtil::pinyin_segmentation(engine_->get_raw_pinyin());
          engine_->set_use_fullhelpcode(false);
        }
        std::string pure_pinyin = boost::algorithm::replace_all_copy(FanimeEngine::seg_pinyin, "'", "");
        FanimeEngine::word_pinyin += pure_pinyin;
        FanimeEngine::word_to_be_created += text_to_commit;
        // insert to database
        FanimeEngine::fan_dict.create_word(FanimeEngine::word_pinyin, FanimeEngine::word_to_be_created);
        inputContext->commitString(FanimeEngine::word_to_be_created);
        // 清理缓存
        FanimeEngine::cached_buffer.clear();
      } else {
        inputContext->commitString(text_to_commit);
        if (GlobalIME::need_to_update_weight) {
          GlobalIME::pinyin = engine_->pure_pinyin;
          // FCITX_INFO() << "fany come here: " << GlobalIME::pinyin << " " << text_to_commit;
          FanimeEngine::fan_dict.update_weight_by_word(text_to_commit);
        }
      }
      state->reset();
    }
  }

private:
  FanimeEngine *engine_;
};

class FanimeCandidateList : public fcitx::CandidateList, public fcitx::PageableCandidateList, public fcitx::CursorMovableCandidateList {
public:
  FanimeCandidateList(FanimeEngine *engine, fcitx::InputContext *ic, const std::string &code);
  const fcitx::Text &label(int idx) const override { return labels_[idx]; }
  const fcitx::CandidateWord &candidate(int idx) const override { return *candidates_[idx]; }
  int size() const override { return cand_size_; }
  fcitx::CandidateLayoutHint layoutHint() const override { return fcitx::CandidateLayoutHint::NotSet; }
  bool usedNextBefore() const override { return false; }
  void prev() override;
  void next() override;
  bool hasPrev() const override;
  bool hasNext() const override;

  // TODO: 这里是什么意思
  void prevCandidate() override { cursor_ = (cursor_ + CANDIDATE_SIZE - 1) % CANDIDATE_SIZE; }
  void nextCandidate() override { cursor_ = (cursor_ + 1) % CANDIDATE_SIZE; }
  int cursorIndex() const override { return cursor_; }

private:
  FanimeEngine *engine_;
  fcitx::InputContext *ic_;
  fcitx::Text labels_[CANDIDATE_SIZE];
  std::unique_ptr<FanimeCandidateWord> candidates_[CANDIDATE_SIZE];
  std::string code_;
  int cursor_ = 0;
  int cand_size_ = CANDIDATE_SIZE;
  static std::unique_ptr<Log> logger_;

  // generate words
  int generate();
  void generate_from_cache();
  void handle_fullhelpcode();
  void handle_fullhelpcode_during_creating();
  bool will_trigger_singlehelpcode_mode();
  void handle_singlehelpcode();
  void handle_singlehelpcode_during_creating();
};

FanimeCandidateList::FanimeCandidateList(FanimeEngine *engine, fcitx::InputContext *ic, const std::string &code) : engine_(engine), ic_(ic), code_(code) {
  boost::algorithm::to_lower(code_);
  setPageable(this);
  setCursorMovable(this);
  // #ifdef FAN_DEBUG
  auto start = std::chrono::high_resolution_clock::now();
  // #endif
  cand_size_ = generate(); // generate actually
                           // #ifdef FAN_DEBUG
  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double, std::milli> duration_ms = end - start;
  FCITX_INFO() << "fany generate time: " << duration_ms.count();
  if (duration_ms.count() > 5)
    logger_->info("time warning: " + std::to_string(duration_ms.count()) + " " + code);
  // #endif
  for (int i = 0; i < cand_size_; i++) { // generate indices of candidate window
    const char label[2] = {static_cast<char>('0' + (i + 1)), '\0'};
    labels_[i].append(label);
    labels_[i].append(". ");
  }
}

void FanimeCandidateList::prev() {
  if (!hasPrev()) {
    return;
  }
  int cur_page = engine_->get_cand_page_idx() - 1;
  engine_->set_cand_page_idx(cur_page);
  long unsigned int vec_size = FanimeEngine::current_candidates.size() - cur_page * CANDIDATE_SIZE > CANDIDATE_SIZE ? CANDIDATE_SIZE : FanimeEngine::current_candidates.size();
  for (long unsigned int i = 0; i < CANDIDATE_SIZE; i++) {
    if (i < vec_size) {
      std::string cur_han_words = std::get<1>(FanimeEngine::current_candidates[i + cur_page * CANDIDATE_SIZE]);
      candidates_[i] = std::make_unique<FanimeCandidateWord>(engine_, cur_han_words + PinyinUtil::compute_helpcodes(cur_han_words));
    }
  }
  if (vec_size == 0) {
    candidates_[0] = std::make_unique<FanimeCandidateWord>(engine_, "😍");
  }
  cand_size_ = vec_size;
  for (int i = 0; i < cand_size_; i++) { // generate indices of candidate window
    const char label[2] = {static_cast<char>('0' + (i + 1)), '\0'};
    labels_[i].clear();
    labels_[i].append(label);
    labels_[i].append(". ");
  }
}

void FanimeCandidateList::next() {
  if (!hasNext()) {
    return;
  }
  int cur_page = engine_->get_cand_page_idx() + 1;
  engine_->set_cand_page_idx(cur_page);
  long unsigned int vec_size = FanimeEngine::current_candidates.size() - cur_page * CANDIDATE_SIZE > CANDIDATE_SIZE ? CANDIDATE_SIZE : FanimeEngine::current_candidates.size() - cur_page * CANDIDATE_SIZE;
  for (long unsigned int i = 0; i < CANDIDATE_SIZE; i++) {
    if (i < vec_size) {
      std::string cur_han_words = std::get<1>(FanimeEngine::current_candidates[i + cur_page * CANDIDATE_SIZE]);
      candidates_[i] = std::make_unique<FanimeCandidateWord>(engine_, cur_han_words + PinyinUtil::compute_helpcodes(cur_han_words));
    }
  }
  if (vec_size == 0) {
    candidates_[0] = std::make_unique<FanimeCandidateWord>(engine_, "😍");
  }
  cand_size_ = vec_size;
  for (int i = 0; i < cand_size_; i++) { // generate indices of candidate window
    const char label[2] = {static_cast<char>('0' + (i + 1)), '\0'};
    labels_[i].clear();
    labels_[i].append(label);
    labels_[i].append(". ");
  }
}

bool FanimeCandidateList::hasPrev() const {
  if (engine_->get_cand_page_idx() > 0) {
    return true;
  }
  return false;
}

bool FanimeCandidateList::hasNext() const {
  int total_page = static_cast<int>(FanimeEngine::current_candidates.size()) / CANDIDATE_SIZE;
  if (static_cast<int>(FanimeEngine::current_candidates.size()) % CANDIDATE_SIZE > 0 && FanimeEngine::current_candidates.size() > CANDIDATE_SIZE) {
    total_page += 1;
  }
  if (engine_->get_cand_page_idx() < (total_page - 1)) {
    return true;
  }
  return false;
}

std::unique_ptr<Log> FanimeCandidateList::logger_ = std::make_unique<Log>(PinyinUtil::get_home_path() + "/.local/share/fcitx5-fanime/app.log");

int FanimeCandidateList::generate() {
  FanimeEngine::pure_pinyin = code_;
  FanimeEngine::seg_pinyin = PinyinUtil::pinyin_segmentation(code_);
  FanimeEngine::supposed_han_cnt = boost::count(FanimeEngine::seg_pinyin, '\'') + 1;
  FanimeEngine::can_create_word = PinyinUtil::is_all_complete_pinyin(FanimeEngine::pure_pinyin, FanimeEngine::seg_pinyin) || will_trigger_singlehelpcode_mode() || engine_->get_use_fullhelpcode();

  if (FanimeEngine::during_creating) {
    // 处理辅助码的情况，如果有辅助码，就筛一下
    if (engine_->get_use_fullhelpcode()) {
      handle_fullhelpcode_during_creating();
      FanimeEngine::supposed_han_cnt = boost::count(PinyinUtil::pinyin_segmentation(engine_->get_raw_pinyin()), '\'') + 1;
    } else if (will_trigger_singlehelpcode_mode()) {
      FanimeEngine::current_candidates = FanimeEngine::fan_dict.generate_for_creating_word(code_.substr(0, code_.size() - 1));
      handle_singlehelpcode_during_creating();
      FanimeEngine::supposed_han_cnt -= 1;
    } else {
      FanimeEngine::current_candidates = FanimeEngine::fan_dict.generate_for_creating_word(code_);
    }
  } else {
    if (engine_->get_use_fullhelpcode()) {
      handle_fullhelpcode();
      FanimeEngine::supposed_han_cnt = boost::count(PinyinUtil::pinyin_segmentation(engine_->get_raw_pinyin()), '\'') + 1;
    } else if (will_trigger_singlehelpcode_mode()) { // 默认的单码辅助
      handle_singlehelpcode();
      FanimeEngine::supposed_han_cnt -= 1;
    } else {
      bool need_query = true;
      for (auto item : FanimeEngine::cached_buffer) {
        if (item.first == code_) {
          FanimeEngine::current_candidates = item.second;
          need_query = false;
          break;
        }
      }
      if (need_query) {
        FanimeEngine::current_candidates = FanimeEngine::fan_dict.generate(code_);
      }
      if (FanimeEngine::current_candidates.size() > 0)
        FanimeEngine::cached_buffer.push_front(std::make_pair(code_, FanimeEngine::current_candidates));
      else {
        std::string quanpin_seg_str = PinyinUtil::convert_seg_shuangpin_to_seg_complete_pinyin(PinyinUtil::pinyin_segmentation(code_));
        // FCITX_INFO() << "quanpin google: " << quanpin_seg_str;
        // FCITX_INFO() << "quanpin google: " << engine_->get_raw_pinyin();
        std::string sentence = FanimeEngine::fan_dict.search_sentence_from_ime_engine(quanpin_seg_str); // 使用谷歌拼音输入法引擎进行造句
        FanimeEngine::current_candidates.clear();
        FanimeEngine::current_candidates.push_back(std::make_tuple(engine_->get_raw_pinyin(), sentence, 0));
      }
      generate_from_cache();
    }
  }

  engine_->set_cand_page_idx(0);
  long unsigned int vec_size = FanimeEngine::current_candidates.size() > CANDIDATE_SIZE ? CANDIDATE_SIZE : FanimeEngine::current_candidates.size();
  // 放到实际的候选列表里面去
  for (long unsigned int i = 0; i < CANDIDATE_SIZE; i++) {
    if (i < vec_size) {
      std::string cur_han_words = std::get<1>(FanimeEngine::current_candidates[i]);
      if (PinyinUtil::cnt_han_chars(cur_han_words) > 2) {
        candidates_[i] = std::make_unique<FanimeCandidateWord>(engine_, cur_han_words + PinyinUtil::compute_helpcodes(cur_han_words.substr(0, PinyinUtil::get_first_char_size(cur_han_words))));
      } else {
        candidates_[i] = std::make_unique<FanimeCandidateWord>(engine_, cur_han_words + PinyinUtil::compute_helpcodes(cur_han_words));
      }
    }
  }
  if (vec_size == 0) {
    candidates_[0] = std::make_unique<FanimeCandidateWord>(engine_, code_);
    return 1;
  }
  return vec_size;
}

void FanimeCandidateList::generate_from_cache() {
  // 如果没查到或者已经查到的也不合适，就补上拼音子串的结果用来给接下来的造词使用
  std::string seg_pinyin = PinyinUtil::pinyin_segmentation(code_);
  while (true) {
    size_t pos = seg_pinyin.rfind('\'');
    if (pos != std::string::npos) {
      seg_pinyin = seg_pinyin.substr(0, pos);
      std::string pure_pinyin = boost::algorithm::replace_all_copy(seg_pinyin, "'", "");
      for (auto item : FanimeEngine::cached_buffer)
        if (item.first == pure_pinyin) {
          FanimeEngine::current_candidates.insert(FanimeEngine::current_candidates.end(), item.second.begin(), item.second.end());
          break;
        }
    } else
      break;
  }
}

void FanimeCandidateList::handle_fullhelpcode() {
  std::vector<DictionaryUlPb::WordItem> tmp_cand_list;
  bool need_to_query = true;
  for (auto item : FanimeEngine::cached_buffer)
    if (item.first == engine_->get_raw_pinyin()) {
      tmp_cand_list = item.second;
      need_to_query = false;
      break;
    }
  if (need_to_query)
    tmp_cand_list = FanimeEngine::fan_dict.generate(engine_->get_raw_pinyin());
  // 把辅助码过滤前的结果加入缓存，不能把辅助码带上
  FanimeEngine::cached_buffer.push_front(std::make_pair(engine_->get_raw_pinyin(), tmp_cand_list));
  if (engine_->get_raw_pinyin().size() == 2) {
    if (code_.size() == 3) {
      for (const auto &cand : tmp_cand_list) {
        std::string cur_han_words = std::get<1>(cand);
        size_t cplen = PinyinUtil::get_first_char_size(cur_han_words);
        if (PinyinUtil::helpcode_keymap.count(cur_han_words.substr(0, cplen)) && PinyinUtil::helpcode_keymap[cur_han_words.substr(0, cplen)][0] == code_[code_.size() - 1]) {
          FanimeEngine::current_candidates.push_back(cand);
        }
      }
    } else if (code_.size() == 4) {
      for (const auto &cand : tmp_cand_list) {
        std::string cur_han_words = std::get<1>(cand);
        size_t cplen = PinyinUtil::get_first_char_size(cur_han_words);
        if (PinyinUtil::helpcode_keymap.count(cur_han_words.substr(0, cplen)) && PinyinUtil::helpcode_keymap[cur_han_words.substr(0, cplen)] == code_.substr(2, 2)) {
          FanimeEngine::current_candidates.push_back(cand);
        }
      }
    }
  } else { // engine_->get_raw_pinyin().size() >= 4
    // 把双字和单字提取出来(如果有的话)
    if (code_.size() % 2) {
      auto tmp_code_ = code_;
      code_ = code_.substr(0, 5);
      generate_from_cache();
      code_ = tmp_code_;
      tmp_cand_list = FanimeEngine::current_candidates;
      FanimeEngine::current_candidates.clear();
      for (const auto &cand : tmp_cand_list) {
        std::string cur_han_words = std::get<1>(cand);
        size_t cplen = PinyinUtil::get_first_char_size(cur_han_words);
        if (PinyinUtil::helpcode_keymap.count(cur_han_words.substr(0, cplen)) && PinyinUtil::helpcode_keymap[cur_han_words.substr(0, cplen)][0] == code_[code_.size() - 1]) {
          FanimeEngine::current_candidates.push_back(cand);
        }
      }
    } else if (code_.size() >= 6) {
      auto tmp_code_ = code_;
      code_ = code_.substr(0, 6);
      generate_from_cache();
      code_ = tmp_code_;
      tmp_cand_list = FanimeEngine::current_candidates;
      FanimeEngine::current_candidates.clear();
      for (const auto &cand : tmp_cand_list) {
        std::string cur_han_words = std::get<1>(cand);
        size_t cplen = PinyinUtil::get_first_char_size(cur_han_words);
        size_t han_cnt = PinyinUtil::cnt_han_chars(cur_han_words);
        if (han_cnt == 2) {
          // clang-format off
          if (PinyinUtil::helpcode_keymap.count(cur_han_words.substr(0, cplen))
            && PinyinUtil::helpcode_keymap[cur_han_words.substr(0, cplen)][0] == code_[code_.size() - 2]
            && PinyinUtil::helpcode_keymap.count(cur_han_words.substr(cplen, cur_han_words.size() - cplen))
            && PinyinUtil::helpcode_keymap[cur_han_words.substr(cplen, cur_han_words.size() - cplen)][0] == code_[code_.size() - 1]) {
            FanimeEngine::current_candidates.push_back(cand);
          }
          // clang-format on
        } else if (han_cnt == 1) {
          if (PinyinUtil::helpcode_keymap.count(cur_han_words.substr(0, cplen)) && PinyinUtil::helpcode_keymap[cur_han_words.substr(0, cplen)] == code_.substr(code_.size() - 2, 2)) {
            FanimeEngine::current_candidates.push_back(cand);
          }
        }
      }
    }
  }
}

void FanimeCandidateList::handle_fullhelpcode_during_creating() {
  std::vector<DictionaryUlPb::WordItem> tmp_cand_list_with_helpcode_trimed = FanimeEngine::fan_dict.generate_for_creating_word(engine_->get_raw_pinyin());
  FanimeEngine::current_candidates.clear();

  if (engine_->get_raw_pinyin().size() == 2) {
    if (code_.size() == 3) {
      for (const auto &cand : tmp_cand_list_with_helpcode_trimed) {
        std::string cur_han_words = std::get<1>(cand);
        size_t cplen = PinyinUtil::get_first_char_size(cur_han_words);
        if (PinyinUtil::helpcode_keymap.count(cur_han_words.substr(0, cplen)) && PinyinUtil::helpcode_keymap[cur_han_words.substr(0, cplen)][0] == code_[code_.size() - 1]) {
          FanimeEngine::current_candidates.push_back(cand);
        }
      }
    } else if (code_.size() == 4) {
      for (const auto &cand : tmp_cand_list_with_helpcode_trimed) {
        std::string cur_han_words = std::get<1>(cand);
        size_t cplen = PinyinUtil::get_first_char_size(cur_han_words);
        if (PinyinUtil::helpcode_keymap.count(cur_han_words.substr(0, cplen)) && PinyinUtil::helpcode_keymap[cur_han_words.substr(0, cplen)] == code_.substr(2, 2)) {
          FanimeEngine::current_candidates.push_back(cand);
        }
      }
    }
  } else { // engine_->get_raw_pinyin().size() >= 4
    // 把双字和单字提取出来(如果有的话)
    if (code_.size() % 2) {
      for (const auto &cand : tmp_cand_list_with_helpcode_trimed) {
        std::string cur_han_words = std::get<1>(cand);
        size_t cplen = PinyinUtil::get_first_char_size(cur_han_words);
        if (PinyinUtil::helpcode_keymap.count(cur_han_words.substr(0, cplen)) && PinyinUtil::helpcode_keymap[cur_han_words.substr(0, cplen)][0] == code_[code_.size() - 1]) {
          FanimeEngine::current_candidates.push_back(cand);
        }
      }
    } else if (code_.size() >= 6) {
      for (const auto &cand : tmp_cand_list_with_helpcode_trimed) {
        std::string cur_han_words = std::get<1>(cand);
        size_t cplen = PinyinUtil::get_first_char_size(cur_han_words);
        size_t han_cnt = PinyinUtil::cnt_han_chars(cur_han_words);
        if (han_cnt == 2) {
          // clang-format off
          if (PinyinUtil::helpcode_keymap.count(cur_han_words.substr(0, cplen))
            && PinyinUtil::helpcode_keymap[cur_han_words.substr(0, cplen)][0] == code_[code_.size() - 2]
            && PinyinUtil::helpcode_keymap.count(cur_han_words.substr(cplen, cur_han_words.size() - cplen))
            && PinyinUtil::helpcode_keymap[cur_han_words.substr(cplen, cur_han_words.size() - cplen)][0] == code_[code_.size() - 1]) {
            FanimeEngine::current_candidates.push_back(cand);
          }
          // clang-format on
        } else if (han_cnt == 1) {
          if (PinyinUtil::helpcode_keymap.count(cur_han_words.substr(0, cplen)) && PinyinUtil::helpcode_keymap[cur_han_words.substr(0, cplen)] == code_.substr(code_.size() - 2, 2)) {
            FanimeEngine::current_candidates.push_back(cand);
          }
        }
      }
    }
  }
}

bool FanimeCandidateList::will_trigger_singlehelpcode_mode() {
  // 至少三码才能三码
  if (code_.size() < 3 || code_.size() % 2 != 1)
    return false;
  std::string seg_pinyin = PinyinUtil::pinyin_segmentation(code_);
  bool really = true;
  for (size_t i = 2; i < seg_pinyin.size(); i += 3) {
    if (seg_pinyin[i] != '\'') {
      really = false;
      break;
    }
  }
  return really;
}

void FanimeCandidateList::handle_singlehelpcode() {
#ifdef FAN_DEBUG
  auto start = std::chrono::high_resolution_clock::now();
#endif
  generate_from_cache();
#ifdef FAN_DEBUG
  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double, std::milli> duration_ms = end - start;
  FCITX_INFO() << "fany cache time: " << duration_ms.count();
#endif
  std::vector<DictionaryUlPb::WordItem> tmp_cand_list_with_helpcode_trimed = FanimeEngine::current_candidates;
  FanimeEngine::current_candidates.clear();
  size_t most_matched_han_cnt = (code_.size() - 1) / 2;
  std::vector<DictionaryUlPb::WordItem> first_helpcode_matched_list;
  std::vector<DictionaryUlPb::WordItem> last_helpcode_matched_list;
  std::vector<DictionaryUlPb::WordItem> other_first_helpcode_matched_list;
  std::vector<DictionaryUlPb::WordItem> not_matched_list;
  // 先根据辅助码进行筛选
  for (const auto &cand : tmp_cand_list_with_helpcode_trimed) {
    std::string cur_han_words = std::get<1>(cand);
    size_t han_cnt = PinyinUtil::cnt_han_chars(cur_han_words);
    size_t first_han_char_size = PinyinUtil::get_first_char_size(cur_han_words);
    size_t last_han_char_size = PinyinUtil::get_last_char_size(cur_han_words);
    if (han_cnt == most_matched_han_cnt) {
      if (PinyinUtil::helpcode_keymap.count(cur_han_words.substr(0, first_han_char_size)) && PinyinUtil::helpcode_keymap[cur_han_words.substr(0, first_han_char_size)][0] == code_[code_.size() - 1])
        first_helpcode_matched_list.push_back(cand);
      // 对于两字、三字词，使最后一个字也可以成为辅助码
      else if ((han_cnt == 2 || han_cnt == 3) && PinyinUtil::helpcode_keymap.count(cur_han_words.substr(cur_han_words.size() - last_han_char_size, last_han_char_size)) && PinyinUtil::helpcode_keymap[cur_han_words.substr(cur_han_words.size() - last_han_char_size, last_han_char_size)][0] == code_[code_.size() - 1])
        last_helpcode_matched_list.push_back(cand);
    } else {
      if (PinyinUtil::helpcode_keymap.count(cur_han_words.substr(0, first_han_char_size)) && PinyinUtil::helpcode_keymap[cur_han_words.substr(0, first_han_char_size)][0] == code_[code_.size() - 1])
        other_first_helpcode_matched_list.push_back(cand);
      else
        not_matched_list.push_back(cand);
    }
  }
  if (first_helpcode_matched_list.size() > 0)
    FanimeEngine::current_candidates.insert(FanimeEngine::current_candidates.end(), first_helpcode_matched_list.begin(), first_helpcode_matched_list.end());
  if (last_helpcode_matched_list.size() > 0)
    FanimeEngine::current_candidates.insert(FanimeEngine::current_candidates.end(), last_helpcode_matched_list.begin(), last_helpcode_matched_list.end());
  if (other_first_helpcode_matched_list.size() > 0)
    FanimeEngine::current_candidates.insert(FanimeEngine::current_candidates.end(), other_first_helpcode_matched_list.begin(), other_first_helpcode_matched_list.end());
  // 然后当作不完整的拼音来进行模糊查询得到的结果紧随着放在后面
#ifdef FAN_DEBUG
  // start = std::chrono::high_resolution_clock::now();
#endif
  auto tmp_cand_list = FanimeEngine::fan_dict.generate(code_);
#ifdef FAN_DEBUG
  // end = std::chrono::high_resolution_clock::now();
  // duration_ms = end - start;
  // FCITX_INFO() << "fany dict generate time: " << duration_ms.count() << " " << code_;
#endif
  FanimeEngine::current_candidates.insert(FanimeEngine::current_candidates.end(), tmp_cand_list.begin(), tmp_cand_list.end());
  // 把第一步中筛掉的那些数据排在最后
  if (not_matched_list.size() > 0)
    FanimeEngine::current_candidates.insert(FanimeEngine::current_candidates.end(), not_matched_list.begin(), not_matched_list.end());
}

void FanimeCandidateList::handle_singlehelpcode_during_creating() {
  std::vector<DictionaryUlPb::WordItem> tmp_cand_list_with_helpcode_trimed = FanimeEngine::fan_dict.generate_for_creating_word(code_.substr(0, code_.size() - 1));
  FanimeEngine::current_candidates.clear();
  size_t most_matched_han_cnt = (code_.size() - 1) / 2;
  std::vector<DictionaryUlPb::WordItem> first_helpcode_matched_list;
  std::vector<DictionaryUlPb::WordItem> last_helpcode_matched_list;
  std::vector<DictionaryUlPb::WordItem> other_first_helpcode_matched_list;
  std::vector<DictionaryUlPb::WordItem> not_matched_list;
  // 先根据辅助码进行筛选
  for (const auto &cand : tmp_cand_list_with_helpcode_trimed) {
    std::string cur_han_words = std::get<1>(cand);
    size_t han_cnt = PinyinUtil::cnt_han_chars(cur_han_words);
    size_t first_han_char_size = PinyinUtil::get_first_char_size(cur_han_words);
    size_t last_han_char_size = PinyinUtil::get_last_char_size(cur_han_words);
    if (han_cnt == most_matched_han_cnt) {
      if (PinyinUtil::helpcode_keymap.count(cur_han_words.substr(0, first_han_char_size)) && PinyinUtil::helpcode_keymap[cur_han_words.substr(0, first_han_char_size)][0] == code_[code_.size() - 1])
        first_helpcode_matched_list.push_back(cand);
      // 对于两字、三字词，使最后一个字也可以成为辅助码
      else if ((han_cnt == 2 || han_cnt == 3) && PinyinUtil::helpcode_keymap.count(cur_han_words.substr(cur_han_words.size() - last_han_char_size, last_han_char_size)) && PinyinUtil::helpcode_keymap[cur_han_words.substr(cur_han_words.size() - last_han_char_size, last_han_char_size)][0] == code_[code_.size() - 1])
        last_helpcode_matched_list.push_back(cand);
    } else {
      if (PinyinUtil::helpcode_keymap.count(cur_han_words.substr(0, first_han_char_size)) && PinyinUtil::helpcode_keymap[cur_han_words.substr(0, first_han_char_size)][0] == code_[code_.size() - 1])
        other_first_helpcode_matched_list.push_back(cand);
      else
        not_matched_list.push_back(cand);
    }
  }
  if (first_helpcode_matched_list.size() > 0)
    FanimeEngine::current_candidates.insert(FanimeEngine::current_candidates.end(), first_helpcode_matched_list.begin(), first_helpcode_matched_list.end());
  if (last_helpcode_matched_list.size() > 0)
    FanimeEngine::current_candidates.insert(FanimeEngine::current_candidates.end(), last_helpcode_matched_list.begin(), last_helpcode_matched_list.end());
  if (other_first_helpcode_matched_list.size() > 0)
    FanimeEngine::current_candidates.insert(FanimeEngine::current_candidates.end(), other_first_helpcode_matched_list.begin(), other_first_helpcode_matched_list.end());
  // 然后当作不完整的拼音来进行模糊查询得到的结果紧随着放在后面
  auto tmp_cand_list = FanimeEngine::fan_dict.generate(code_);
  FanimeEngine::current_candidates.insert(FanimeEngine::current_candidates.end(), tmp_cand_list.begin(), tmp_cand_list.end());
  // 把第一步中筛掉的那些数据排在最后
  if (not_matched_list.size() > 0)
    FanimeEngine::current_candidates.insert(FanimeEngine::current_candidates.end(), not_matched_list.begin(), not_matched_list.end());
}

} // namespace

std::unique_ptr<::Log> FanimeState::logger = std::make_unique<Log>(PinyinUtil::get_home_path() + "/.local/share/fcitx5-fanime/app.log");
void FanimeState::keyEvent(fcitx::KeyEvent &event) {
  // 如果候选列表不为空，那么，在键入数字键之后，就可以将候选项选中并且上屏了
  if (auto candidateList = ic_->inputPanel().candidateList()) {
    // 数字键的情况
    int idx = event.key().keyListIndex(selectionKeys);
    GlobalIME::need_to_update_weight = true; // 需要更新权重
    // use space key to commit first candidate
    if (idx == selectionKeys.size() - 1) {
      idx = 0;
      GlobalIME::need_to_update_weight = false;
    }
    if (event.key().check(FcitxKey_comma) || event.key().check(FcitxKey_period)) {
      idx = 0;
      GlobalIME::need_to_update_weight = false;
    }
    if (idx >= 0 && idx < candidateList->size() + 1) {
      event.accept();
      candidateList->candidate(idx).select(ic_);
      if (event.key().check(FcitxKey_comma))
        ic_->commitString("，");
      if (event.key().check(FcitxKey_period))
        ic_->commitString("。");
      return;
    }
    // 翻页键的情况，全局默认的是上箭头和下箭头
    // 向前翻页 -> 上箭头或者
    if (event.key().checkKeyList(engine_->instance()->globalConfig().defaultPrevPage()) || event.key().check(FcitxKey_minus)) {
      if (auto *pageable = candidateList->toPageable(); pageable && pageable->hasPrev()) {
        event.accept();
        pageable->prev();
        ic_->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
      }
      return event.filterAndAccept();
    }

    // 向后翻页或者 TAB 键
    if (event.key().checkKeyList(engine_->instance()->globalConfig().defaultNextPage()) || event.key().check(FcitxKey_equal) || event.key().check(FcitxKey_Tab)) {
      if (auto *pageable = candidateList->toPageable(); pageable && pageable->hasNext()) {
        if (event.key().check(FcitxKey_Tab)) {
          // 在非造字模式的情况下，只有单字和双字可以用完整的辅助码
          if (buffer_.userInput().size() % 2 != 1) {
            engine_->set_use_fullhelpcode(true);
            engine_->set_raw_pinyin(buffer_.userInput());
            auto &inputPanel = ic_->inputPanel();
            // 嵌在候选框中的 preedit
            std::string aux = "🪓"; // 作个标记(辅助码的“斧”)
            fcitx::Text preedit(FanimeEngine::word_to_be_created + PinyinUtil::pinyin_segmentation(buffer_.userInput()) + aux);
            inputPanel.setPreedit(preedit);
            ic_->updatePreedit();
          }
        }
        pageable->next();
        ic_->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
      }
      return event.filterAndAccept();
    }
  }

  if (buffer_.empty()) {                                              // current text buffer is empty
    if (!checkAlpha(event.key().keySymToString(event.key().sym()))) { // current text is empty and not alpha
      // if it gonna commit something
      auto c = fcitx::Key::keySymToUnicode(event.key().sym());
      if (!c) {
        return;
      }
      std::string punc, puncAfter;

      // skip key pad
      if (c && !event.key().isKeyPad()) {
        std::tie(punc, puncAfter) = engine_->punctuation()->call<fcitx::IPunctuation::pushPunctuationV2>("zh_CN", ic_, c);
      }

      //  TODO: 重新设计 quickphrase
      /*
      if (event.key().check(FcitxKey_semicolon) && engine_->quickphrase()) {
        auto keyString = fcitx::utf8::UCS4ToUTF8(c);
        // s is punc or key
        auto output = !punc.empty() ? (punc + puncAfter) : keyString;
        // alt is key or empty
        auto altOutput = !punc.empty() ? keyString : "";
        // if no punc: key -> key (s = key, alt = empty)
        // if there's punc: key -> punc, return -> key (s = punc, alt =
        // key)
        std::string text;
        engine_->quickphrase()->call<fcitx::IQuickPhrase::trigger>(ic_, text, "", output, altOutput, fcitx::Key(FcitxKey_semicolon));
        event.filterAndAccept();
        return;
      }
      */
      if (!punc.empty()) {
        event.filterAndAccept();
        if (event.key().check(FcitxKey_grave)) {
          event.filterAndAccept();
          ic_->commitString("·");
        } else if (event.key().check(FcitxKey_asciitilde)) {
          ic_->commitString("~");
        } else {
          ic_->commitString(punc + puncAfter);
        }
        if (size_t length = fcitx::utf8::lengthValidated(puncAfter); length != 0 && length != fcitx::utf8::INVALID_LENGTH) {
          for (size_t i = 0; i < length; i++) {
            ic_->forwardKey(fcitx::Key(FcitxKey_Left));
          }
        }
      }
      return;
    }
  } else { // current text buffer is not empty
    if (event.key().check(FcitxKey_BackSpace)) {
      // 取消使用完整的辅助码
      if (buffer_.userInput() == engine_->get_raw_pinyin() && engine_->get_use_fullhelpcode()) {
        engine_->set_use_fullhelpcode(false);
        engine_->set_raw_pinyin("");
      } else {
        buffer_.backspace();
      }
      if (!buffer_.size())
        // 清理状态
        reset();
      updateUI();
      return event.filterAndAccept();
    }
    if (event.key().check(FcitxKey_Return)) {
      ic_->commitString(buffer_.userInput());
      reset();
      return event.filterAndAccept();
    }
    if (event.key().check(FcitxKey_Escape)) {
      reset();
      return event.filterAndAccept();
    }
    if (!checkAlpha(event.key().keySymToString(event.key().sym()))) { // current text buffer is not empty, and current key pressed is not alpha
      return event.filterAndAccept();
    }
  }

  // 1. current text buffer is empty and current key pressed is alpha
  // 2. current text buffer is not empty and current key pressed is alpha
  buffer_.type(event.key().sym()); // update buffer_, so when the fucking event itself is updated?
  updateUI();
  return event.filterAndAccept();
}

void FanimeState::setCode(std::string code) {
  buffer_.clear();
  buffer_.type(code);
  updateUI();
}

void FanimeState::updateUI() {
  auto &inputPanel = ic_->inputPanel(); // also need to track the initialization of ic_
  inputPanel.reset();
  FanimeEngine::current_candidates.clear();
  if (buffer_.size() > 0) {
    inputPanel.setCandidateList(std::make_unique<FanimeCandidateList>(engine_, ic_, buffer_.userInput()));
    // 嵌在候选框中的 preedit
    std::string aux("");
    if (engine_->get_use_fullhelpcode())
      aux = "🪓"; // 作个标记(辅助码的“斧”)
    fcitx::Text preedit(FanimeEngine::word_to_be_created + PinyinUtil::pinyin_segmentation(buffer_.userInput()) + aux);
    inputPanel.setPreedit(preedit);
    // 嵌在具体的应用中的 preedit
    // fcitx::Text clientPreedit(FanimeEngine::word_to_be_created + PinyinUtil::extract_preview(ic_->inputPanel().candidateList()->candidate(0).text().toString()), fcitx::TextFormatFlag::Underline);
    fcitx::Text clientPreedit(buffer_.userInput(), fcitx::TextFormatFlag::Underline);
    // TODO: 这里无论如何设置，在 chrome 中不生效，鉴定为 chrome 系列的问题，当然，firefox 也有类似的问题，不尽相同。以后有机会可以去看看能否提个 PR
    // clientPreedit.setCursor(PinyinUtil::extract_preview(ic_->inputPanel().candidateList()->candidate(0).text().toString()).size());
    clientPreedit.setCursor(0);
    inputPanel.setClientPreedit(clientPreedit); // 嵌在应用程序中的
  } else {
    fcitx::Text clientPreedit(buffer_.userInput());
    inputPanel.setClientPreedit(clientPreedit); // 嵌在应用程序中的
  }
  ic_->updatePreedit();
  ic_->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
}

void FanimeState::reset() {
  buffer_.clear();
  engine_->set_use_fullhelpcode(false);
  engine_->set_raw_pinyin("");
  FanimeEngine::cached_buffer.clear(); // 清理缓存
  FanimeEngine::current_candidates.clear();
  FanimeEngine::during_creating = false;
  FanimeEngine::word_to_be_created = "";
  FanimeEngine::word_pinyin = "";
  updateUI();
}

fcitx::InputBuffer &FanimeState::getBuffer() { return buffer_; }

//
//~:D FanimeEngine
//
DictionaryUlPb FanimeEngine::fan_dict = DictionaryUlPb();
boost::circular_buffer<std::pair<std::string, std::vector<DictionaryUlPb::WordItem>>> FanimeEngine::cached_buffer(20);
std::vector<DictionaryUlPb::WordItem> FanimeEngine::current_candidates;
size_t FanimeEngine::current_page_idx;
std::string FanimeEngine::pure_pinyin("");
std::string FanimeEngine::seg_pinyin("");
size_t FanimeEngine::supposed_han_cnt = 0;
bool FanimeEngine::can_create_word = false;
std::string FanimeEngine::word_to_be_created;
std::string FanimeEngine::word_pinyin("");
bool FanimeEngine::during_creating = false;

FanimeEngine::FanimeEngine(fcitx::Instance *instance) : instance_(instance), factory_([this](fcitx::InputContext &ic) { return new FanimeState(this, &ic); }) { instance->inputContextManager().registerProperty("fanimeState", &factory_); }

void FanimeEngine::activate(const fcitx::InputMethodEntry &entry, fcitx::InputContextEvent &event) {
  FCITX_UNUSED(entry);
  auto *inputContext = event.inputContext();
  // Request full width.
  fullwidth();
  chttrans();
  for (const auto *actionName : {"chttrans", "punctuation", "fullwidth"}) {
    if (auto *action = instance_->userInterfaceManager().lookupAction(actionName)) {
      inputContext->statusArea().addAction(fcitx::StatusGroup::InputMethod, action);
    }
  }
}

void FanimeEngine::deactivate(const fcitx::InputMethodEntry &entry, fcitx::InputContextEvent &event) {
  auto *inputContext = event.inputContext();
  do {
    if (event.type() != fcitx::EventType::InputContextSwitchInputMethod) {
      break;
    }
    FanimeState *state = inputContext->propertyFor(&factory_);
    inputContext->commitString(state->getBuffer().userInput()); // 切换输入法到英文时，原样 commit 已经输入的字符串
  } while (0);
  reset(entry, event);
}

void FanimeEngine::keyEvent(const fcitx::InputMethodEntry &entry, fcitx::KeyEvent &keyEvent) {
  FCITX_UNUSED(entry);
  if (keyEvent.isRelease() || keyEvent.key().states()) {
    return;
  }
  // FCITX_INFO() << keyEvent.key() << " isRelease=" << keyEvent.isRelease();
  auto ic = keyEvent.inputContext();
  auto *state = ic->propertyFor(&factory_);
  state->keyEvent(keyEvent);
}

void FanimeEngine::reset(const fcitx::InputMethodEntry &, fcitx::InputContextEvent &event) {
  auto *state = event.inputContext()->propertyFor(&factory_);
  set_use_fullhelpcode(false);
  set_raw_pinyin("");
  state->reset();
}

FCITX_ADDON_FACTORY(FanimeEngineFactory);
