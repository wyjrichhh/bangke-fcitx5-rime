#!/usr/bin/env python3
"""Regression checks against production Pango/Cairo code, without a compositor."""
import ast,json,os,tempfile
from pathlib import Path
import cairo,gi
gi.require_version('Pango','1.0');gi.require_version('PangoCairo','1.0')
from gi.repository import Pango,PangoCairo
source=Path(__file__).with_name('panel.py').read_text();tree=ast.parse(source)
names={'rounded','highlight_path','candidate_geometry','candidate_layout','paint_candidates','option_bool','format_candidate','validate_appearance'}
ns={'json':json,'Pango':Pango,'PangoCairo':PangoCairo,'_cairo':cairo}
exec(compile(ast.Module(body=[n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name in names],type_ignores=[]),'panel.py','exec'),ns)
context=cairo.Context(cairo.ImageSurface(cairo.FORMAT_ARGB32,1,1));options={}
def layout(text,size):
 l=PangoCairo.create_layout(context);d=Pango.FontDescription(options.get('font_face','Noto Sans CJK SC'));d.set_absolute_size(size*Pango.SCALE);l.set_font_description(d)
 if options.get('text_orientation')=='vertical':text='\n'.join(text)
 l.set_text(text,-1);return l
palette=dict(back=[.1,.1,.15,1],text=[1,1,1,1],accent=[.8,.2,.2,.7],on_accent=[1,1,1,1],muted=[.7,.7,.7,1],border=[.2,.8,.2,1])
def draw(settings,radius=9):
 global options
 options=settings
 g=ns['candidate_geometry'](['蚌壳','更长的候选'],['1','2'],['bang ke',''],True,1000,layout,float(settings.get('font_point',16))*4/3,settings)
 surface=cairo.ImageSurface(cairo.FORMAT_ARGB32,g['width'],g['height']);ns['paint_candidates'](cairo.Context(surface),g,0,palette,layout,radius)
 return g,bytes(surface.get_data())
base={'candidate_format':'[label]. [candidate] [comment]','font_point':'16','line_spacing':'5','candidate_list_layout':'stacked','text_orientation':'horizontal'}
g,pixels=draw(base)
for key,value in [('alpha','.4'),('shadow_size','10'),('mutual_exclusive','true')]:
 changed=dict(base,**{key:value});assert draw(changed)[1]!=pixels,key
assert draw(dict(base,hilited_corner_radius='10'))[1]==pixels
assert draw(dict(base,border_width='3',border_height='2'))[1]!=pixels
assert draw(base,0)[1]!=pixels
assert draw(dict(base,font_point='28'))[0]['height']>g['height']
assert draw(dict(base,line_spacing='20'))[0]['height']>g['height']
assert draw(dict(base,candidate_format='[candidate]'))[0]['width']<g['width']
assert draw(dict(base,text_orientation='vertical'))[0]['height']>g['height']
assert ns['format_candidate']('[candidate] / [label]: [comment]','1','词','ci')==[('词','text'),(' / ','text'),('1','label'),(': ','text'),('ci','comment')]
payload={'version':1,'style':base,'light':palette,'dark':palette};assert ns['validate_appearance'](json.dumps(payload))==payload
for broken in [dict(payload,version=99),dict(payload,style=dict(base,font_point='0')),dict(payload,light=dict(palette,text=[2,0,0,1]))]:
 try:ns['validate_appearance'](json.dumps(broken))
 except (ValueError,KeyError,TypeError):pass
 else:raise AssertionError('invalid appearance accepted')
print('PASS: actual pixels change for alpha, corners, shadow, transparent layering and border; font, spacing, template and text direction affect geometry; malformed payload rejected')

# Repaint on the same retained GTK backing surface: alpha must not accumulate.
options=dict(base,alpha='.35')
g=ns['candidate_geometry'](['蚌壳','更长的候选'],['1','2'],['bang ke',''],True,1000,layout,16*4/3,options)
surface=cairo.ImageSurface(cairo.FORMAT_ARGB32,g['width'],g['height']);ctx=cairo.Context(surface)
ns['paint_candidates'](ctx,g,0,palette,layout,9);expected=bytes(surface.get_data())
for _ in range(20):ns['paint_candidates'](ctx,g,0,palette,layout,9)
assert bytes(surface.get_data())==expected,'transparency accumulated between frames'
# An opaque previous frame must not prevent lowering background opacity.
g['options']=dict(options,alpha='1');ns['paint_candidates'](ctx,g,0,palette,layout,9)
g['options']=options;ns['paint_candidates'](ctx,g,0,palette,layout,9)
assert bytes(surface.get_data())==expected,'old opaque pixels survived opacity change'
# First/last retain panel corners for both axes even at nonzero highlight radius.
for vertical in (False,True):
 g=ns['candidate_geometry'](['甲','乙','丙'],['1','2','3'],['','',''],vertical,1000,layout,24,base)
 for selected in (0,1,2):
  x,y,w,h=g['cells'][selected]
  ctx=cairo.Context(cairo.ImageSurface(cairo.FORMAT_ARGB32,g['width'],g['height']))
  ns['highlight_path'](ctx,(x,y,w,h),g['width'],g['height'],12,3)
  # Three px from an exterior corner must follow radius 12, not radius 3.
  for px,py,outer in ((x+2,y+2,selected==0), (x+w-2,y+h-2,selected==2)):
   assert ctx.in_fill(px,py)!=outer,(vertical,selected,px,py)
print('PASS: opacity stable across retained frames; opaque-to-transparent repaint; first/middle/last corner identity at nonzero highlight radius in both axes')

# Full Unicode/ASCII paragraphs, including the final character, must be retained.
long_text='长文本输入必须完整显示，不能用省略号代替。'*100+' 最后一个字终'
for vertical in (False,True):
 options=dict(base,candidate_list_layout='stacked' if vertical else 'linear')
 g=ns['candidate_geometry']([long_text,'x'*2000],['1','2'],['完整注释'*100,''],vertical,400,layout,20,options)
 assert g['height']>1000 and g['width']<=400
 for paragraph,word in zip(g['layouts'],(long_text,'x'*2000)):
  assert word in paragraph.get_text() and paragraph.get_ellipsize()==Pango.EllipsizeMode.NONE
  assert paragraph.get_line_count()>1
  last=paragraph.get_line_readonly(paragraph.get_line_count()-1)
  assert last.start_index+last.length==len(paragraph.get_text().encode('utf-8'))
# AI candidates may wrap onto further rows, but cannot alter the first layout's glyph positions.
options=dict(base,candidate_list_layout='linear')
a=ns['candidate_geometry'](['固定的首个候选','词'],['1','2'],['',''],False,400,layout,20,options)
b=ns['candidate_geometry'](['固定的首个候选',long_text],['1','2'],['',''],False,400,layout,20,options)
first_a,first_b=a['layouts'][0],b['layouts'][0]
assert first_a.get_pixel_size()==first_b.get_pixel_size() and a['text_tops'][0]==b['text_tops'][0]
for offset in range(len(first_a.get_text().encode('utf-8'))):
 pa,pb=first_a.index_to_pos(offset),first_b.index_to_pos(offset)
 assert (pa.x,pa.y,pa.width,pa.height)==(pb.x,pb.y,pb.width,pb.height)
# Compare actual rasterized first-candidate glyphs, independently of panel dimensions.
def glyphs(paragraph):
 surface=cairo.ImageSurface(cairo.FORMAT_ARGB32,400,100);ctx=cairo.Context(surface)
 ctx.set_source_rgb(1,1,1);PangoCairo.show_layout(ctx,paragraph);return bytes(surface.get_data())
assert glyphs(first_a)==glyphs(first_b)
print('PASS: full long CJK/ASCII candidates and comments wrap through their final byte; AI update preserves first-candidate glyph positions and exact pixels')

# Fade the complete fill stack exactly once: selection and shadow must not
# remain opaque when background opacity is zero. Text and border stay sharp.
options=dict(base,shadow_size='7',border_width='0',border_height='0')
g=ns['candidate_geometry'](['甲','乙'],['1','2'],['',''],True,400,layout,24,options)
for exclusive in ('false','true'):
 g['options']=dict(options,mutual_exclusive=exclusive)
 values=[]
 for alpha in ('1','0.5','0'):
  g['options']['alpha']=alpha
  image=cairo.ImageSurface(cairo.FORMAT_ARGB32,g['width'],g['height'])
  ns['paint_candidates'](cairo.Context(image),g,0,palette,layout,9)
  image.flush();x=5;y=int(g['cells'][0][3]/2)
  pixel=bytes(image.get_data())[y*image.get_stride()+x*4:y*image.get_stride()+x*4+4]
  values.append(pixel)
 assert values[0][3]>0 and values[2]==b'\0\0\0\0',values
 for full,half in zip(values[0],values[1]):assert abs(half-full*.5)<=1,(values,exclusive)
print('PASS: selected background and shadow scale together at alpha 1/.5/0; exclusive highlight supported; no residual background at zero opacity')
