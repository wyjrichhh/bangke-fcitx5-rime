"""Isolated test service: reject classicui requests, record accepted layout changes."""
import os
import json
import dbus
import dbus.service
import dbus.mainloop.glib
from gi.repository import GLib

dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
bus = dbus.SessionBus()
names = [dbus.service.BusName(n, bus) for n in ('org.kde.impanel', 'org.fcitx.Fcitx5')]
class Panel(dbus.service.Object):
    @dbus.service.method('org.kde.impanel2', in_signature='', out_signature='s')
    def GetPanelIdentity(self): return 'bangke-panel:test'
    @dbus.service.method('org.kde.impanel2', in_signature='s', out_signature='s')
    def SetLayout(self, orientation):
        with open(os.environ['LAYOUT_LOG'], 'a') as f: f.write(str(orientation)+'\n')
        return orientation
    @dbus.service.method('org.kde.impanel2', in_signature='s', out_signature='s')
    def SetAppearance(self, raw):
        data=json.loads(str(raw))
        assert data['version']==1
        with open(os.environ['LAYOUT_LOG'], 'a') as f: f.write(('vertical' if data['style']['candidate_list_layout']=='stacked' else 'horizontal')+'\n')
        with open(os.environ['LAYOUT_LOG']+'.json','w') as f: json.dump(data,f)
        return 'applied:1'
class Controller(dbus.service.Object):
    @dbus.service.method('org.fcitx.Fcitx.Controller1', in_signature='sv', out_signature='')
    def SetConfig(self, uri, payload):
        if 'classicui' in str(uri): raise dbus.DBusException('classicui must not be called')
    @dbus.service.method('org.fcitx.Fcitx.Controller1', in_signature='', out_signature='')
    def ReloadConfig(self): pass
class Rime(dbus.service.Object):
    @dbus.service.method('org.fcitx.Fcitx.Bangke1', in_signature='', out_signature='')
    def Deploy(self): pass
objects = [Panel(bus, '/org/kde/impanel'), Controller(bus, '/controller'), Rime(bus, '/bangke')]
open(os.environ['MOCK_READY'], 'w').close()
GLib.MainLoop().run()
