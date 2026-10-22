#!/usr/bin/env python3
"""Check real GTK/Wayland typing through the compositor, not a mock context.

Run inside Omarchy with GDK_BACKEND=wayland and GTK_IM_MODULE=fcitx.
This covers the GTK Fcitx bridge in a native Wayland window. Synthetic
shortcuts bypass compositor IM grabs, so they cannot test text-input-v3.
Creates a disposable entry, sends keys only to its window, restores the IM,
and optionally saves screenshots under BANGKE_TEST_CAPTURE_DIR.
"""
import json
import os
from pathlib import Path
import subprocess
import time

import dbus
from dbus.mainloop.glib import DBusGMainLoop
import gi
gi.require_version('Gtk', '3.0')
from gi.repository import Gtk, GLib

DBusGMainLoop(set_as_default=True)
bus = dbus.SessionBus()
controller = dbus.Interface(bus.get_object('org.fcitx.Fcitx5', '/controller'),
                            'org.fcitx.Fcitx.Controller1')
previous = str(controller.CurrentInputMethod())
previous_cursor = json.loads(subprocess.check_output(['hyprctl','-j','cursorpos'],text=True))
panel = dbus.Interface(bus.get_object('org.bangke.Panel', '/org/bangke/Panel'), 'org.bangke.Panel1')
window = Gtk.Window(title='Bangke UI isolation probe')
window.set_default_size(640, 120)
entry = Gtk.Entry()
entry.set_margin_start(24)
entry.set_margin_end(24)
window.add(entry)
window.show_all()
entry.grab_focus()
window.present()


def pump(seconds=.3):
    deadline = time.monotonic()+seconds
    main = GLib.MainContext.default()
    while time.monotonic() < deadline:
        while main.pending(): main.iteration(False)
        time.sleep(.01)


def key(value):
    expression = "hl.dsp.send_shortcut({mods='',key="+json.dumps(value)+",window="+json.dumps('address:'+own[0]['address'])+"})"
    result = subprocess.run(['hyprctl', 'dispatch', expression], text=True, capture_output=True)
    assert result.returncode == 0, (expression, result.stdout, result.stderr)
    pump(.04)


try:
    pump(1)
    clients = json.loads(subprocess.check_output(['hyprctl', '-j', 'clients'], text=True))
    own = [c for c in clients if c.get('title') == 'Bangke UI isolation probe']
    assert len(own) == 1 and not own[0]['xwayland'], own
    x=own[0]['at'][0]+own[0]['size'][0]//2
    y=own[0]['at'][1]+own[0]['size'][1]//2
    subprocess.run(['hyprctl','dispatch',f'hl.dsp.cursor.move({{x={x},y={y}}})'],check=True,stdout=subprocess.DEVNULL)
    focus = "hl.dsp.focus({window="+json.dumps('address:'+own[0]['address'])+"})"
    subprocess.run(['hyprctl', 'dispatch', focus], check=True, stdout=subprocess.DEVNULL)
    pump(1)
    entry.grab_focus()
    pump(.5)
    active=json.loads(subprocess.check_output(['hyprctl','-j','activewindow'],text=True))
    assert window.is_active() and entry.has_focus(), (window.is_active(),entry.has_focus(),entry.is_focus(),active)
    for name in ('rime', 'rime-ai', 'rime'):
        entry.set_text('')
        entry.grab_focus()
        controller.Activate()
        controller.SetCurrentIM(name)
        pump()
        endpoint, interface = ('/bangke', 'org.fcitx.Fcitx.Bangke1') if name == 'rime-ai' else ('/rime', 'org.fcitx.Fcitx.Rime1')
        dbus.Interface(bus.get_object('org.fcitx.Fcitx5', endpoint), interface).SetAsciiMode(False)
        for character in 'nihao': key(character)
        pump(.8)
        state = json.loads(str(panel.GetPanelState()))
        assert state['visible'] == (name == 'rime-ai'), (name, state)
        if os.environ.get('BANGKE_TEST_CAPTURE_DIR'):
            directory = Path(os.environ['BANGKE_TEST_CAPTURE_DIR'])
            directory.mkdir(parents=True, exist_ok=True)
            subprocess.run(['grim', str(directory/('wayland-'+name+'.png'))], check=True)
        key('space')
        pump()
        assert any('\u4e00' <= c <= '\u9fff' for c in entry.get_text()), (name, entry.get_text())
        print(json.dumps({'im': name, 'wayland': True, 'bangke_visible': state['visible'],
                          'commit': entry.get_text()}, ensure_ascii=False), flush=True)
    print('PASS: real GTK/Wayland input, private panel isolation and Chinese commits', flush=True)
finally:
    if previous: controller.SetCurrentIM(previous)
    window.destroy()
    pump()
    x,y=previous_cursor['x'],previous_cursor['y']
    subprocess.run(['hyprctl','dispatch',f'hl.dsp.cursor.move({{x={x},y={y}}})'],check=True,stdout=subprocess.DEVNULL)
