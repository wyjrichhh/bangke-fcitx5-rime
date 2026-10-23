#!/usr/bin/env python3
"""Stage compatible OpenCC data privately, without modifying librime-data files."""
import json, pathlib, shutil, sys
root = pathlib.Path(sys.argv[1]) / 'usr/share/fcitx5-rime/seed/opencc'
root.mkdir(parents=True, exist_ok=True)
for src in pathlib.Path('/usr/share/opencc').glob('*'):
    dst = root / src.name
    if src.suffix == '.json':
        value = json.loads(src.read_text()); value.pop('normalization', None)
        dst.write_text(json.dumps(value, ensure_ascii=False))
    elif src.suffix == '.ocd2':
        shutil.copyfile(src, dst)

def explicit_group_policy(value):
    if isinstance(value, dict):
        if value.get('type') == 'group':
            # Preserve OpenCC's legacy default while satisfying its new schema.
            value.setdefault('match_policy', 'short_circuit')
        for child in value.values():
            explicit_group_policy(child)
    elif isinstance(value, list):
        for child in value:
            explicit_group_policy(child)

for config in root.glob('*.json'):
    value = json.loads(config.read_text())
    explicit_group_policy(value)
    config.write_text(json.dumps(value, ensure_ascii=False) + '\n')
