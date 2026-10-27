#!/usr/bin/env python3
"""Check composition sizing and burst scheduling without a desktop session."""
import ast
from pathlib import Path
from types import SimpleNamespace
source = Path(__file__).with_name('panel.py').read_text()
tree = ast.parse(source)
nodes = [n for n in tree.body if isinstance(n, ast.ClassDef) and n.name == 'CompositionSize']
ns = {}
exec(compile(ast.Module(body=nodes, type_ignores=[]), 'panel.py', 'exec'), ns)
size = ns['CompositionSize']()
def geometry(width):
    return dict(width=width, cells=[(0, 0, width, 40)])
for vertical in (False, True):
    size.reset()
    for incoming, expected in ((200, 200), (350, 350), (180, 350), (400, 400)):
        result = size.apply(geometry(incoming), ('window', vertical), 1000, vertical)
        assert result['width'] == expected
        assert result['cells'][-1][2] == expected
    assert size.apply(geometry(180), ('other-window', vertical), 1000, vertical)['width'] == 180
    size.reset()
    assert size.apply(geometry(120), ('window', vertical), 1000, vertical)['width'] == 120
panel = next(n for n in tree.body if isinstance(n, ast.ClassDef) and n.name == 'Panel')
methods = [n for n in panel.body if isinstance(n, ast.FunctionDef) and n.name in ('queue_refresh', '_flush_refresh')]
callbacks = []
ns = {'GLib': SimpleNamespace(timeout_add=lambda delay, cb: callbacks.append(cb) or len(callbacks), SOURCE_REMOVE=False)}
exec(compile(ast.Module(body=methods, type_ignores=[]), 'panel.py', 'exec'), ns)
frames = []
fake = SimpleNamespace(_refresh_source=0, refresh=lambda: frames.append('refresh'))
fake._flush_refresh = lambda: ns['_flush_refresh'](fake)
for _ in range(30):
    ns['queue_refresh'](fake)
assert len(callbacks) == 1 and not frames
callbacks[0]()
assert frames == ['refresh'] and fake._refresh_source == 0
ns['queue_refresh'](fake)
assert len(callbacks) == 2
refresh = next(n for n in panel.body if isinstance(n, ast.FunctionDef) and n.name == 'refresh')
hides = [n for n in ast.walk(refresh) if isinstance(n, ast.Call) and isinstance(n.func, ast.Attribute) and n.func.attr == 'hide']
assert len(hides) == 1
assert hides[0] in list(ast.walk(refresh.body[0]))  # Only the empty-candidate path unmaps.
print('PASS: width growth/reset, both layouts, burst coalescing, no update-time unmapping')

# Width growth at a screen edge must constrain content, not push its origin.
nodes=[n for n in tree.body if isinstance(n,ast.ClassDef) and n.name=='CompositionOrigin']
ns={};exec(compile(ast.Module(body=nodes,type_ignores=[]),'panel.py','exec'),ns)
origin=ns['CompositionOrigin']();area=SimpleNamespace(x=0,y=26,width=1920,height=1054)
key=('window', 'composition-1')
assert origin.apply(key,(1500,970),300,44,area,2)==(1500,970)
assert origin.width_limit(key,area,1000)==416
# Simulate a refitted target and a client cursor changed by an AI preedit update.
for target in ((1000,960),(1530,965),(1510,970)):
 assert origin.apply(key,target,416,44,area,2)==(1500,970)
# A fallback must not replace an already accurate origin.
assert origin.apply(key,(24,24),300,44,area,1)==(1500,970)
assert origin.apply(('window','composition-2'),(200,400),300,44,area,2)==(200,400)
assert origin.apply(('moved-window','composition-2'),(240,440),300,44,area,2)==(240,440)
origin.reset();assert origin.width_limit(key,area,1000)==1000
print('PASS: AI refitting/caret changes preserve origin; screen-edge width capped; new composition/window repositions')

origin=ns['CompositionOrigin']();ns['SPOT_GAP']=5
origin.apply(key,(1500,400),300,44,area,2,(1500,500,2,24))
assert origin.height_limit(key,area,1000)==95
assert origin.apply(key,(1000,0),416,5000,area,2)==(1500,400)
print('PASS: taller complete text uses a bounded viewport above the caret without moving the origin')

# Window layout changes must refresh an idle visible composition without a keypress.
watch = next(n for n in panel.body if isinstance(n, ast.FunctionDef) and n.name == '_watch_placement')
screen = SimpleNamespace(x=0, y=0, width=1920, height=1080)
snapshot = ['browser', (967, 560, 941, 508), 'chromium']
queries, updates = [], []
cursor = [1225, 834, 0, 29]
resets = []
def focus(*, force=False):
    queries.append(force)
    return tuple(snapshot)
ns = {'S': {'im': 'ours', 'composition': 'composition'}, 'OUR_IM': 'ours',
      'GLib': SimpleNamespace(SOURCE_REMOVE=False, SOURCE_CONTINUE=True),
      'focused_window': focus, 'plugin_caret': lambda: tuple(cursor)}
exec(compile(ast.Module(body=[watch], type_ignores=[]), 'panel.py', 'exec'), ns)
fake = SimpleNamespace(get_visible=lambda: True, _screen=lambda: screen,
                       _placement_watch_source=1, _placement_cursor=None, _resize_cursor=None,
                       _composition_origin=SimpleNamespace(reset=lambda: resets.append('reset')),
                       _placement_key=('browser', snapshot[1], (0, 0, 1920, 1080), 'style', 'composition'),
                       queue_refresh=lambda: updates.append('refresh'))
assert ns['_watch_placement'](fake) is True and not updates and queries == [True]
# Candidate text and caret updates in the same layout must not unpin the panel.
assert ns['_watch_placement'](fake) is True and not updates
snapshot[1] = (0, 0, 1920, 1080)
cursor[:] = [258, 274, 0, 29]
assert ns['_watch_placement'](fake) is True and updates == ['refresh']
fake._placement_key = ('browser', snapshot[1], (0, 0, 1920, 1080), 'style', 'composition')
updates.clear()
# The compositor moved the old local rectangle before Chromium commits its layout.
cursor[:] = [258, 274, 0, 29]
assert ns['_watch_placement'](fake) is True and not resets and not updates
# A later client cursor commit must correct the pinned point even with unchanged geometry.
cursor[:] = [748, 516, 0, 30]
assert ns['_watch_placement'](fake) is True and resets == ['reset'] and updates == ['refresh']
updates.clear()
cursor[0] += 20
assert ns['_watch_placement'](fake) is True and not updates and resets == ['reset']
screen.width, screen.height = 1512, 982
assert ns['_watch_placement'](fake) is True and updates == ['refresh']
fake.get_visible = lambda: False
updates.clear()
assert ns['_watch_placement'](fake) is False and fake._placement_watch_source == 0 and not updates
print('PASS: idle composition follows fullscreen/output geometry changes; unchanged layout stays pinned; hidden panel stops watching')
