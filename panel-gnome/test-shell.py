#!/usr/bin/env python3
"""Real GNOME integration fixture; only use on test-shell.sh's private bus."""
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
from gi.repository import GLib, Gtk, GdkPixbuf

assert os.environ.get('BANGKE_TEST_REPO'), 'Use test-shell.sh for isolated settings and bus'
dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
bus = dbus.SessionBus()
backend_name = dbus.service.BusName('org.fcitx.Fcitx5', bus)
screenshot_name = dbus.service.BusName('org.gnome.Screenshot', bus)
results = Path(sys.argv[1])

class Input(dbus.service.Object):
    @dbus.service.signal('org.kde.kimpanel.inputmethod', signature='as')
    def RegisterProperties(self, entries): pass
    @dbus.service.signal('org.kde.kimpanel.inputmethod', signature='b')
    def ShowLookupTable(self, visible): pass
    @dbus.service.signal('org.bangke.Candidate1', signature='sb')
    def Composition(self, token, active): pass
    @dbus.service.signal('org.bangke.Candidate1', signature='asasas')
    def Candidates(self, combined, texts, comments): pass

fixture = Input(bus, '/rime')
def interface(name='org.kde.impanel', path='/org/kde/impanel', iface='org.kde.impanel2'):
    return dbus.Interface(bus.get_object(name, path), iface)
panel = interface()

def pump(seconds=.25):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        while GLib.MainContext.default().pending(): GLib.MainContext.default().iteration(False)
        time.sleep(.01)

def state(): return json.loads(str(panel.GetPanelState()))
def wait_visible():
    for _ in range(40):
        pump(.1)
        if state()['visible']: return state()
    raise AssertionError(f'Panel did not become visible: {state()}')

def overview(visible):
    props = interface('org.gnome.Shell', '/org/gnome/Shell', 'org.freedesktop.DBus.Properties')
    props.Set('org.gnome.Shell', 'OverviewActive', dbus.Boolean(visible))
    pump(.4)

def capture(name):
    pump(.4)
    shot = interface('org.gnome.Shell.Screenshot', '/org/gnome/Shell/Screenshot', 'org.gnome.Shell.Screenshot')
    success, path = shot.Screenshot(False, False, str(results / name))
    assert success, path

# Patterned real Wayland client beneath the panel makes backdrop blur visible.
window = Gtk.Window(title='Bangke GNOME integration fixture')
window.set_default_size(1100, 650)
canvas = Gtk.DrawingArea()
def draw(_widget, cr):
    for y in range(0, 900, 24):
        for x in range(0, 1500, 24):
            cr.set_source_rgb(*((.12, .24, .38) if (x // 24 + y // 24) % 2 else (.75, .6, .3)))
            cr.rectangle(x, y, 24, 24); cr.fill()
canvas.connect('draw', draw)
window.add(canvas); window.show_all(); pump(.8); overview(False)
colors = {'back': [.98, .94, .92, 1], 'text': [.17, .15, .13, 1],
          'accent': [1, .49, .24, 1], 'on_accent': [1, 1, 1, 1], 'muted': [.62, .55, .5, 1], 'border': [.4, .3, .2, 1]}
appearance = {'version': 1, 'style': {'alpha': '0.6', 'corner_radius': '18', 'font_point': '14',
    'font_face': 'sans-serif', 'text_orientation': 'horizontal', 'candidate_list_layout': 'stacked',
    'translucency': 'false', 'border_width': '1', 'border_height': '1', 'candidate_format': '[label]. [candidate] [comment]',
    'memorize_size': 'true', 'show_paging': 'true'}, 'light': colors, 'dark': colors}
assert str(panel.SetAppearance(json.dumps(appearance))) == 'applied:1'
pump()
fixture.RegisterProperties(['鍙/Fcitx/im:蚌壳拼音:icon:蚌壳拼音'])
fixture.Composition('fixture:1', True)
texts = ['你好，世界', '独立配色与圆角', '长候选可以换行']
comments = ['AI', '', '注释']
combined = [text + (' ' + comment if comment else '') for text, comment in zip(texts, comments)]
fixture.Candidates(combined, texts, comments)
pump()
panel.SetSpotRect(300, 220, 2, 24)
panel.SetLookupTable(['1', '2', '3'], combined, ['', '', ''], False, True, 0, 0)
fixture.ShowLookupTable(True)
first = wait_visible()
assert first['candidates'] == 3 and first['im'] == '蚌壳拼音', first
capture('glass-off.png')
appearance['style']['translucency'] = 'true'
assert str(panel.SetAppearance(json.dumps(appearance))) == 'applied:1'
pump()
assert state()['blur'] is True
capture('glass-on.png')
print('BLUR STATE', json.dumps(state()), flush=True)
def edge_energy(path):
    pix = GdkPixbuf.Pixbuf.new_from_file(str(path))
    data, stride, channels = pix.get_pixels(), pix.get_rowstride(), pix.get_n_channels()
    g = first['geometry']
    left, top = int(g['x']) + 25, int(g['y'] + g['height']) - 36
    width = int(g['width']) - 50
    return sum(abs(data[y * stride + x * channels + c] - data[y * stride + (x + 1) * channels + c])
               for y in range(top, top + 10) for x in range(left, left + width - 1) for c in range(3))
off_energy, on_energy = edge_energy(results / 'glass-off.png'), edge_energy(results / 'glass-on.png')
print(f'Background edge energy: off={off_energy}, on={on_energy}', flush=True)
assert off_energy > 0 and on_energy < off_energy * .75, 'Blur must measurably smooth the patterned backdrop' 
# Exercise all newly available layout controls and persisted round trips.
for layout in ['vertical', 'horizontal']:
    appearance['style'].update(text_orientation=layout, corner_radius='30',
        hilited_corner_radius='12', mutual_exclusive='true', line_spacing='6', spacing='7')
    assert str(panel.SetAppearance(json.dumps(appearance))) == 'applied:1'
    pump()
    assert json.loads(str(panel.GetAppearance()))['style'] == appearance['style']
    assert state()['visible']
    capture('layout-' + layout + '.png')
# Pixel evidence: both the panel radius and exclusive transparency alter rendering.
def pixel_changes(a, b):
    pa = GdkPixbuf.Pixbuf.new_from_file(str(results / a)).get_pixels()
    pb = GdkPixbuf.Pixbuf.new_from_file(str(results / b)).get_pixels()
    return sum(x != y for x, y in zip(pa, pb))
for radius in ['0', '30']:
    appearance['style']['corner_radius'] = radius
    panel.SetAppearance(json.dumps(appearance)); pump(); capture('radius-' + radius + '.png')
assert pixel_changes('radius-0.png', 'radius-30.png') > 100
for exclusive in ['false', 'true']:
    appearance['style']['mutual_exclusive'] = exclusive
    panel.SetAppearance(json.dumps(appearance)); pump(); capture('exclusive-' + exclusive + '.png')
assert pixel_changes('exclusive-false.png', 'exclusive-true.png') > 100
# First, middle and last selection share the panel edge, with square internal edges.
panel.SetLookupTable(['1', '2', '3'], combined, ['', '', ''], False, False, 0, 0)
pump()
for selected in range(3):
    panel.SetLookupTable(['1', '2', '3'], combined, ['', '', ''], False, False, selected, 0)
    pump(); capture('highlight-' + str(selected) + '.png')
locked_height = state()['geometry']['height']
# AI refresh must preserve the composition origin even if a new rect arrives.
panel.SetSpotRect(450, 350, 2, 24)
long_text = '这是用于验证长文本完整换行与滚动的人工智能候选。' * 80
fixture.Candidates([long_text], [long_text], [''])
pump()
panel.SetLookupTable(['1'], [long_text], [''], False, False, 0, 0)
pump()
assert state()['origin'] == first['origin'], (first, state())
assert state()['geometry']['height'] == locked_height, (locked_height, state())
panel.SetLookupTable(['1'], ['短候选'], [''], False, False, 0, 0)
pump()
assert state()['geometry']['height'] == locked_height, (locked_height, state())
panel.SetLookupTable(['1'], [long_text], [''], False, False, 0, 0)
pump()
capture('long-candidate.png')
# A remembered wide panel must not distribute spare width between short labels.
panel.SetLookupTable(['1', '2', '3'], ['一', '二', '三'], ['', '', ''], False, False, 0, 0)
pump()
appearance['style']['candidate_list_layout'] = 'linear'
panel.SetAppearance(json.dumps(appearance)); pump()
short_state = state()
assert short_state['cells'][0]['width'] < 100, short_state
assert short_state['cells'][1]['width'] < 100, short_state
assert short_state['cells'][1]['label'][0] - short_state['cells'][0]['label'][0] < 100, short_state
capture('compact-spacing.png')
print('PASS: short candidates stay adjacent in a wide remembered AI panel', flush=True)
fixture.Composition('fixture:1', False)
fixture.ShowLookupTable(False)
pump()
assert not state()['visible']
# Re-enable exercises Shell's actual teardown and D-Bus registration path.
extensions = interface('org.gnome.Shell.Extensions', '/org/gnome/Shell/Extensions', 'org.gnome.Shell.Extensions')
assert extensions.DisableExtension('bangke-panel@bangke.org')
pump()
assert not bus.name_has_owner('org.kde.impanel')
assert extensions.EnableExtension('bangke-panel@bangke.org')
pump(.6)
panel = interface()
assert str(panel.GetPanelIdentity()).startswith('bangke-panel:gnome-')
assert json.loads(str(panel.GetAppearance()))['style'] == appearance['style']
errors = extensions.GetExtensionErrors('bangke-panel@bangke.org')
assert not errors, errors
window.destroy()
(results / 'state.json').write_text(json.dumps(first, ensure_ascii=False, indent=2))
print('PASS: GNOME 46 real Wayland rendering, blur toggle, AI origin, long text, hide, disable/re-enable and screenshots')
