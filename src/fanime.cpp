/*
 * SPDX-FileCopyrightText: 2021~2021 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */
#include "fanime.h"
#include "dict.h"
#include "log.h"
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

namespace {

static const int CANDIDATE_SIZE = 8; // 候选框默认的 size，不许超过 9，不许小于 4

bool checkAlpha(std::string s) { return s.size() == 1 && isalpha(s[0]); }

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
    inputContext->commitString(text().toString());
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
    setPageable(this);
    setCursorMovable(this);
    cand_size = generate();               // generate actually
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
        candidates_[i] = std::make_unique<FanimeCandidateWord>(engine_, cur_candidates_[i + cur_page_ * CANDIDATE_SIZE]);
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
        candidates_[i] = std::make_unique<FanimeCandidateWord>(engine_, cur_candidates_[i + cur_page_ * CANDIDATE_SIZE]);
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
    // logger->info("fanycode => " + code_);
    // std::vector<std::string> candi_vec = dict.generate(code_);
    cur_candidates_ = dict.generate(code_);
    cur_page_ = 0;
    long unsigned int vec_size = cur_candidates_.size() > CANDIDATE_SIZE ? CANDIDATE_SIZE : cur_candidates_.size();
    for (long unsigned int i = 0; i < CANDIDATE_SIZE; i++) {
      if (i < vec_size) {
        candidates_[i] = std::make_unique<FanimeCandidateWord>(engine_, cur_candidates_[i]);
      }
    }
    if (vec_size == 0) {
      candidates_[0] = std::make_unique<FanimeCandidateWord>(engine_, "😍");
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
    // 向前翻页 -> 上箭头
    if (event.key().checkKeyList(engine_->instance()->globalConfig().defaultPrevPage()) || event.key().check(FcitxKey_minus)) {
      if (auto *pageable = candidateList->toPageable(); pageable && pageable->hasPrev()) {
        event.accept();
        pageable->prev();
        ic_->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
      }
      return event.filterAndAccept();
    }

    // 向后翻页
    if (event.key().checkKeyList(engine_->instance()->globalConfig().defaultNextPage()) || event.key().check(FcitxKey_equal)) {
      if (auto *pageable = candidateList->toPageable(); pageable && pageable->hasNext()) {
        pageable->next();
        ic_->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
      }
      return event.filterAndAccept();
    }
  }

  if (buffer_.empty()) {                                              // current text buffer is empty
    if (!checkAlpha(event.key().keySymToString(event.key().sym()))) { // current text is empty and not digit
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
      buffer_.backspace();
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
    if (!checkAlpha(event.key().keySymToString(event.key().sym()))) { // current text buffer is not empty, and current key pressed is not digit

      return event.filterAndAccept();
    }
  }

  // 1. current text buffer is empty and current key pressed is digit
  // 2. current text buffer is not empty and current key pressed is digit
  buffer_.type(event.key().sym()); // update buffer_, so when the fucking event itself is updated?
  updateUI();
  return event.filterAndAccept();
}

void FanimeState::setCode(std::string code) {
  // TODO: 重写
  buffer_.clear();
  buffer_.type(code);
  updateUI();
}

void FanimeState::updateUI() {
  auto &inputPanel = ic_->inputPanel(); // also need to track the initialization of ic_
  inputPanel.reset();
  if (buffer_.size() > 0) { // 已经输入了拼音字符
    inputPanel.setCandidateList(std::make_unique<FanimeCandidateList>(engine_, ic_, buffer_.userInput()));
  }
  if (ic_->capabilityFlags().test(fcitx::CapabilityFlag::Preedit)) {
    fcitx::Text preedit(buffer_.userInput(), fcitx::TextFormatFlag::HighLight);
    inputPanel.setClientPreedit(preedit);
  } else {
    fcitx::Text preedit(buffer_.userInput());
    inputPanel.setPreedit(preedit);
  }
  ic_->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
  ic_->updatePreedit();
}

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
  state->reset();
}

FCITX_ADDON_FACTORY(FanimeEngineFactory);
