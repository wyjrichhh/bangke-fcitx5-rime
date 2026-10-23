// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef BANGKE_RIMEPANEL_H
#define BANGKE_RIMEPANEL_H

#ifdef BANGKE_PRIVATE_PANEL
#include <fcitx-utils/dbus/objectvtable.h>
#include <fcitx-utils/dbus/servicewatcher.h>
#include <fcitx/instance.h>

namespace fcitx {
class RimeEngine;
class RimePanel : public dbus::ObjectVTable<RimePanel> {
public:
    explicit RimePanel(RimeEngine *engine);
    ~RimePanel();
    void activate(InputContext *ic);
    void hide();
    void selectCandidate(int32_t index);
    void page(bool up);
    void refresh();

private:
    struct Callback {
        RimePanel *panel;
        void operator()(InputContext *ic) const { panel->update(ic); }
    };
    InputContext *currentContext();
    bool usesPrivatePanel(InputContext *ic);
    void update(InputContext *ic);
    void clearNativePanel(InputContext *ic);
    FCITX_OBJECT_VTABLE_METHOD(selectCandidate, "SelectCandidate", "i", "");
    FCITX_OBJECT_VTABLE_METHOD(page, "Page", "b", "");
    FCITX_OBJECT_VTABLE_METHOD(refresh, "Refresh", "", "");
    RimeEngine *engine_;
    bool available_ = false;
    std::unique_ptr<dbus::ServiceWatcher> watcher_;
    std::unique_ptr<dbus::ServiceWatcherEntry> service_;
    std::unique_ptr<HandlerTableEntry<EventHandler>> updateHandler_;
};
} // namespace fcitx
#else
namespace fcitx {
class RimeEngine;
class InputContext;
class RimePanel {
public:
    explicit RimePanel(RimeEngine *) {}
    void activate(InputContext *) {}
    void hide() {}
};
} // namespace fcitx
#endif
#endif
