#!/usr/bin/env python3
"""Render the production Cairo functions without starting GTK or claiming D-Bus."""
import ast
from pathlib import Path
import sys
import os
import cairo
import gi
gi.require_version('Pango', '1.0')
gi.require_version('PangoCairo', '1.0')
from gi.repository import Pango, PangoCairo

here = Path(__file__).resolve().parent
out = Path(sys.argv[1]) if len(sys.argv) > 1 else here / 'references/linux'
out.mkdir(parents=True, exist_ok=True)
tree = ast.parse((here / 'panel.py').read_text())
names = {'rounded','highlight_path', 'candidate_geometry','candidate_layout', 'paint_candidates', 'option_bool', 'format_candidate'}
functions = [n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name in names]
ns = {'_cairo': cairo, 'Pango': Pango, 'PangoCairo': PangoCairo}
exec(compile(ast.Module(body=functions, type_ignores=[]), 'panel.py', 'exec'), ns)
measure = cairo.Context(cairo.ImageSurface(cairo.FORMAT_ARGB32, 1, 1))
def layout(text, size):
    result = PangoCairo.create_layout(measure)
    font = Pango.FontDescription('Noto Sans CJK SC')
    font.set_absolute_size(size * Pango.SCALE)
    result.set_font_description(font)
    result.set_text(text, -1)
    return result

# Same lost_temple role colours as the macOS reference; Linux's live panel
# takes its palette from Omarchy instead of forcing this comparison palette.
palette = dict(back=(.188,.188,.188),text=(.965,.953,.910),accent=(.792,.902,.510),
               on_accent=(0,0,0),muted=(.68,.68,.65))
for vertical in (False, True):
    g = ns['candidate_geometry'](['蚌壳','半个','帮客','棒喝','蚌壳'],
         ['1','2','3','4','5'], ['bang ke','ban ge','bang ke','bang he','bang ke'],
         vertical, 1000, layout)
    if os.environ.get('BANGKE_REFERENCE_HIGHLIGHT_RADIUS'):
        g['options']={'hilited_corner_radius':os.environ['BANGKE_REFERENCE_HIGHLIGHT_RADIUS']}
    assert len(g['cells']) == 5
    for name,index in (('first',0),('middle',2),('last',4)):
        cell = g['cells'][index]
        if name == 'first': assert cell[0] == 0 and cell[1] == 0
        if name == 'last':
            edge = cell[1]+cell[3] if vertical else cell[0]+cell[2]
            assert abs(edge-(g['height'] if vertical else g['width'])) < 1.01
        surface = cairo.ImageSurface(cairo.FORMAT_ARGB32, (g['width']+32)*2, (g['height']+32)*2)
        ctx = cairo.Context(surface)
        ctx.scale(2,2)
        ctx.set_source_rgb(.22,.22,.22)
        ctx.paint()
        ctx.translate(16,16)
        ns['paint_candidates'](ctx,g,index,palette,layout)
        # Composite a desktop behind the transparent layer only after rendering.
        ctx.save();ctx.identity_matrix();ctx.set_operator(cairo.OPERATOR_DEST_OVER)
        ctx.set_source_rgb(.22,.22,.22);ctx.paint();ctx.restore()
        file = out / f"linux-{'stacked' if vertical else 'linear'}-{name}.png"
        surface.write_to_png(str(file))
        # Interior highlight reaches every outer edge, but is clipped at corners.
        pixels=surface.get_data(); stride=surface.get_stride()
        def rgb(x,y):
            offset=int((y+16)*2)*stride+int((x+16)*2)*4
            return tuple(pixels[offset+k] for k in (2,1,0))
        x,y,w,h=cell
        assert rgb(x+w/2,y+2)[1] > 200  # selected fill
        assert rgb(0,0) == (56,56,56)  # rounded outer corner shows backdrop
        print(file)
# Long candidates stay within the width budget; all five remain represented.
g=ns['candidate_geometry'](['很长的候选词'*100]*5,['1','2','3','4','5'],['']*5,False,800,layout)
assert g['width'] <= 800 and len(g['cells']) == 5
assert max(x+w for x,y,w,h in g['cells']) <= 800
assert all(layout.get_line_count()>1 for layout in g['layouts'])
assert all(layout.get_ellipsize()==Pango.EllipsizeMode.NONE for layout in g['layouts'])
print('PASS: six corner states and long-candidate width limit')
