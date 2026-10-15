#!/usr/bin/env python3
"""Run inside the logged-in GNOME 46 session; restores original appearance."""
import ast
import json
import subprocess


def call(method, *args):
    result = subprocess.run([
        'gdbus', 'call', '--session', '--dest', 'org.kde.impanel',
        '--object-path', '/org/kde/impanel', '--method', f'org.kde.impanel2.{method}', *args,
    ], check=True, capture_output=True, text=True)
    return ast.literal_eval(result.stdout)[0]

identity = call('GetPanelIdentity')
assert identity.startswith('bangke-panel:gnome-'), identity
original = call('GetAppearance')
data = json.loads(original)
assert data and data['version'] == 1, 'Save appearance in Bangke Settings before running this test'
try:
    changed = json.loads(original)
    changed['style'].update(alpha='0.55', corner_radius='12', translucency='true', candidate_list_layout='stacked')
    assert call('SetAppearance', json.dumps(changed, ensure_ascii=False)) == 'applied:1'
    assert json.loads(call('GetAppearance')) == changed
    state = json.loads(call('GetPanelState'))
    assert state['backend'] == 'gnome'
    assert state['style']['alpha'] == '0.55'
    assert call('SetLayout', 'horizontal') == 'horizontal'
    assert json.loads(call('GetAppearance'))['style']['candidate_list_layout'] == 'linear'
finally:
    assert call('SetAppearance', original) == 'applied:1'
assert json.loads(call('GetAppearance')) == data
print('PASS: real GNOME panel identity, appearance round trip, layout update and configuration restoration')
