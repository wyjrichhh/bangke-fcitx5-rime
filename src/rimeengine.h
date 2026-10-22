/*
 * SPDX-FileCopyrightText: 2017~2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef _FCITX_RIMEENGINE_H_
#define _FCITX_RIMEENGINE_H_

#include "rimeservice.h"
#include <fcitx-config/configuration.h>
#include <fcitx-config/iniparser.h>
#include <fcitx-utils/event.h>
#include <fcitx-utils/eventdispatcher.h>
#include <fcitx-utils/i18n.h>
#include <fcitx-utils/library.h>
#include <fcitx-utils/log.h>
#include <fcitx/action.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addonmanager.h>
#include <fcitx/icontheme.h>
#include <fcitx/inputcontextproperty.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/instance.h>
#include <fcitx/menu.h>
#include <memory>
#include <rime_api.h>
#include <string_view>

namespace fcitx {

class RimeState;

FCITX_CONFIGURATION(
    RimeEngineConfig,
    Option<bool> showPreeditInApplication{this, "PreeditInApplication",
                                          _("Show preedit within application"),
                                          true};
    Option<bool> inlineCandidate{this, "InlineCandidate", _("Show selected candidate in application"), false};
    Option<bool> preeditCursorPositionAtBeginning{
        this, "PreeditCursorPositionAtBeginning",
        _("Fix embedded preedit cursor at the beginning of the preedit"), true};
    Option<bool> commitWhenDeactivate{
        this, "Commit when deactivate",
        _("Commit current text when deactivating"), true};
#ifdef FCITX_RIME_LOAD_PLUGIN
    Option<bool> autoloadPlugins{this, "AutoloadPlugins",
                                 _("Load available plugins automatically"),
                                 false};
    Option<std::vector<std::string>> plugins{this, "Plugins", _("Plugins"),
                                             std::vector<std::string>()};
    Option<std::vector<std::string>> modules{this, "Modules", _("Modules"),
                                             std::vector<std::string>()};
#endif
);

class RimeEngine final : public InputMethodEngineV2 {
public:
    RimeEngine(Instance *instance);
    ~RimeEngine();
    Instance *instance() { return instance_; }
    void activate(const InputMethodEntry &entry,
                  InputContextEvent &event) override;
    void deactivate(const InputMethodEntry &entry,
                    InputContextEvent &event) override;
    void keyEvent(const InputMethodEntry &entry, KeyEvent &keyEvent) override;
    void reloadConfig() override;
    void reset(const InputMethodEntry &entry,
               InputContextEvent &event) override;
    void save() override;
    auto &factory() { return factory_; }

    void updateAction(InputContext *inputContext) {
        imAction_->update(inputContext);
    }

    const Configuration *getConfig() const override { return &config_; }
    void setConfig(const RawConfig &config) override {
        config_.load(config, true);
        safeSaveAsIni(config_, "conf/rime-ai.conf");
        updateConfig();
    }
    void setSubConfig(const std::string &path, const RawConfig &) override;
    void updateConfig();

    std::string subMode(const InputMethodEntry &, InputContext &) override;
    std::string subModeIconImpl(const InputMethodEntry &,
                                InputContext &) override;
    std::string subModeLabelImpl(const InputMethodEntry &,
                                 InputContext &) override;
    const RimeEngineConfig &config() const { return config_; }

    rime_api_t *api() { return api_; }
    const auto &appOptions() const { return appOptions_; }

    void rimeStart(bool fullcheck);

    void deploy();

    RimeState *state(InputContext *ic);

    FCITX_ADDON_DEPENDENCY_LOADER(dbus, instance_->addonManager());

private:
    static void rimeNotificationHandler(void *context_object,
                                        RimeSessionId session_id,
                                        const char *message_type,
                                        const char *message_value);

    void sync();
    void updateSchemaMenu();
    // 通知里用的显示名（= fcitx5 注册名，来自 inputmethod conf 的 Name=）
    std::string displayName() const;
    void notify(RimeSessionId session, const std::string &type,
                const std::string &value);
    void handleReservedProperty(RimeSessionId session,
                                std::string_view messageValue);
    // 插件自有键空间（ai_predict/*）。返回 true 表示这条通知已被消费。
    bool handlePluginProperty(RimeSessionId session,
                              std::string_view messageValue);
    // AI 插件的推理在后台线程跑，跑完只落缓存、不碰 Context；把候选浮出来是
    // 前端的事。pending 置位期间按 mac 侧的时序反复轻推一次重排，直到结果
    // 进缓存或超时（见 rimeengine.cpp）。
    void pumpAIPrediction(RimeSessionId session);
    void scheduleAIPumpTick();
    void nudgeComposition(RimeSessionId session);
    bool isAIPredictionPending(RimeSessionId session) const;
    static void applyReservedPropertyState(RimeState &state,
                                           std::string_view key,
                                           std::string_view value);

    IconTheme theme_;
    Instance *instance_;
    EventDispatcher eventDispatcher_;
    rime_api_t *api_;
    bool firstRun_ = true;
    FactoryFor<RimeState> factory_;

    std::unique_ptr<Action> imAction_;
    SimpleAction deployAction_;
    SimpleAction syncAction_;
    SimpleAction configAction_;

    RimeEngineConfig config_;
    std::unordered_map<std::string, std::unordered_map<std::string, bool>>
        appOptions_;

    FCITX_ADDON_DEPENDENCY_LOADER(notifications, instance_->addonManager());

    std::list<SimpleAction> schemActions_;
    Menu schemaMenu_;
#ifdef FCITX_RIME_LOAD_PLUGIN
    std::unordered_map<std::string, Library> pluginPool_;
#endif
    std::unique_ptr<EventSourceTime> timeEvent_;

    // AI 推理泵。一次只有一个会话在泵上：输入法同一时刻只在一个 IC 里组合。
    // aiPumpRunning_ 与指针分开记：回调返回 false 之后事件源已从循环摘除，
    // 但指针还持有它（不能在回调里自我销毁），靠这个标志判断还能不能起新的。
    std::unique_ptr<EventSourceTime> aiPumpEvent_;
    bool aiPumpRunning_ = false;
    RimeSessionId aiPumpSession_ = 0;
    uint64_t aiPumpInterval_ = 0; // 当前拍的间隔，逐拍翻倍退避
    uint64_t aiPumpDeadline_ = 0;

    RimeService service_{this};
};

class RimeEngineFactory : public AddonFactory {
public:
    AddonInstance *create(AddonManager *manager) override {
        registerDomain("fcitx5-bangke", FCITX_INSTALL_LOCALEDIR);
        return new RimeEngine(manager->instance());
    }
};
} // namespace fcitx

FCITX_DECLARE_LOG_CATEGORY(rime);

#define RIME_DEBUG() FCITX_LOGC(rime, Debug)
#define RIME_ERROR() FCITX_LOGC(rime, Error)

#endif // _FCITX_RIMEENGINE_H_
