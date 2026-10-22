import os,subprocess,tempfile,pathlib,time,json,dbus
SOURCE = pathlib.Path(__file__).resolve().parents[1]
root=pathlib.Path(tempfile.mkdtemp(prefix='bk-live-settings-'))
env=dict(os.environ,PATH=str(SOURCE/'config-ui/build')+':'+os.environ['PATH'],XDG_STATE_HOME=str(root/'state'),BANGKE_PANEL_BACKEND='kimpanel',HOME=str(root),XDG_DATA_HOME=str(root/'data'),XDG_CONFIG_HOME=str(root/'config'),DISPLAY=':97',GDK_BACKEND='x11',QT_QPA_PLATFORM='offscreen',BK_OMARCHY_THEME_DIR=str(SOURCE / 'config-ui/tests/fixtures/omarchy-tokyo-night'),MOCK_READY=str(root/'ready'),LAYOUT_LOG=str(root/'layout.log'))
rime=root/'data/fcitx5/bangke';rime.mkdir(parents=True)
(rime/'bangke.yaml').write_text((SOURCE/'data/bangke.yaml').read_text())
mock=pathlib.Path(str(SOURCE / 'config-ui/tests/custompanel-mock.py')).read_text().replace("('org.bangke.Panel', 'org.fcitx.Fcitx5')","('org.fcitx.Fcitx5',)").replace("Panel(bus, '/org/bangke/Panel'), ","")
(root/'controller.py').write_text(mock)
procs=[]
try:
 procs.append(subprocess.Popen(['Xvfb',':97','-screen','0','1600x1000x24'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL));time.sleep(.4)
 for name,cmd in [('controller',['python',str(root/'controller.py')]),('panel',['python',str(SOURCE/'panel-omarchy/panel.py')])]:
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
 # Xvfb cannot supply Hyprland backdrop effects; exercise every other setting.
 subprocess.run([str(SOURCE / 'config-ui/build/custompanel_save_test'), '--without-blur'],env=env,check=True)
 got=json.loads(str(panel.GetAppearance()));assert got['style']['candidate_list_layout']=='linear'
 for mode in ('linear','stacked'):
  got['style'].update(candidate_list_layout=mode,font_point='24',alpha='.65',hilited_corner_radius='10',line_spacing='14',spacing='12',show_paging='true',inline_preedit='false',candidate_format='[candidate] / [label]: [comment]')
  assert str(panel.SetAppearance(json.dumps(got)))=='applied:1'
  signal('UpdatePreeditText',['bang ke','',0,True],'ssib');signal('ShowPreedit',[True],'b');signal('ShowLookupTable',[True],'b')
  details=dbus.lowlevel.SignalMessage('/bangke','org.bangke.Candidate1','Candidates')
  details.append(['蚌壳 bang ke','半个','新的很长候选'],['蚌壳','半个','新的很长候选'],['bang ke','',''],signature='asasas');bus.send_message(details);bus.flush()
  assert panel.UpdateLookupTableFast(['1','2','3'],['蚌壳 bang ke','半个','新的很长候选'],['','',''],True,True,0,0)
  time.sleep(.25)
  assert json.loads(str(panel.GetAppearance()))['style']==got['style']
  state=json.loads(str(panel.GetPanelState()));assert state['im']=='蚌壳拼音' and state['visible'] and state['style']['font_point']=='24' and state['preedit_height']>0 and state['paging_height']==24 and state['comments']==1,state
  # Real GTK frame sequence: AI-only update changes width and reported caret.
  def composition(token,active):
   msg=dbus.lowlevel.SignalMessage('/bangke','org.bangke.Candidate1','Composition')
   msg.append(token,active,signature='sb');bus.send_message(msg);bus.flush()
  composition('session:'+mode,True)
  panel.SetSpotRect(900,700,2,24)
  panel.UpdateLookupTableFast(['1','2','3'],['蚌壳','半个','帮客'],['','',''],False,False,0,0);time.sleep(.15)
  before=json.loads(str(panel.GetPanelState()))
  panel.SetSpotRect(1100,720,2,24)
  panel.UpdateLookupTableFast(['1','2','3'],['蚌壳','AI新加入的很长很长候选'*15,'棒喝'],['','',''],False,False,0,0);time.sleep(.15)
  after=json.loads(str(panel.GetPanelState()))
  assert before['origin']==after['origin'] and before['first_baseline']==after['first_baseline'] and before['buffer_size']==after['buffer_size'],(before,after)
  assert after['content_size'][1]>before['content_size'][1],(before,after)
  panel.UpdateLookupTableFast(['1','2','3'],['蚌壳','完整长文本必须能滚动查看直到最后一个字'*300+'终','最后候选'],['','',''],False,False,0,0);time.sleep(.2)
  huge=json.loads(str(panel.GetPanelState()));assert huge['content_size'][1]>huge['buffer_size'][1] and huge['scroll'][1]>huge['scroll'][2],huge
  panel.UpdateLookupTableCursor(2);time.sleep(.2)
  assert json.loads(str(panel.GetPanelState()))['scroll'][0]>0
  assert huge['buffer_size']==before['buffer_size'],(before,huge)
  composition('session:new:'+mode,True)
  panel.SetSpotRect(200,300,2,24)
  panel.UpdateLookupTableFast(['1','2'],['新的','输入'],['',''],False,False,0,0);time.sleep(.15)
  assert json.loads(str(panel.GetPanelState()))['origin']!=before['origin']
  print('PASS: GTK AI update keeps origin, glyph baseline and actual surface size; full long text scrolls and selection reveals last candidate; new composition relocates',mode)
 # Malformed updates must leave persisted and live state intact.
 try:panel.SetAppearance(json.dumps(dict(got,version=99)))
 except dbus.DBusException:pass
 else:raise AssertionError('malformed payload accepted')
 assert json.loads(str(panel.GetAppearance()))==got
 assert procs[-1].poll() is None,(root/'panel.log').read_text()
 panel.ShowPreedit(False);panel.UpdateLookupTableFast([],[],[],False,False,-1,0)
 print('PASS: production GTK panel + real Qt save over D-Bus; both candidate axes, preedit, paging, full appearance acknowledgement and invalid-update rollback')
 print('LOGDIR',root)
finally:
 for proc in reversed(procs):
  proc.terminate()
  try:proc.wait(timeout=3)
  except subprocess.TimeoutExpired:proc.kill();proc.wait()
