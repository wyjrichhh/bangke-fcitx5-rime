#!/usr/bin/env python3
import importlib.util, pathlib, tempfile
spec = importlib.util.spec_from_file_location('data', pathlib.Path(__file__).with_name('user-data.py'))
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
original = '''[Groups/0]
Name=Primary
DefaultIM=chewing
Custom=preserve

[Groups/0/Items/0]
Name=keyboard-us
Layout=us

[Groups/0/Items/1]
Name=chewing
Layout=tw
Extra=keep

[Groups/1]
Name=Second
DefaultIM=keyboard-de

[Groups/1/Items/0]
Name=keyboard-de
Layout=de

[GroupOrder]
0=Primary
1=Second
'''
installed = m.profile(original)
assert installed.count('Name=rime-ai') == 1
assert 'DefaultIM=chewing' in installed and 'Extra=keep' in installed
assert m.profile(installed) == installed
removed = m.profile(installed, True)
assert 'rime-ai' not in removed and original.rstrip() == removed.rstrip()
# Remove arbitrary section keys/order, multiple groups, and repair selected defaults.
odd = '[Groups/4]\nDefaultIM=rime-ai\n[Groups/4/Items/7]\nExtra=x\nLayout=\nName=rime-ai\nOther=x\n[Groups/4/Items/9]\nLayout=de\nName=keyboard-de\n[GroupOrder]\n0=Other\n'
clean = m.profile(odd, True)
assert 'rime-ai' not in clean and 'Other=x' not in clean
assert 'DefaultIM=keyboard-de' in clean and '[Groups/4/Items/0]' in clean
with tempfile.TemporaryDirectory() as root:
    root=pathlib.Path(root); seed=root/'seed'; dest=root/'rime'; state=root/'state.json'
    seed.mkdir(); (seed/'dict.txt').write_text('v1'); (seed/'x.custom.yaml').write_text('factory')
    m.synchronize(seed,dest,state)
    (dest/'x.custom.yaml').write_text('user settings'); (dest/'learned.userdb').write_text('learned')
    (seed/'dict.txt').write_text('v2'); (seed/'x.custom.yaml').write_text('new factory')
    m.synchronize(seed,dest,state)
    assert (dest/'dict.txt').read_text() == 'v2'
    assert (dest/'x.custom.yaml').read_text() == 'user settings'
    (dest/'dict.txt').write_text('user dictionary'); (seed/'dict.txt').write_text('v3')
    m.synchronize(seed,dest,state)
    assert (dest/'dict.txt').read_text() == 'user dictionary'
    (seed/'dict.txt').unlink(); m.synchronize(seed,dest,state)
    assert (dest/'dict.txt').exists() and (dest/'learned.userdb').read_text() == 'learned'
    (seed/'obsolete.txt').write_text('owned'); m.synchronize(seed,dest,state)
    (seed/'obsolete.txt').unlink(); m.synchronize(seed,dest,state)
    assert not (dest/'obsolete.txt').exists()
both = installed + '\n[Groups/0/Items/8]\nName=rime\nLayout=\n'
merged = m.profile(both, remove=True, target_name='rime')
assert merged.count('Name=rime-ai') == 1 and '\nName=rime\n' not in merged
print('PASS: idempotent registration, multi-group removal, defaults/layout preservation, seed update and user-data retention')
