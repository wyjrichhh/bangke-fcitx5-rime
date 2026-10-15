// SPDX-License-Identifier: BSD-3-Clause
// Keep the standard Fcitx tray/menu available with a standalone kimpanel UI.
#include <fcitx/addonfactory.h>
#include <fcitx/addoninstance.h>
#include <fcitx/addonmanager.h>
#include <fcitx-module/notificationitem/notificationitem_public.h>

namespace {
class BangkeTray : public fcitx::AddonInstance {
public:
    explicit BangkeTray(fcitx::AddonManager *manager)
        : tray_(manager->addon("notificationitem")) {
        if (tray_) {
            tray_->call<fcitx::INotificationItem::enable>();
        }
    }
    ~BangkeTray() override {
        if (tray_) {
            tray_->call<fcitx::INotificationItem::disable>();
        }
    }
private:
    fcitx::AddonInstance *tray_;
};
class BangkeTrayFactory : public fcitx::AddonFactory {
    fcitx::AddonInstance *create(fcitx::AddonManager *manager) override {
        return new BangkeTray(manager);
    }
};
}
FCITX_ADDON_FACTORY(BangkeTrayFactory)
