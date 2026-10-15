#!/usr/bin/env python3
"""Maintain only Bangke's profile entry and checksum-tracked seed files."""
import argparse, hashlib, json, os, pathlib, re, shutil, tempfile, subprocess

def atomic(path, content):
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, tmp = tempfile.mkstemp(dir=path.parent, prefix='.bangke-')
    try:
        with os.fdopen(fd, 'w') as stream: stream.write(content)
        os.replace(tmp, path)
    finally:
        if os.path.exists(tmp): os.unlink(tmp)

def profile(text, remove=False, target_name="rime-ai"):
    sections = []
    for line in text.splitlines(keepends=True):
        if line.startswith('['): sections.append([line.strip(), [line]])
        elif sections: sections[-1][1].append(line)
        else: sections.append(['', [line]])
    items = {}
    for header, lines in sections:
        match = re.fullmatch(r'\[Groups/(\d+)/Items/\d+\]', header)
        if match:
            name = next((x.strip()[5:] for x in lines if x.startswith('Name=')), '')
            items.setdefault(match[1], []).append(name)
    if not remove and any('rime-ai' in names for names in items.values()): return text
    if not remove:
        groups = [re.fullmatch(r'\[Groups/(\d+)\]', h) for h, _ in sections]
        group = next((m[1] for m in groups if m), None)
        if group is None:
            return text.rstrip() + '\n[Groups/0]\nName=Default\nDefault Layout=us\nDefaultIM=rime-ai\n\n[Groups/0/Items/0]\nName=keyboard-us\nLayout=\n\n[Groups/0/Items/1]\nName=rime-ai\nLayout=\n\n[GroupOrder]\n0=Default\n'
        # Append, preserving existing selection, group order, item layouts and unknown keys.
        indices = [int(m[1]) for h, _ in sections if (m := re.fullmatch(r'\[Groups/' + group + r'/Items/(\d+)\]', h))]
        entry = f'\n[Groups/{group}/Items/{max(indices, default=-1)+1}]\nName=rime-ai\nLayout=\n\n'
        order = re.search(r'(?m)^\[GroupOrder\]', text)
        offset = order.start() if order else len(text)
        return text[:offset].rstrip() + '\n' + entry + text[offset:]
    out, counters = [], {}
    for header, lines in sections:
        match = re.fullmatch(r'\[Groups/(\d+)/Items/\d+\]', header)
        if match:
            group = match[1]
            if any(line.strip() == f'Name={target_name}' for line in lines): continue
            index = counters.get(group, 0); counters[group] = index + 1
            lines = [f'[Groups/{group}/Items/{index}]\n'] + lines[1:]
        else:
            group = re.fullmatch(r'\[Groups/(\d+)\]', header)
            fallback = next((x for x in items.get(group[1], []) if x != target_name), '') if group else ''
            lines = [f'DefaultIM={fallback}\n' if x.strip() == f'DefaultIM={target_name}' else x for x in lines]
        out.extend(lines)
    return ''.join(out)

def digest(path): return hashlib.sha256(path.read_bytes()).hexdigest()

def synchronize(seed, dest, manifest):
    previous = json.loads(manifest.read_text()) if manifest.exists() else {}
    current = {}
    for src in sorted(seed.rglob('*')):
        if not src.is_file(): continue
        rel = str(src.relative_to(seed)); dst = dest / rel; checksum = digest(src)
        current[rel] = checksum
        # custom.yaml is always user-owned, including changes made by the settings UI.
        if not dst.exists() or (not rel.endswith('.custom.yaml') and previous.get(rel) == digest(dst)):
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(src, dst)
    for rel, checksum in previous.items():
        dst = dest / rel
        if rel not in current and not rel.endswith('.custom.yaml') and dst.is_file() and digest(dst) == checksum:
            dst.unlink()
    atomic(manifest, json.dumps(current, ensure_ascii=False, indent=2))

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('action', choices=['install', 'remove'])
    parser.add_argument('--ubuntu', action='store_true')
    parser.add_argument('--seed', type=pathlib.Path, default=pathlib.Path('/usr/share/fcitx5-rime/seed'))
    args = parser.parse_args()
    home = pathlib.Path.home()
    config = pathlib.Path(os.environ.get('XDG_CONFIG_HOME', home / '.config'))
    data = pathlib.Path(os.environ.get('XDG_DATA_HOME', home / '.local/share'))
    path = config / 'fcitx5/profile'
    before = path.read_text() if path.exists() else ''
    legacy = data / 'bangke-pinyin/legacy-migrated'
    old_addon_gone = not pathlib.Path('/usr/share/fcitx5/inputmethod/rime.conf').exists()
    if args.action == 'install' and (old_addon_gone or pathlib.Path('/var/lib/bangke-pinyin/migrate-rime').exists()) and not legacy.exists():
        # Migrate a replaced addon, or a legacy addon proven Bangke-branded by pre-upgrade.
        before_migration = before
        if re.search(r'(?m)^Name=rime-ai$', before):
            before = re.sub(r'(?m)^DefaultIM=rime$', 'DefaultIM=rime-ai', before)
            before = profile(before, remove=True, target_name='rime')
        else:
            before = re.sub(r'(?m)^(Name|DefaultIM)=rime$', r'\1=rime-ai', before)
        if before != before_migration:
            shutil.copyfile(path, path.with_suffix('.bangke-legacy-backup'))
            atomic(path, before)
        legacy.parent.mkdir(parents=True, exist_ok=True)
        legacy.touch()
    after = profile(before, args.action == 'remove')
    if before != after:
        if path.exists(): shutil.copyfile(path, path.with_suffix('.bangke-backup'))
        atomic(path, after)
    if args.action == 'install':
        synchronize(args.seed, data / 'fcitx5/rime', data / 'bangke-pinyin/seed-manifest.json')
    if args.ubuntu:
        selection = data / 'bangke-pinyin/input-framework.json'
        xinput = home / '.xinputrc'
        if args.action == 'install' and not selection.exists() and shutil.which('im-config'):
            previous = xinput.read_text() if xinput.exists() else None
            # im-config refuses to overwrite manually maintained startup scripts.
            result = subprocess.run(['im-config', '-n', 'fcitx5'])
            if result.returncode == 0 and xinput.exists() and xinput.read_text() != previous:
                atomic(selection, json.dumps({'previous': previous, 'sha256': digest(xinput)}))
        elif args.action == 'remove' and selection.exists():
            record = json.loads(selection.read_text())
            if xinput.exists() and digest(xinput) == record['sha256']:
                if record['previous'] is None: xinput.unlink()
                else: atomic(xinput, record['previous'])
            selection.unlink()
    # Never remove user dictionaries, custom YAML, learned data or user-edited themes.

if __name__ == '__main__': main()
