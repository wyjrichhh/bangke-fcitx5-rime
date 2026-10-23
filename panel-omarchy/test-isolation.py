#!/usr/bin/env python3
"""Exercise real installed panels and engines in a live Omarchy session.

Requires xwininfo and DISPLAY pointing at the session's XWayland server.
The disposable D-Bus client uses that display so native classicui creates a
real candidate window. It never enables client-side candidate rendering.
"""
import json
import os
from pathlib import Path
import subprocess
import time

import dbus
from dbus.mainloop.glib import DBusGMainLoop
from gi.repository import GLib

DBusGMainLoop(set_as_default=True)
bus = dbus.SessionBus()
controller = dbus.Interface(bus.get_object('org.fcitx.Fcitx5', '/controller'),
                            'org.fcitx.Fcitx.Controller1')
previous = str(controller.CurrentInputMethod())
config = Path.home()/'.config/fcitx5/conf/classicui.conf'
before = config.read_bytes() if config.exists() else None
assert not bus.name_has_owner('org.kde.impanel'), 'Run on Omarchy with its original classic UI'
panel = dbus.Interface(bus.get_object('org.bangke.Panel', '/org/bangke/Panel'), 'org.bangke.Panel1')
im = dbus.Interface(bus.get_object('org.fcitx.Fcitx5', '/org/freedesktop/portal/inputmethod'),
                    'org.fcitx.Fcitx.InputMethod1')
path, _ = im.CreateInputContext([('program', 'org.bangke.PanelIsolationTest'),
                                 ('display', 'x11:'+os.environ['DISPLAY'])])
context = dbus.Interface(bus.get_object('org.fcitx.Fcitx5', path), 'org.fcitx.Fcitx.InputContext1')
commits = []
context.connect_to_signal('CommitString', lambda text: commits.append(str(text)))
context.SetCapability(dbus.UInt64(1 | 2 | 16 | (1 << 23)))


def pump(seconds=.3):
    deadline = time.monotonic()+seconds
    main = GLib.MainContext.default()
    while time.monotonic() < deadline:
        while main.pending(): main.iteration(False)
        time.sleep(.01)


def native_windows():
    tree = subprocess.check_output(['xwininfo', '-root', '-tree'], text=True)
    shown = []
    for line in tree.splitlines():
        if 'Fcitx5 Input Window' not in line: continue
        xid = line.strip().split()[0]
        attrs = subprocess.check_output(['xwininfo', '-id', xid], text=True)
        if 'Map State: IsViewable' in attrs: shown.append(xid)
    return shown


def select_menu(name):
    watcher = dbus.Interface(bus.get_object('org.kde.StatusNotifierWatcher', '/StatusNotifierWatcher'),
                              'org.freedesktop.DBus.Properties')
    items = watcher.Get('org.kde.StatusNotifierWatcher', 'RegisteredStatusNotifierItems')
    assert items, 'Input-method tray menu is missing'
    labels = {str(row[0]): str(row[1]) for row in controller.AvailableInputMethods()}
    for item in items:
        service, path = str(item).split('/', 1)
        properties = dbus.Interface(bus.get_object(service, '/'+path), 'org.freedesktop.DBus.Properties')
        menu_path = str(properties.Get('org.kde.StatusNotifierItem', 'Menu'))
        menu = dbus.Interface(bus.get_object(service, menu_path), 'com.canonical.dbusmenu')
        menu.AboutToShow(0)
        menu.Event(dbus.Int32(0), 'opened', dbus.String('', variant_level=1), dbus.UInt32(0))
        _, root = menu.GetLayout(0, -1, [])
        def find(node):
            ident, properties, children = node
            if str(properties.get('label', '')) == labels[name]: return int(ident)
            for child in children:
                found = find(child)
                if found is not None: return found
            return None
        ident = find(root)
        if ident is not None:
            menu.Event(dbus.Int32(ident), 'clicked', dbus.String('', variant_level=1), dbus.UInt32(0))
            pump(.15)  # Fcitx defers menu actions until keyboard focus returns.
            menu.Event(dbus.Int32(0), 'closed', dbus.String('', variant_level=1), dbus.UInt32(0))
            return
    raise AssertionError('Input-method tray menu does not contain '+labels[name])


def type_text(name, text='jintiantianqihenhao'):
    context.Reset()
    controller.Activate()
    select_menu(name)
    pump()
    assert str(controller.CurrentInputMethod()) == name
    endpoint, interface = ('/bangke', 'org.fcitx.Fcitx.Bangke1') if name == 'rime-ai' else ('/rime', 'org.fcitx.Fcitx.Rime1')
    service = dbus.Interface(bus.get_object('org.fcitx.Fcitx5', endpoint), interface)
    # Cold-start deployment may not have created a usable Rime session yet.
    for _ in range(300):
        schema = str(service.GetCurrentSchema())
        if schema and not schema.startswith('.'): break
        pump(.1)
    else: raise AssertionError(name+': Rime did not finish deployment')
    service.SetAsciiMode(False)
    assert not service.IsAsciiMode(), name+': failed to enable Chinese input'
    context.SetCursorRect(450, 350, 2, 25)
    for key in text:
        context.ProcessKeyEvent(ord(key), 0, 0, False, 0)
        pump(.015)
    pump(4)


def assert_commit():
    context.ProcessKeyEvent(0x20, 0, 0, False, 0)
    pump()
    assert commits and any('\u4e00' <= c <= '\u9fff' for c in commits[-1]), commits


try:
    context.FocusIn()
    # The engine is on-demand; load its private API through the actual menu.
    select_menu('rime-ai')
    pump(.3)
    private = dbus.Interface(bus.get_object('org.fcitx.Fcitx5', '/bangke/panel'), 'org.bangke.Panel1')
    for name in ('rime', 'rime-ai', 'rime'):
        type_text(name)
        state = json.loads(str(panel.GetPanelState()))
        native = native_windows()
        assert state['visible'] == (name == 'rime-ai'), (name, state)
        assert bool(native) == (name == 'rime'), (name, native)
        # The private panel must never select candidates in another engine.
        if name == 'rime':
            n = len(commits)
            private.SelectCandidate(0)
            private.Page(False)
            pump()
            assert len(commits) == n
            assert_commit()
        else:
            private.Page(False)
            pump()
            private.Page(True)
            pump()
            n = len(commits)
            private.SelectCandidate(0)
            pump()
            assert len(commits) > n, 'Private-panel click did not commit'
        print(json.dumps({'im': name, 'bangke_visible': state['visible'],
                          'native_windows': native, 'commit': commits[-1]}, ensure_ascii=False), flush=True)
    # Switch before prediction settles: a late Bangke refresh must not clear
    # the upstream engine's shared input panel on the same input context.
    for cycle in range(8):
        context.Reset()
        controller.Activate()
        select_menu('rime-ai')
        bangke = dbus.Interface(bus.get_object('org.fcitx.Fcitx5', '/bangke'),
                                'org.fcitx.Fcitx.Bangke1')
        bangke.SetAsciiMode(False)
        for key in 'jintiantianqihenhao':
            context.ProcessKeyEvent(ord(key), 0, 0, False, 0)
        select_menu('rime')
        context.Reset()
        upstream = dbus.Interface(bus.get_object('org.fcitx.Fcitx5', '/rime'),
                                  'org.fcitx.Fcitx.Rime1')
        upstream.SetAsciiMode(False)
        for key in 'nihao':
            context.ProcessKeyEvent(ord(key), 0, 0, False, 0)
        pump(1)
        assert not json.loads(str(panel.GetPanelState()))['visible']
        assert native_windows(), ('Late Bangke refresh hid upstream candidates', cycle)
        assert_commit()
        assert commits[-1] == '你好', commits[-1]
    print('PASS: eight rapid switches preserve upstream candidates during pending Bangke inference', flush=True)
    type_text('rime-ai', 'nihao')
    context.FocusOut()
    pump()
    assert not json.loads(str(panel.GetPanelState()))['visible'], 'Private panel survived focus out'
    context.FocusIn()
    type_text('rime-ai', 'nihao')
    subprocess.run(['systemctl', '--user', 'stop', 'bangke-panel.service'], check=True)
    pump(.7)
    assert native_windows(), 'Native fallback did not display the active composition'
    assert_commit()
    type_text('rime-ai', 'nihao')
    subprocess.run(['systemctl', '--user', 'start', 'bangke-panel.service'], check=True)
    for _ in range(50):
        if bus.name_has_owner('org.bangke.Panel'): break
        pump(.1)
    panel = dbus.Interface(bus.get_object('org.bangke.Panel', '/org/bangke/Panel'), 'org.bangke.Panel1')
    pump(1)
    assert json.loads(str(panel.GetPanelState()))['visible'], 'Private panel did not recover'
    assert not native_windows(), 'Native window survived private-panel recovery'
    assert_commit()
    assert not bus.name_has_owner('org.kde.impanel'), 'Bangke acquired global Kimpanel'
    assert (config.read_bytes() if config.exists() else None) == before, 'Original skin configuration changed'
    print('PASS: tray menu switching, native/private/native windows, click/paging, focus-out, crash fallback/recovery and original skin preservation', flush=True)
finally:
    subprocess.run(['systemctl', '--user', 'start', 'bangke-panel.service'], check=True)
    context.Reset()
    if previous: controller.SetCurrentIM(previous)
    context.FocusOut()
    context.DestroyIC()
