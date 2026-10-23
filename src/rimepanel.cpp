// SPDX-License-Identifier: GPL-2.0-or-later
#include "rimepanel.h"
#ifdef BANGKE_PRIVATE_PANEL
#include "dbus_public.h"
#include "rimeengine.h"
#include "rimestate.h"
#include <fcitx-utils/capabilityflags.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/userinterface.h>
#include <fcitx/userinterfacemanager.h>

namespace fcitx {
namespace {
constexpr char serviceName[] = "org.bangke.Panel";
constexpr char panelPath[] = "/org/bangke/Panel";
constexpr char panelInterface[] = "org.bangke.Panel1";
} // namespace
RimePanel::RimePanel(RimeEngine *engine) : engine_(engine) {
    auto *addon = engine_->dbus();
    if (!addon)
        return;
    auto *bus = addon->call<IDBusModule::bus>();
    bus->addObjectVTable("/bangke/panel", panelInterface, *this);
    watcher_ = std::make_unique<dbus::ServiceWatcher>(*bus);
    service_ = watcher_->watchService(
        serviceName, [this](const std::string &, const std::string &,
                            const std::string &owner) {
            available_ = !owner.empty();
            if (auto *ic = currentContext()) {
                if (usesPrivatePanel(ic))
                    clearNativePanel(ic);
                // Rebuild display state without resetting the Rime composition.
                ic->inputPanel().setCustomInputPanelCallback({});
                engine_->state(ic)->updateUI(ic, false);
            }
        });
    updateHandler_ = engine_->instance()->watchEvent(
        EventType::InputContextFlushUI, EventWatcherPhase::PreInputMethod,
        [this](Event &event) {
            auto &updateEvent = static_cast<InputContextFlushUIEvent &>(event);
            if (updateEvent.component() != UserInterfaceComponent::InputPanel)
                return;
            auto *ic = updateEvent.inputContext();
            auto &input = ic->inputPanel();
            const auto *ours =
                input.customInputPanelCallback().target<Callback>();
            if (!usesPrivatePanel(ic)) {
                if (ours && ours->panel == this) {
                    input.setCustomInputPanelCallback({});
                    hide();
                }
                return;
            }
            // Temporary modes may install their own custom panel. Preserve it.
            if (input.customInputPanelCallback() && !ours) {
                hide();
                return;
            }
            input.setCustomInputPanelCallback(Callback{this});
        });
}
RimePanel::~RimePanel() {
    updateHandler_.reset();
    service_.reset();
    engine_->instance()->inputContextManager().foreach(
        [this](InputContext *ic) {
            auto &input = ic->inputPanel();
            const auto *callback =
                input.customInputPanelCallback().target<Callback>();
            if (callback && callback->panel == this)
                input.setCustomInputPanelCallback({});
            return true;
        });
    hide();
}
InputContext *RimePanel::currentContext() {
    auto *ic = engine_->instance()->mostRecentInputContext();
    return ic && ic->hasFocus() &&
                   engine_->instance()->inputMethod(ic) == "rime-ai"
               ? ic
               : nullptr;
}
bool RimePanel::usesPrivatePanel(InputContext *ic) {
    return available_ && ic && ic->hasFocus() &&
           engine_->instance()->inputMethod(ic) == "rime-ai" &&
           !ic->capabilityFlags().test(CapabilityFlag::ClientSideInputPanel);
}
void RimePanel::clearNativePanel(InputContext *ic) {
    // The Rime session owns the composition; clearing only its display lets the
    // previous UI unmap without committing or discarding the user's input.
    ic->inputPanel().reset();
    auto *addon = engine_->instance()->addonManager().addon(
        engine_->instance()->currentUI());
    if (auto *ui = dynamic_cast<UserInterface *>(addon)) {
        ui->update(UserInterfaceComponent::InputPanel, ic);
    }
}
void RimePanel::activate(InputContext *ic) {
    if (usesPrivatePanel(ic))
        clearNativePanel(ic);
}
void RimePanel::hide() {
    if (!available_)
        return;
    auto *bus = engine_->dbus()->call<IDBusModule::bus>();
    auto message = bus->createMethodCall(serviceName, panelPath, panelInterface,
                                         "ShowLookupTable");
    message << false;
    message.send();
    auto preedit = bus->createMethodCall(serviceName, panelPath, panelInterface,
                                         "ShowPreedit");
    preedit << false;
    preedit.send();
    auto auxiliary = bus->createMethodCall(serviceName, panelPath, panelInterface,
                                           "ShowAuxiliaryText");
    auxiliary << false;
    auxiliary.send();
}
void RimePanel::update(InputContext *ic) {
    if (!usesPrivatePanel(ic)) {
        hide();
        return;
    }
    auto *bus = engine_->dbus()->call<IDBusModule::bus>();
    auto call = [&](const char *method) {
        return bus->createMethodCall(serviceName, panelPath, panelInterface,
                                     method);
    };
    auto properties = call("RegisterProperties");
    properties << std::vector<std::string>{
        "/Fcitx/im:蚌壳拼音:fcitx-bangke-im:"};
    properties.send();
    const auto &rect = ic->cursorRect();
    auto spot = call("SetRelativeSpotRectV2");
    spot << int32_t(rect.left()) << int32_t(rect.top()) << int32_t(rect.width())
         << int32_t(rect.height()) << ic->scaleFactor();
    spot.send();
    auto &input = ic->inputPanel();
    auto auxiliaryText = input.auxUp().toString();
    if (!input.auxDown().empty()) {
        if (!auxiliaryText.empty())
            auxiliaryText += '\n';
        auxiliaryText += input.auxDown().toString();
    }
#ifdef BANGKE_OVERLAY_MESSAGE
    // Fcitx 5.1.22 moved temporary IM information out of the normal aux fields.
    // Match Classic UI: overlays are only visible when the input panel is empty.
    if (input.empty())
        auxiliaryText = input.overlayMessage().toString();
#endif
    auto auxiliary = call("UpdateAuxiliaryText");
    auxiliary << auxiliaryText;
    auxiliary.send();
    auto auxiliaryVisible = call("ShowAuxiliaryText");
    auxiliaryVisible << !auxiliaryText.empty();
    auxiliaryVisible.send();
    auto preedit = call("UpdatePreeditText");
    preedit << input.preedit().toString() << std::string()
            << int32_t(input.preedit().cursor()) << true;
    preedit.send();
    auto preeditVisible = call("ShowPreedit");
    preeditVisible << !input.preedit().empty();
    preeditVisible.send();
    std::vector<std::string> labels, texts, attributes;
    int32_t cursor = -1;
    bool prev = false, next = false;
    if (auto list = input.candidateList()) {
        cursor = list->cursorIndex();
        for (int i = 0; i < list->size(); ++i) {
            labels.push_back(list->label(i).toString());
            texts.push_back(list->candidate(i).text().toString());
            attributes.emplace_back();
        }
        if (auto *pages = list->toPageable()) {
            prev = pages->hasPrev();
            next = pages->hasNext();
        }
    }
    auto table = call("UpdateLookupTable");
    table << labels << texts << attributes << prev << next << cursor
          << int32_t(0);
    table.send();
    auto visible = call("ShowLookupTable");
    visible << !texts.empty();
    visible.send();
}
void RimePanel::selectCandidate(int32_t index) {
    auto *ic = currentContext();
    if (!usesPrivatePanel(ic))
        return;
    auto list = ic->inputPanel().candidateList();
    if (list && index >= 0 && index < list->size())
        list->candidate(index).select(ic);
}
void RimePanel::page(bool up) {
    auto *ic = currentContext();
    if (!usesPrivatePanel(ic))
        return;
    auto list = ic->inputPanel().candidateList();
    if (list) {
        if (auto *pages = list->toPageable()) {
            if (up && pages->hasPrev())
                pages->prev();
            else if (!up && pages->hasNext())
                pages->next();
        }
    }
}
void RimePanel::refresh() {
    if (auto *ic = currentContext())
        ic->updateUserInterface(UserInterfaceComponent::InputPanel);
}
} // namespace fcitx

#endif
