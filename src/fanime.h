/*
 * SPDX-FileCopyrightText: 2021~2021 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */
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
#include <iconv.h>
#include "log.h"

class FanimeEngine;

class FanimeState : public fcitx::InputContextProperty {
public:
  FanimeState(FanimeEngine *engine, fcitx::InputContext *ic) : engine_(engine), ic_(ic) {}

  void keyEvent(fcitx::KeyEvent &keyEvent);
  void setCode(std::string code);
  void updateUI();
  // 清除 buffer，更新 UI
  void reset() {
    buffer_.clear();
    updateUI();
  }

private:
  FanimeEngine *engine_;
  fcitx::InputContext *ic_;
  fcitx::InputBuffer buffer_{{fcitx::InputBufferOption::AsciiOnly, fcitx::InputBufferOption::FixedCursor}};
  static std::unique_ptr<::Log> logger;
};

class FanimeEngine : public fcitx::InputMethodEngineV2 {
public:
  FanimeEngine(fcitx::Instance *instance);

  void activate(const fcitx::InputMethodEntry &entry, fcitx::InputContextEvent &event) override;
  void keyEvent(const fcitx::InputMethodEntry &entry, fcitx::KeyEvent &keyEvent) override;

  void reset(const fcitx::InputMethodEntry &, fcitx::InputContextEvent &event) override;

  auto factory() const { return &factory_; }
  auto conv() const { return conv_; }
  auto instance() const { return instance_; }

  FCITX_ADDON_DEPENDENCY_LOADER(quickphrase, instance_->addonManager());
  FCITX_ADDON_DEPENDENCY_LOADER(punctuation, instance_->addonManager());

private:
  FCITX_ADDON_DEPENDENCY_LOADER(chttrans, instance_->addonManager());
  FCITX_ADDON_DEPENDENCY_LOADER(fullwidth, instance_->addonManager());

  fcitx::Instance *instance_;
  fcitx::FactoryFor<FanimeState> factory_;
  iconv_t conv_;
};

class FanimeEngineFactory : public fcitx::AddonFactory {
  fcitx::AddonInstance *create(fcitx::AddonManager *manager) override {
    FCITX_UNUSED(manager);
    return new FanimeEngine(manager->instance());
  }
};

#endif // _FCITX5_FANIME_FANIME_H_
