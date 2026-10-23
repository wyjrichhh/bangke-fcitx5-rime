"""Exercise installed addons through the running Fcitx5 desktop process.

Requires python-dbus and python-gobject. Creates a disposable client context,
requests client-side candidates, and restores the previously selected IM.
"""
import json
import sys
import time

import dbus
from dbus.mainloop.glib import DBusGMainLoop
from gi.repository import GLib


DBusGMainLoop(set_as_default=True)
bus = dbus.SessionBus()
controller = dbus.Interface(bus.get_object("org.fcitx.Fcitx5", "/controller"),
                            "org.fcitx.Fcitx.Controller1")
previous = str(controller.CurrentInputMethod())
im = dbus.Interface(bus.get_object("org.fcitx.Fcitx5",
                                   "/org/freedesktop/portal/inputmethod"),
                    "org.fcitx.Fcitx.InputMethod1")
path, _ = im.CreateInputContext([("program", "org.bangke.CoexistenceTest")])
context = dbus.Interface(bus.get_object("org.fcitx.Fcitx5", path),
                         "org.fcitx.Fcitx.InputContext1")
menus = []
commits = []


def update(*args):
    candidates = [{"label": str(label), "text": str(text)} for label, text in args[4]]
    if candidates:
        menus.append(candidates)


def pump(seconds):
    deadline = time.monotonic() + seconds
    main = GLib.MainContext.default()
    while time.monotonic() < deadline:
        while main.pending():
            main.iteration(False)
        time.sleep(0.01)


context.connect_to_signal("UpdateClientSideUI", update)
context.connect_to_signal("CommitString", lambda text: commits.append(str(text)))
# ClientSideUI | Preedit | FormattedPreedit | GetIMInfoOnFocus | ClientSideInputPanel.
context.SetCapability(dbus.UInt64(1 | 2 | 16 | (1 << 23) | (1 << 39)))
try:
    context.FocusIn()
    for name in (sys.argv[1:] or ("rime", "rime-ai", "rime")):
        menus.clear()
        commits.clear()
        controller.Activate()
        controller.SetCurrentIM(name)
        pump(0.3)
        assert str(controller.CurrentInputMethod()) == name, f"input method did not switch to {name}"
        service_path, service_interface = (("/bangke", "org.fcitx.Fcitx.Bangke1")
                                            if name == "rime-ai" else
                                            ("/rime", "org.fcitx.Fcitx.Rime1"))
        service = dbus.Interface(bus.get_object("org.fcitx.Fcitx5", service_path),
                                 service_interface)
        service.SetAsciiMode(False)
        context.Reset()
        for key in "jintiantianqihenhao":
            context.ProcessKeyEvent(ord(key), 0, 0, False, 0)
            pump(0.02)
        pump(4)
        assert menus, f"{name}: no client-side candidates"
        context.ProcessKeyEvent(0x20, 0, 0, False, 0)
        pump(0.5)
        assert commits and any("\u4e00" <= ch <= "\u9fff" for ch in "".join(commits)), \
            f"{name}: Chinese text was not committed"
        print(json.dumps({"im": name, "schema": str(service.GetCurrentSchema()),
                          "candidates": menus[-1], "commits": commits}, ensure_ascii=False),
              flush=True)
    print("Live Fcitx5 switch, candidate and commit coexistence: OK", flush=True)
finally:
    context.Reset()
    if previous:
        controller.SetCurrentIM(previous)
    context.FocusOut()
    context.DestroyIC()
