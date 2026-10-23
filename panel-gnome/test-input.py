#!/usr/bin/env python3
"""Installed-package input regression on test-shell.sh's private GNOME session."""
import json
import os
from pathlib import Path
import subprocess
import sys
import time
import dbus
import dbus.service
import dbus.mainloop.glib
import gi
gi.require_version('Gtk', '3.0')
from gi.repository import GLib, Gtk

assert os.environ.get('BANGKE_TEST_REPO'), 'Run through test-shell.sh'
results = Path(sys.argv[1])
dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
bus = dbus.SessionBus()
tray_items = []
class TrayWatcher(dbus.service.Object):
    @dbus.service.method('org.kde.StatusNotifierWatcher', in_signature='s', out_signature='')
    def RegisterStatusNotifierItem(self, service):
        tray_items.append(str(service))
    @dbus.service.method('org.freedesktop.DBus.Properties', in_signature='ss', out_signature='v')
    def Get(self, interface, prop):
        return dbus.Boolean(True) if prop == 'IsStatusNotifierHostRegistered' else dbus.Int32(0)
watcher_name = dbus.service.BusName('org.kde.StatusNotifierWatcher', bus)
watcher = TrayWatcher(bus, '/StatusNotifierWatcher')
subprocess.run(['python3', '/usr/lib/bangke-pinyin/user-data.py', 'install'], check=True)
# A fresh profile explicitly uses keyboard-us as the initial input method.
profile = Path(os.environ['XDG_CONFIG_HOME']) / 'fcitx5/profile'
profile.write_text('[Groups/0]\nName=Default\nDefault Layout=us\nDefaultIM=rime-ai\n\n[Groups/0/Items/0]\nName=keyboard-us\nLayout=\n\n[Groups/0/Items/1]\nName=rime-ai\nLayout=\n\n[GroupOrder]\n0=Default\n')
log = open(results / 'fcitx.log', 'w')
fcitx = subprocess.Popen(['fcitx5'], env={**os.environ, 'BK_QT_PLATFORM': 'offscreen'}, stdout=log, stderr=log)

def pump(seconds=.3):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        while GLib.MainContext.default().pending(): GLib.MainContext.default().iteration(False)
        time.sleep(.01)

keyboard = None
try:
    for _ in range(200):
        if bus.name_has_owner('org.fcitx.Fcitx5'): break
        assert fcitx.poll() is None, 'Fcitx failed to start; see fcitx.log'
        pump(.1)
    controller = dbus.Interface(bus.get_object('org.fcitx.Fcitx5', '/controller'), 'org.fcitx.Fcitx.Controller1')
    window = Gtk.Window(title='Bangke real Fcitx5 input test')
    window.set_default_size(850, 500)
    entry = Gtk.Entry()
    box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=12, margin=24)
    box.pack_start(Gtk.Label(label='Real GTK / Fcitx5 / Rime / GNOME panel integration'), False, False, 0)
    box.pack_start(entry, False, False, 0)
    window.add(box); window.show_all(); window.present(); entry.grab_focus()
    pump(2)
    props = dbus.Interface(bus.get_object('org.gnome.Shell', '/org/gnome/Shell'), 'org.freedesktop.DBus.Properties')
    props.Set('org.gnome.Shell', 'OverviewActive', dbus.Boolean(False))
    pump()
    remote = dbus.Interface(bus.get_object('org.gnome.Mutter.RemoteDesktop', '/org/gnome/Mutter/RemoteDesktop'), 'org.gnome.Mutter.RemoteDesktop')
    path = remote.CreateSession()
    keyboard = dbus.Interface(bus.get_object('org.gnome.Mutter.RemoteDesktop', path), 'org.gnome.Mutter.RemoteDesktop.Session')
    keyboard.Start()
    keyboard.NotifyKeyboardKeysym(dbus.UInt32(0xffe1), True)
    keyboard.NotifyKeyboardKeysym(dbus.UInt32(0xffe1), False)
    pump(1)
    controller.SetCurrentIM('rime-ai'); controller.Activate()
    rime = dbus.Interface(bus.get_object('org.fcitx.Fcitx5', '/bangke'), 'org.fcitx.Fcitx.Bangke1')
    # Fresh seed installation compiles dictionaries asynchronously. Query the
    # real Rime session instead of racing startup with a fixed short delay.
    for _ in range(600):
        schema = str(rime.GetCurrentSchema())
        if schema and schema != '.default': break
        pump(.2)
    assert schema and schema != '.default', f'Rime deployment did not finish: {schema}'
    rime.SetSchema('rime_ice'); rime.SetAsciiMode(False)
    pump(.5)
    def key(sym):
        keyboard.NotifyKeyboardKeysym(dbus.UInt32(sym), True)
        keyboard.NotifyKeyboardKeysym(dbus.UInt32(sym), False)
        pump(.12)
    print('BEFORE INPUT', str(controller.CurrentInputMethod()), int(controller.State()), str(controller.DebugInfo()), flush=True)
    for char in 'nihao': key(ord(char))
    pump(2)
    panel = dbus.Interface(bus.get_object('org.kde.impanel', '/org/kde/impanel'), 'org.kde.impanel2')
    state = json.loads(str(panel.GetPanelState()))
    print('AFTER INPUT', repr(entry.get_text()), str(controller.CurrentInputMethod()), int(controller.State()), str(controller.DebugInfo()), flush=True)
    assert state['visible'] and state['candidates'] > 0 and state['im'] == '蚌壳拼音', state
    screenshot_name = dbus.service.BusName('org.gnome.Screenshot', bus)
    shot = dbus.Interface(bus.get_object('org.gnome.Shell.Screenshot', '/org/gnome/Shell/Screenshot'), 'org.gnome.Shell.Screenshot')
    success, _ = shot.Screenshot(False, False, str(results / 'real-input.png'))
    assert success
    (results / 'real-input-state.json').write_text(json.dumps(state, ensure_ascii=False, indent=2))
    key(0x20)
    pump(1)
    text = entry.get_text()
    assert any('\u4e00' <= char <= '\u9fff' for char in text), repr(text)
    assert not json.loads(str(panel.GetPanelState()))['visible']
    print(f'PASS: installed package, real GTK/Wayland keystrokes, Rime candidates and Chinese commit: {text}', flush=True)
    assert tray_items, 'Kimpanel must retain the Fcitx tray'
    item = bus.get_object(tray_items[-1], '/StatusNotifierItem')
    tray = dbus.Interface(item, 'org.freedesktop.DBus.Properties').GetAll('org.kde.StatusNotifierItem')
    assert str(tray['Status']) == 'Active', tray
    menu = dbus.Interface(bus.get_object(tray_items[-1], str(tray['Menu'])), 'com.canonical.dbusmenu')
    def settings_id(node):
        label = str(node[1].get('label', ''))
        if '蚌壳' in label and '设置' in label: return int(node[0])
        for child in node[2]:
            found = settings_id(child)
            if found is not None: return found
    action_id = settings_id(menu.GetLayout(0, -1, [])[1])
    assert action_id is not None, menu.GetLayout(0, -1, [])
    menu.Event(action_id, 'clicked', dbus.Int32(0), dbus.UInt32(0))
    settings_pids = []
    for _ in range(50):
        for proc in Path('/proc').iterdir():
            if not proc.name.isdigit(): continue
            try:
                command = (proc / 'cmdline').read_bytes().split(b'\0')[0]
                environment = (proc / 'environ').read_bytes().split(b'\0')
                if command.endswith(b'bangke-pinyin-config') and (b'DBUS_SESSION_BUS_ADDRESS=' + os.environ['DBUS_SESSION_BUS_ADDRESS'].encode()) in environment:
                    settings_pids.append(int(proc.name))
            except (OSError, PermissionError): pass
        if settings_pids: break
        pump(.1)
    assert settings_pids, 'Tray Settings action must launch the real configuration application'
    for pid in set(settings_pids): os.kill(pid, 15)
    print('PASS: Kimpanel tray active, Bangke Settings menu present and launches real settings application', flush=True)
    qt_test = Path(os.environ['BANGKE_TEST_REPO']) / 'config-ui/build/custompanel_save_test'
    if qt_test.exists():
        subprocess.run([str(qt_test)], env={**os.environ, 'QT_QPA_PLATFORM': 'offscreen', 'BANGKE_PANEL_BACKEND': 'gnome'}, check=True)
        assert json.loads(str(panel.GetAppearance()))['style']['candidate_list_layout'] == 'linear'
        print('PASS: real Qt Settings saves to GNOME panel over D-Bus', flush=True)
    extensions = dbus.Interface(bus.get_object('org.gnome.Shell.Extensions', '/org/gnome/Shell/Extensions'), 'org.gnome.Shell.Extensions')
    assert extensions.DisableExtension('bangke-panel@bangke.org')
    pump(.6)
    assert str(controller.CurrentUI()) == 'classicui', str(controller.CurrentUI())
    assert fcitx.poll() is None
    print('PASS: disabling GNOME panel returns Fcitx5 to Classic UI', flush=True)
    assert extensions.EnableExtension('bangke-panel@bangke.org')
    pump(.6)
    assert str(controller.CurrentUI()) == 'kimpanel', str(controller.CurrentUI())
    print('PASS: re-enabling GNOME panel restores Kimpanel', flush=True)
    window.destroy()
finally:
    if keyboard: keyboard.Stop()
    fcitx.terminate()
    try: fcitx.wait(timeout=10)
    except subprocess.TimeoutExpired: fcitx.kill(); fcitx.wait()
    log.close()
