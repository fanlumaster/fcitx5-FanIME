#ifndef _FCITX5_FANIME_FANIME_H_
#define _FCITX5_FANIME_FANIME_H_

#include <fcitx-utils/inputbuffer.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextproperty.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/inputpanel.h>
#include <fcitx/instance.h>
#include <boost/circular_buffer.hpp>
#include <iconv.h>
#include "dict.h"
#include "log.h"

class FanimeEngine;

class FanimeState : public fcitx::InputContextProperty {
public:
  FanimeState(FanimeEngine *engine, fcitx::InputContext *ic) : engine_(engine), ic_(ic) {}

  void keyEvent(fcitx::KeyEvent &keyEvent);
  void setCode(std::string code);
  void updateUI();
  // 清除 buffer，更新 UI
  void reset();
  fcitx::InputContext &getIc();
  fcitx::InputBuffer &getBuffer();

private:
  FanimeEngine *engine_;
  fcitx::InputContext *ic_;
  fcitx::InputBuffer buffer_{{fcitx::InputBufferOption::AsciiOnly, fcitx::InputBufferOption::FixedCursor}};
  bool use_fullhelpcode_ = false;
  static std::unique_ptr<::Log> logger;
};

class FanimeEngine : public fcitx::InputMethodEngineV2 {
public:
  static DictionaryUlPb fan_dict;
  static boost::circular_buffer<std::pair<std::string, std::vector<DictionaryUlPb::WordItem>>> cached_buffer;
  static std::string pure_pinyin;
  static std::string seg_pinyin;
  static size_t supposed_han_cnt;
  static bool can_create_word;

  FanimeEngine(fcitx::Instance *instance);

  void activate(const fcitx::InputMethodEntry &entry, fcitx::InputContextEvent &event) override;
  void deactivate(const fcitx::InputMethodEntry &entry, fcitx::InputContextEvent &event) override;
  void keyEvent(const fcitx::InputMethodEntry &entry, fcitx::KeyEvent &keyEvent) override;

  void reset(const fcitx::InputMethodEntry &, fcitx::InputContextEvent &event) override;

  auto factory() const { return &factory_; }
  auto conv() const { return conv_; }
  auto instance() const { return instance_; }

  FCITX_ADDON_DEPENDENCY_LOADER(quickphrase, instance_->addonManager());
  FCITX_ADDON_DEPENDENCY_LOADER(punctuation, instance_->addonManager());

  bool get_use_fullhelpcode() { return use_fullhelpcode_; }
  void set_use_fullhelpcode(bool flag) { use_fullhelpcode_ = flag; }
  std::string get_raw_pinyin() { return raw_pinyin; }
  void set_raw_pinyin(std::string pinyin) { raw_pinyin = pinyin; }
  int get_cand_page_idx() { return cand_page_idx_; }
  void set_cand_page_idx(int page_idx) { cand_page_idx_ = page_idx; }

private:
  FCITX_ADDON_DEPENDENCY_LOADER(chttrans, instance_->addonManager());
  FCITX_ADDON_DEPENDENCY_LOADER(fullwidth, instance_->addonManager());

  fcitx::Instance *instance_;
  fcitx::FactoryFor<FanimeState> factory_;
  iconv_t conv_;
  // TODO: 后续把这几个辅助码相关的变量抽象成一个简单类
  bool use_fullhelpcode_ = false;
  std::string raw_pinyin;
  int cand_page_idx_;
};

class FanimeEngineFactory : public fcitx::AddonFactory {
  fcitx::AddonInstance *create(fcitx::AddonManager *manager) override {
    FCITX_UNUSED(manager);
    return new FanimeEngine(manager->instance());
  }
};

#endif // _FCITX5_FANIME_FANIME_H_
