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
#include <chrono>

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
    // inputContext->commitString("fanyfull");
    auto state = inputContext->propertyFor(engine_->factory());
    state->reset();
  }

private:
  FanimeEngine *engine_;
};

class FanimeCandidateList : public fcitx::CandidateList, public fcitx::PageableCandidateList, public fcitx::CursorMovableCandidateList {
public:
  FanimeCandidateList(FanimeEngine *engine, fcitx::InputContext *ic, const std::string &code) : engine_(engine), ic_(ic), code_(code) {
    boost::algorithm::to_lower(code_);
    setPageable(this);
    setCursorMovable(this);
    auto start = std::chrono::high_resolution_clock::now();
    cand_size = generate(); // generate actually
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> duration_ms = end - start;
    FCITX_INFO() << "fany generate time: " << duration_ms.count();
    for (int i = 0; i < cand_size; i++) { // generate indices of candidate window
      const char label[2] = {static_cast<char>('0' + (i + 1)), '\0'};
      labels_[i].append(label);
      labels_[i].append(". ");
    }
  }

  const fcitx::Text &label(int idx) const override { return labels_[idx]; }

  const fcitx::CandidateWord &candidate(int idx) const override { return *candidates_[idx]; }
  int size() const override { return cand_size; }
  fcitx::CandidateLayoutHint layoutHint() const override { return fcitx::CandidateLayoutHint::NotSet; }
  bool usedNextBefore() const override { return false; }
  void prev() override {
    if (!hasPrev()) {
      return;
    }
    cur_page_ -= 1;
    long unsigned int vec_size = cur_candidates_.size() - cur_page_ * CANDIDATE_SIZE > CANDIDATE_SIZE ? CANDIDATE_SIZE : cur_candidates_.size();
    for (long unsigned int i = 0; i < CANDIDATE_SIZE; i++) {
      if (i < vec_size) {
        candidates_[i] = std::make_unique<FanimeCandidateWord>(engine_, cur_candidates_[i + cur_page_ * CANDIDATE_SIZE] + PinyinUtil::compute_helpcodes(cur_candidates_[i + cur_page_ * CANDIDATE_SIZE]));
      }
    }
    if (vec_size == 0) {
      candidates_[0] = std::make_unique<FanimeCandidateWord>(engine_, "😍");
    }
    cand_size = vec_size;
    for (int i = 0; i < cand_size; i++) { // generate indices of candidate window
      const char label[2] = {static_cast<char>('0' + (i + 1)), '\0'};
      labels_[i].clear();
      labels_[i].append(label);
      labels_[i].append(". ");
    }
  }
  void next() override {
    if (!hasNext()) {
      return;
    }
    cur_page_ += 1;
    long unsigned int vec_size = cur_candidates_.size() - cur_page_ * CANDIDATE_SIZE > CANDIDATE_SIZE ? CANDIDATE_SIZE : cur_candidates_.size() - cur_page_ * CANDIDATE_SIZE;
    for (long unsigned int i = 0; i < CANDIDATE_SIZE; i++) {
      if (i < vec_size) {
        candidates_[i] = std::make_unique<FanimeCandidateWord>(engine_, cur_candidates_[i + cur_page_ * CANDIDATE_SIZE] + PinyinUtil::compute_helpcodes(cur_candidates_[i + cur_page_ * CANDIDATE_SIZE]));
      }
    }
    if (vec_size == 0) {
      candidates_[0] = std::make_unique<FanimeCandidateWord>(engine_, "😍");
    }
    cand_size = vec_size;
    for (int i = 0; i < cand_size; i++) { // generate indices of candidate window
      const char label[2] = {static_cast<char>('0' + (i + 1)), '\0'};
      labels_[i].clear();
      labels_[i].append(label);
      labels_[i].append(". ");
    }
  }

  bool hasPrev() const override {
    if (cur_page_ > 0) {
      return true;
    }
    return false;
  }

  bool hasNext() const override {
    int total_page = static_cast<int>(cur_candidates_.size()) / CANDIDATE_SIZE;
    if (static_cast<int>(cur_candidates_.size()) % CANDIDATE_SIZE > 0 && cur_candidates_.size() > CANDIDATE_SIZE) {
      total_page += 1;
    }
    if (cur_page_ < (total_page - 1)) {
      return true;
    }
    return false;
  }

  // TODO: 这里是什么意思
  void prevCandidate() override { cursor_ = (cursor_ + CANDIDATE_SIZE - 1) % CANDIDATE_SIZE; }
  void nextCandidate() override { cursor_ = (cursor_ + 1) % CANDIDATE_SIZE; }
  int cursorIndex() const override { return cursor_; }

private:
  // generate words
  int generate() {
    if (engine_->get_use_fullhelpcode()) {
      auto tmp_cand_list = dict.generate(engine_->get_raw_pinyin());
      if (engine_->get_raw_pinyin().size() == 2) {
        if (code_.size() == 3) {
          for (const auto &cand : tmp_cand_list) {
            size_t cplen = PinyinUtil::get_first_char_size(cand);
            if (PinyinUtil::helpcode_keymap.count(cand.substr(0, cplen)) && PinyinUtil::helpcode_keymap[cand.substr(0, cplen)][0] == code_[code_.size() - 1]) {
              cur_candidates_.push_back(cand);
            }
          }
        } else if (code_.size() == 4) {
          for (const auto &cand : tmp_cand_list) {
            size_t cplen = PinyinUtil::get_first_char_size(cand);
            if (PinyinUtil::helpcode_keymap.count(cand.substr(0, cplen)) && PinyinUtil::helpcode_keymap[cand.substr(0, cplen)] == code_.substr(2, 2)) {
              cur_candidates_.push_back(cand);
            }
          }
        }
      } else { // engine_->get_raw_pinyin().size() == 4
        if (code_.size() == 5) {
          for (const auto &cand : tmp_cand_list) {
            size_t cplen = PinyinUtil::get_first_char_size(cand);
            if (PinyinUtil::helpcode_keymap.count(cand.substr(0, cplen)) && PinyinUtil::helpcode_keymap[cand.substr(0, cplen)][0] == code_[code_.size() - 1]) {
              cur_candidates_.push_back(cand);
            }
          }
        } else if (code_.size() == 6) {
          for (const auto &cand : tmp_cand_list) {
            size_t cplen = PinyinUtil::get_first_char_size(cand);
            // clang-format off
            if (PinyinUtil::helpcode_keymap.count(cand.substr(0, cplen))
             && PinyinUtil::helpcode_keymap[cand.substr(0, cplen)][0] == code_[code_.size() - 2]
             && PinyinUtil::helpcode_keymap.count(cand.substr(cplen, cand.size() - cplen))
             && PinyinUtil::helpcode_keymap[cand.substr(cplen, cand.size() - cplen)][0] == code_[code_.size() - 1]) {
              cur_candidates_.push_back(cand);
            }
            // clang-format on
          }
        }
      }
    } else if (code_.size() > 1 && code_.size() % 2) {
      auto tmp_cand_list = dict.generate(code_.substr(0, code_.size() - 1));
      for (const auto &cand : tmp_cand_list) {
        size_t cplen = PinyinUtil::get_first_char_size(cand);
        if (PinyinUtil::helpcode_keymap.count(cand.substr(0, cplen)) && PinyinUtil::helpcode_keymap[cand.substr(0, cplen)][0] == code_[code_.size() - 1]) {
          cur_candidates_.push_back(cand);
        }
      }
      auto tmp_cand_list_02 = dict.generate(code_);
      cur_candidates_.insert(cur_candidates_.end(), tmp_cand_list_02.begin(), tmp_cand_list_02.end());
    } else {
      cur_candidates_ = dict.generate(code_);
    }
    cur_page_ = 0;
    long unsigned int vec_size = cur_candidates_.size() > CANDIDATE_SIZE ? CANDIDATE_SIZE : cur_candidates_.size();
    // 放到实际的候选列表里面去
    for (long unsigned int i = 0; i < CANDIDATE_SIZE; i++) {
      if (i < vec_size) {
        candidates_[i] = std::make_unique<FanimeCandidateWord>(engine_, cur_candidates_[i] + PinyinUtil::compute_helpcodes(cur_candidates_[i]));
      }
    }
    if (vec_size == 0) {
      candidates_[0] = std::make_unique<FanimeCandidateWord>(engine_, "");
      return 1;
    }
    return vec_size;
  }

  FanimeEngine *engine_;
  fcitx::InputContext *ic_;
  fcitx::Text labels_[CANDIDATE_SIZE];
  std::unique_ptr<FanimeCandidateWord> candidates_[CANDIDATE_SIZE];
  std::vector<std::string> cur_candidates_;
  int cur_page_;
  std::string code_;
  int cursor_ = 0;
  int cand_size = CANDIDATE_SIZE;
  static DictionaryUlPb dict;
  static std::unique_ptr<Log> logger;
};

DictionaryUlPb FanimeCandidateList::dict = DictionaryUlPb();
std::unique_ptr<Log> FanimeCandidateList::logger = std::make_unique<Log>("/home/sonnycalcr/.local/share/fcitx5-fanyime/app.log");

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
