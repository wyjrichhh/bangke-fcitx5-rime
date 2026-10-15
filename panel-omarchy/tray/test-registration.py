import dbus, dbus.service, dbus.mainloop.glib
from gi.repository import GLib
import subprocess, tempfile, os

dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
bus=dbus.SessionBus(); loop=GLib.MainLoop(); state={'ok':False}
class Watcher(dbus.service.Object):
    @dbus.service.method('org.kde.StatusNotifierWatcher', in_signature='s', out_signature='')
    def RegisterStatusNotifierItem(self, service):
        print('REGISTER',service,flush=True)
        GLib.timeout_add(200, check, str(service))
    @dbus.service.method('org.freedesktop.DBus.Properties', in_signature='ss', out_signature='v')
    def Get(self, interface, prop):
        return dbus.Boolean(True) if prop=='IsStatusNotifierHostRegistered' else dbus.Int32(0)
    @dbus.service.signal('org.kde.StatusNotifierWatcher', signature='')
    def StatusNotifierHostRegistered(self): pass
name=dbus.service.BusName('org.kde.StatusNotifierWatcher', bus)
watcher=Watcher(bus, '/StatusNotifierWatcher')
def check(service):
    try:
        obj=bus.get_object(service,'/StatusNotifierItem')
        props=dbus.Interface(obj,'org.freedesktop.DBus.Properties').GetAll('org.kde.StatusNotifierItem')
        assert str(props['Status'])=='Active',props
        menu=str(props['Menu'])
        layout=dbus.Interface(bus.get_object(service,menu),'com.canonical.dbusmenu').GetLayout(0,-1,[])
        assert len(layout[1][2])>0,layout
        print('PASS active tray, standard menu entries:',len(layout[1][2]),flush=True)
        state['ok']=True
    except Exception as e: print('FAIL',repr(e),flush=True)
    loop.quit(); return False
with tempfile.TemporaryDirectory() as home:
    env=dict(os.environ,HOME=home,XDG_CONFIG_HOME=home+'/.config',XDG_DATA_HOME=home+'/.local/share')
    with open('/tmp/bangke-tray-fcitx.log','w') as log:
        proc=subprocess.Popen(['fcitx5','-D','--keep','--disable','classicui,wayland,xcb','--enable','notificationitem,bangke-tray','-u','kimpanel'],env=env,stdout=log,stderr=log)
        GLib.timeout_add_seconds(12,lambda: (loop.quit(),False)[1]); loop.run()
        proc.terminate(); proc.wait(timeout=5)
    output=open('/tmp/bangke-tray-fcitx.log').read()
    assert 'Loaded addon bangke-tray' in output,output
    assert 'Loaded addon classicui' not in output,output
    assert state['ok'],output
