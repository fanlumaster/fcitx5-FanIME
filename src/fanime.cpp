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
#include <chrono>
#include <boost/locale.hpp>
#include <boost/circular_buffer.hpp>

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
    inputContext->commitString(text_to_commit);
    // TODO: 清理缓存
    auto state = inputContext->propertyFor(engine_->factory());
    state->reset();
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
  std::vector<DictionaryUlPb::WordItem> cur_candidates_;
  int cur_page_;
  std::string code_;
  int cursor_ = 0;
  int cand_size_ = CANDIDATE_SIZE;
  static DictionaryUlPb dict_;
  static std::unique_ptr<Log> logger_;
  static boost::circular_buffer<std::pair<std::string, std::vector<DictionaryUlPb::WordItem>>> cached_buffer_;

  // generate words
  int generate();
  void handle_fullhelpcode();
  bool is_need_singlehelpcode();
  void handle_singlehelpcode();
};

FanimeCandidateList::FanimeCandidateList(FanimeEngine *engine, fcitx::InputContext *ic, const std::string &code) : engine_(engine), ic_(ic), code_(code) {
  boost::algorithm::to_lower(code_);
  setPageable(this);
  setCursorMovable(this);
  auto start = std::chrono::high_resolution_clock::now();
  cand_size_ = generate(); // generate actually
  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double, std::milli> duration_ms = end - start;
  FCITX_INFO() << "fany generate time: " << duration_ms.count();
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
  cur_page_ -= 1;
  long unsigned int vec_size = cur_candidates_.size() - cur_page_ * CANDIDATE_SIZE > CANDIDATE_SIZE ? CANDIDATE_SIZE : cur_candidates_.size();
  for (long unsigned int i = 0; i < CANDIDATE_SIZE; i++) {
    if (i < vec_size) {
      std::string cur_han_words = std::get<1>(cur_candidates_[i + cur_page_ * CANDIDATE_SIZE]);
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
  cur_page_ += 1;
  long unsigned int vec_size = cur_candidates_.size() - cur_page_ * CANDIDATE_SIZE > CANDIDATE_SIZE ? CANDIDATE_SIZE : cur_candidates_.size() - cur_page_ * CANDIDATE_SIZE;
  for (long unsigned int i = 0; i < CANDIDATE_SIZE; i++) {
    if (i < vec_size) {
      std::string cur_han_words = std::get<1>(cur_candidates_[i + cur_page_ * CANDIDATE_SIZE]);
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
  if (cur_page_ > 0) {
    return true;
  }
  return false;
}

bool FanimeCandidateList::hasNext() const {
  int total_page = static_cast<int>(cur_candidates_.size()) / CANDIDATE_SIZE;
  if (static_cast<int>(cur_candidates_.size()) % CANDIDATE_SIZE > 0 && cur_candidates_.size() > CANDIDATE_SIZE) {
    total_page += 1;
  }
  if (cur_page_ < (total_page - 1)) {
    return true;
  }
  return false;
}

DictionaryUlPb FanimeCandidateList::dict_ = DictionaryUlPb();
std::unique_ptr<Log> FanimeCandidateList::logger_ = std::make_unique<Log>("/home/sonnycalcr/.local/share/fcitx5-fanyime/app.log");

boost::circular_buffer<std::pair<std::string, std::vector<DictionaryUlPb::WordItem>>> FanimeCandidateList::cached_buffer_(10);

int FanimeCandidateList::generate() {
  if (engine_->get_use_fullhelpcode()) {
    handle_fullhelpcode();
  } else if (code_.size() > 1 && code_.size() % 2 && is_need_singlehelpcode()) { // 默认的单码辅助
    handle_singlehelpcode();
  } else {
    bool need_query = true;
    for (auto item : cached_buffer_) {
      if (item.first == code_) {
        cur_candidates_ = item.second;
        need_query = false;
        break;
      }
    }
    if (need_query) {
      cur_candidates_ = dict_.generate(code_);
    }
    cached_buffer_.push_front(std::make_pair(code_, cur_candidates_));
  }
  //
  std::string seg_pinyin = PinyinUtil::pinyin_segmentation(code_);
  while (true) {
    size_t pos = seg_pinyin.rfind('\'');
    if (pos != std::string::npos) {
      seg_pinyin = seg_pinyin.substr(0, pos);
      std::string pure_pinyin = boost::algorithm::replace_all_copy(seg_pinyin, "'", "");
      for (auto item : cached_buffer_)
        if (item.first == pure_pinyin) {
          cur_candidates_.insert(cur_candidates_.end(), item.second.begin(), item.second.end());
          break;
        }
    } else
      break;
  }
  cur_page_ = 0;
  long unsigned int vec_size = cur_candidates_.size() > CANDIDATE_SIZE ? CANDIDATE_SIZE : cur_candidates_.size();
  // 放到实际的候选列表里面去
  for (long unsigned int i = 0; i < CANDIDATE_SIZE; i++) {
    if (i < vec_size) {
      std::string cur_han_words = std::get<1>(cur_candidates_[i]);
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

void FanimeCandidateList::handle_fullhelpcode() {
  std::vector<DictionaryUlPb::WordItem> tmp_cand_list;
  bool need_to_query = true;
  for (auto item : cached_buffer_)
    if (item.first == engine_->get_raw_pinyin()) {
      tmp_cand_list = item.second;
      need_to_query = false;
      break;
    }
  if (need_to_query)
    tmp_cand_list = dict_.generate(engine_->get_raw_pinyin());
  // 把辅助码过滤前的结果加入缓存，不能把辅助码带上
  cached_buffer_.push_front(std::make_pair(engine_->get_raw_pinyin(), tmp_cand_list));
  if (engine_->get_raw_pinyin().size() == 2) {
    if (code_.size() == 3) {
      for (const auto &cand : tmp_cand_list) {
        std::string cur_han_words = std::get<1>(cand);
        size_t cplen = PinyinUtil::get_first_char_size(cur_han_words);
        if (PinyinUtil::helpcode_keymap.count(cur_han_words.substr(0, cplen)) && PinyinUtil::helpcode_keymap[cur_han_words.substr(0, cplen)][0] == code_[code_.size() - 1]) {
          cur_candidates_.push_back(cand);
        }
      }
    } else if (code_.size() == 4) {
      for (const auto &cand : tmp_cand_list) {
        std::string cur_han_words = std::get<1>(cand);
        size_t cplen = PinyinUtil::get_first_char_size(cur_han_words);
        if (PinyinUtil::helpcode_keymap.count(cur_han_words.substr(0, cplen)) && PinyinUtil::helpcode_keymap[cur_han_words.substr(0, cplen)] == code_.substr(2, 2)) {
          cur_candidates_.push_back(cand);
        }
      }
    }
  } else { // engine_->get_raw_pinyin().size() == 4
    if (code_.size() == 5) {
      for (const auto &cand : tmp_cand_list) {
        std::string cur_han_words = std::get<1>(cand);
        size_t cplen = PinyinUtil::get_first_char_size(cur_han_words);
        if (PinyinUtil::helpcode_keymap.count(cur_han_words.substr(0, cplen)) && PinyinUtil::helpcode_keymap[cur_han_words.substr(0, cplen)][0] == code_[code_.size() - 1]) {
          cur_candidates_.push_back(cand);
        }
      }
    } else if (code_.size() == 6) {
      for (const auto &cand : tmp_cand_list) {
        std::string cur_han_words = std::get<1>(cand);
        size_t cplen = PinyinUtil::get_first_char_size(cur_han_words);
        // clang-format off
        if (PinyinUtil::helpcode_keymap.count(cur_han_words.substr(0, cplen))
          && PinyinUtil::helpcode_keymap[cur_han_words.substr(0, cplen)][0] == code_[code_.size() - 2]
          && PinyinUtil::helpcode_keymap.count(cur_han_words.substr(cplen, cur_han_words.size() - cplen))
          && PinyinUtil::helpcode_keymap[cur_han_words.substr(cplen, cur_han_words.size() - cplen)][0] == code_[code_.size() - 1]) {
          cur_candidates_.push_back(cand);
        }
        // clang-format on
      }
    }
  }
}

bool FanimeCandidateList::is_need_singlehelpcode() {
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
  std::vector<DictionaryUlPb::WordItem> tmp_cand_list_with_helpcode_trimed;
  bool need_to_query = true;
  for (auto item : cached_buffer_)
    if (item.first == code_.substr(0, code_.size() - 1)) {
      tmp_cand_list_with_helpcode_trimed = item.second;
      need_to_query = false;
      break;
    }
  if (need_to_query)
    tmp_cand_list_with_helpcode_trimed = dict_.generate(code_.substr(0, code_.size() - 1));
  std::vector<DictionaryUlPb::WordItem> last_helpcode_matched_list;
  for (const auto &cand : tmp_cand_list_with_helpcode_trimed) {
    std::string cur_han_words = std::get<1>(cand);
    size_t han_cnt = PinyinUtil::cnt_han_chars(cur_han_words);
    size_t cplen = PinyinUtil::get_first_char_size(cur_han_words);
    size_t last_cplen = PinyinUtil::get_last_char_size(cur_han_words);
    if (PinyinUtil::helpcode_keymap.count(cur_han_words.substr(0, cplen)) && PinyinUtil::helpcode_keymap[cur_han_words.substr(0, cplen)][0] == code_[code_.size() - 1])
      cur_candidates_.push_back(cand);
    // 对于两字、三字词，使最后一个字也可以成为辅助码
    else if ((han_cnt == 2 || han_cnt == 3) && PinyinUtil::helpcode_keymap.count(cur_han_words.substr(cur_han_words.size() - last_cplen, last_cplen)) && PinyinUtil::helpcode_keymap[cur_han_words.substr(cur_han_words.size() - last_cplen, last_cplen)][0] == code_[code_.size() - 1])
      last_helpcode_matched_list.push_back(cand);
  }
  if (last_helpcode_matched_list.size() > 0)
    cur_candidates_.insert(cur_candidates_.end(), last_helpcode_matched_list.begin(), last_helpcode_matched_list.end());
  auto tmp_cand_list = dict_.generate(code_);
  cur_candidates_.insert(cur_candidates_.end(), tmp_cand_list.begin(), tmp_cand_list.end());
}

} // namespace

std::unique_ptr<::Log> FanimeState::logger = std::make_unique<Log>("/home/sonnycalcr/.local/share/fcitx5-fanyime/app.log");
void FanimeState::keyEvent(fcitx::KeyEvent &event) {
  // 如果候选列表不为空，那么，在键入数字键之后，就可以将候选项选中并且上屏了
  if (auto candidateList = ic_->inputPanel().candidateList()) {
    // 数字键的情况
    int idx = event.key().keyListIndex(selectionKeys);
    // use space key to commit first candidate
    if (idx == selectionKeys.size() - 1) {
      idx = 0;
    }
    if (idx >= 0 && idx < candidateList->size() + 1) {
      event.accept();
      candidateList->candidate(idx).select(ic_);
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
          // FCITX_INFO() << "buffer_ " << buffer_.userInput() << " " << PinyinUtil::pinyin_segmentation(buffer_.userInput());
          // 只有单字和双字可以用完整的辅助码
          if ((buffer_.userInput().size() == 2 && PinyinUtil::pinyin_segmentation(buffer_.userInput()).size() == 2) || (buffer_.userInput().size() == 4 && PinyinUtil::pinyin_segmentation(buffer_.userInput()).size() == 5)) {
            engine_->set_use_fullhelpcode(true);
            engine_->set_raw_pinyin(buffer_.userInput());
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
      if (!punc.empty()) {
        event.filterAndAccept();
        ic_->commitString(punc + puncAfter);
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
  if (buffer_.size() > 0) {
    inputPanel.setCandidateList(std::make_unique<FanimeCandidateList>(engine_, ic_, buffer_.userInput()));
    // 嵌在候选框中的 preedit
    fcitx::Text preedit(PinyinUtil::pinyin_segmentation(buffer_.userInput()));
    inputPanel.setPreedit(preedit);
    // 嵌在具体的应用中的 preedit
    fcitx::Text clientPreedit(PinyinUtil::extract_preview(ic_->inputPanel().candidateList()->candidate(0).text().toString()), fcitx::TextFormatFlag::Underline);
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
  updateUI();
}

fcitx::InputBuffer &FanimeState::getBuffer() { return buffer_; }

FanimeEngine::FanimeEngine(fcitx::Instance *instance) : instance_(instance), factory_([this](fcitx::InputContext &ic) { return new FanimeState(this, &ic); }) {
  conv_ = iconv_open("UTF-8", "GB18030");
  if (conv_ == reinterpret_cast<iconv_t>(-1)) {
    throw std::runtime_error("Failed to create converter");
  }
  instance->inputContextManager().registerProperty("fanimeState", &factory_);
}

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
