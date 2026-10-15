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
