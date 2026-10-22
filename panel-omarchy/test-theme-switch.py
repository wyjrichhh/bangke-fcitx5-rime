import os,subprocess,tempfile,pathlib,time,json,dbus
SOURCE = pathlib.Path(__file__).resolve().parents[1]
root=pathlib.Path(tempfile.mkdtemp(prefix='bk-live-settings-'))
env=dict(os.environ,HOME=str(root),XDG_DATA_HOME=str(root/'data'),XDG_CONFIG_HOME=str(root/'config'),DISPLAY=':97',GDK_BACKEND='x11',QT_QPA_PLATFORM='offscreen',XDG_STATE_HOME=str(root/'state'),BANGKE_PANEL_BACKEND='kimpanel',BK_OMARCHY_CATALOG_DIR=str(SOURCE / 'panel-omarchy/themes'),PATH=str(SOURCE/'config-ui/build')+':'+os.environ['PATH'],MOCK_READY=str(root/'ready'),LAYOUT_LOG=str(root/'layout.log'))
rime=root/'data/fcitx5/bangke';rime.mkdir(parents=True)
(rime/'bangke.yaml').write_text((SOURCE/'data/bangke.yaml').read_text())
mock=pathlib.Path(str(SOURCE / 'config-ui/tests/custompanel-mock.py')).read_text().replace("('org.bangke.Panel', 'org.fcitx.Fcitx5')","('org.fcitx.Fcitx5',)").replace("Panel(bus, '/org/bangke/Panel'), ","")
(root/'controller.py').write_text(mock)
current=root/'state/omarchy/current';current.mkdir(parents=True)
catalog=pathlib.Path(str(SOURCE / 'panel-omarchy/themes'))
(current/'theme').symlink_to(catalog/'tokyo-night',target_is_directory=True)
procs=[]
try:
 procs.append(subprocess.Popen(['Xvfb',':97','-screen','0','1600x1000x24'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL));time.sleep(.4)
 for name,cmd in [('controller',['python',str(root/'controller.py')]),('panel',['python',str(SOURCE / 'panel-omarchy/panel.py')])]:
  procs.append(subprocess.Popen(cmd,env=env,stdout=(root/(name+'.log')).open('w'),stderr=subprocess.STDOUT))
 bus=dbus.SessionBus()
 for i in range(100):
  if bus.name_has_owner('org.bangke.Panel') and (root/'ready').exists():break
  time.sleep(.1)
 assert bus.name_has_owner('org.bangke.Panel'),(root/'panel.log').read_text()
 panel=dbus.Interface(bus.get_object('org.bangke.Panel','/org/bangke/Panel'),'org.bangke.Panel1')
 def signal(name,args,signature):
  panel.get_dbus_method(name)(*args)

 signal('RegisterProperties',[['/Fcitx/im:蚌壳拼音:input-keyboard:Bangke:0:0']],'as')
 def wait_palette(expected):
  for _ in range(100):
   got=json.loads(str(panel.GetAppearance()))
   if got['light']==expected['light'] and got['dark']==expected['dark']:return got
   time.sleep(.1)
  raise AssertionError((expected,got,(root/'panel.log').read_text()))
 def dump():
  return json.loads(subprocess.check_output(['bangke-pinyin-config','--dump-panel-appearance'],env=env,text=True))
 wait_palette(dump())
 # Exercise every shipped theme using actual Gio events and actual Qt resolver.
 for theme in sorted(path for path in catalog.iterdir() if path.is_dir()):
  next_link=current/'next-theme';next_link.symlink_to(theme,target_is_directory=True)
  next_link.replace(current/'theme')
  expected=dump();got=wait_palette(expected)
  assert got['style']['color_scheme']=='native'
  print('PASS automatic switch',theme.name,flush=True)
 # Rapid switches must leave the newest result, never a stale worker's output.
 for name in ('nord','flexoki-light','gruvbox','tokyo-night'):
  next_link=current/'next-theme';next_link.symlink_to(catalog/name,target_is_directory=True);next_link.replace(current/'theme')
 wait_palette(dump());print('PASS rapid switch newest theme wins',flush=True)
 # A manually fixed scheme must retain colours when the OS changes.
 (rime/'bangke.custom.yaml').write_text('patch:\n  style/color_scheme: omarchy_nord\n  style/color_scheme_dark: omarchy_nord\n')
 next_link=current/'next-theme';next_link.symlink_to(catalog/'white',target_is_directory=True);next_link.replace(current/'theme')
 fixed=wait_palette(dump())
 next_link=current/'next-theme';next_link.symlink_to(catalog/'gruvbox',target_is_directory=True);next_link.replace(current/'theme')
 wait_palette(fixed);print('PASS manual palette stays fixed',flush=True)
 assert procs[-1].poll() is None,(root/'panel.log').read_text()
 print('LOGDIR',root)
finally:
 for proc in reversed(procs):
  proc.terminate()
  try:proc.wait(timeout=3)
  except subprocess.TimeoutExpired:proc.kill();proc.wait()
