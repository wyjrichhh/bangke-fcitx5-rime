// SPDX-License-Identifier: BSD-3-Clause
// Keep the standard Fcitx tray/menu available with a standalone kimpanel UI.
#include <fcitx-utils/handlertable.h>
#include <fcitx-utils/event.h>
#include <fcitx/instance.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addoninstance.h>
#include <fcitx/addonmanager.h>
#include <fcitx-module/notificationitem/notificationitem_public.h>

namespace {
class BangkeTray : public fcitx::AddonInstance {
public:
    explicit BangkeTray(fcitx::AddonManager *manager)
        : tray_(manager->addon("notificationitem")), instance_(manager->instance()) {
        if (tray_) {
            tray_->call<fcitx::INotificationItem::enable>();
            // Classic UI may suspend after addon construction and disable SNI.
            // Its API is a boolean, not reference-counted. Recover after startup
            // and whenever a registered tray is withdrawn by a UI switch.
            watch_ = tray_->call<fcitx::INotificationItem::watch>([this](bool registered) {
                if (!registered) scheduleEnable();
            });
            initial_ = instance_->eventLoop().addTimeEvent(CLOCK_MONOTONIC, 0, 0,
                [this](fcitx::EventSourceTime *source, uint64_t) {
                    source->setEnabled(false);
                    scheduleEnable();
                    return true;
                });
            initial_->setNextInterval(1000000);
        }
    }
    ~BangkeTray() override {
        initial_.reset();
        pending_.reset();
        watch_.reset();
        if (tray_) {
            tray_->call<fcitx::INotificationItem::disable>();
        }
    }
private:
    void scheduleEnable() {
        pending_ = instance_->eventLoop().addDeferEvent([this](fcitx::EventSource *source) {
            source->setEnabled(false);
            tray_->call<fcitx::INotificationItem::enable>();
            return true;
        });
    }
    fcitx::AddonInstance *tray_;
    fcitx::Instance *instance_;
    std::unique_ptr<fcitx::HandlerTableEntry<fcitx::NotificationItemCallback>> watch_;
    std::unique_ptr<fcitx::EventSourceTime> initial_;
    std::unique_ptr<fcitx::EventSource> pending_;
};
class BangkeTrayFactory : public fcitx::AddonFactory {
    fcitx::AddonInstance *create(fcitx::AddonManager *manager) override {
        return new BangkeTray(manager);
    }
};
}
FCITX_ADDON_FACTORY(BangkeTrayFactory)
