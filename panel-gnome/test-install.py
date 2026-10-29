#!/usr/bin/env python3
"""Package layout and lifecycle regression; isolated HOME and command fixtures."""
import json
import os
from pathlib import Path
import subprocess
import tempfile

repo = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as root:
    root = Path(root)
    stage = root / 'stage'
    # Layout tests run on macOS too; compile the real tray bridge in the Linux build.
    compiler = root / 'compiler'
    compiler.mkdir()
    (compiler / 'pkg-config').write_text('#!/bin/bash\nif [[ $* == *--variable=libdir* ]]; then echo /usr/lib; fi\nexit 0\n')
    (compiler / 'c++').write_text('#!/bin/bash\nwhile (($#)); do if [[ $1 == -o ]]; then touch "$2"; exit 0; fi; shift; done\nexit 1\n')
    for command in compiler.iterdir(): command.chmod(0o755)
    subprocess.run(['bash', str(repo / 'dist/lifecycle/stage.sh'), str(stage), 'ubuntu'],
                   env={**os.environ, 'PATH': f'{compiler}:{os.environ["PATH"]}'}, check=True)
    assert (stage / 'usr/lib/fcitx5/bangke-tray.so').exists()
    assert (stage / 'usr/share/fcitx5/addon/bangke-tray.conf').exists()
    ext = stage / 'usr/share/gnome-shell/extensions/bangke-panel@bangke.org'
    assert json.loads((ext / 'metadata.json').read_text())['shell-version'] == ['46']
    assert (ext / 'extension.js').exists() and (ext / 'rounded.js').exists()
    assert not (stage / 'usr/share/bangke-pinyin/caretshim.cpp').exists()
    assert not (stage / 'usr/lib/systemd/user/bangke-caret.service').exists()
    assert (stage / 'etc/xdg/autostart/bangke-pinyin-setup.desktop').exists()
    home, bin_dir = root / 'home', root / 'bin'
    home.mkdir(); bin_dir.mkdir()
    def command(name, text):
        path = bin_dir / name
        path.write_text('#!/bin/bash\nset -eu\n' + text)
        path.chmod(0o755)
    command('gdbus', '''if [[ $* == *NameHasOwner* ]]; then
  if [[ ${PANEL_CONFLICT:-0} == 1 ]]; then echo '(true,)'; else echo '(false,)'; fi
else
  if [[ ${PANEL_CONFLICT:-0} == 1 ]]; then echo "('other-panel',)";
  else echo "('bangke-panel:gnome-1',)"; fi
fi
''')
    command('gnome-extensions', '''case "$1" in
info) [[ ${UNKNOWN_EXTENSION:-0} != 1 ]] ;;
list) [[ ! -f "$HOME/enabled" ]] || echo bangke-panel@bangke.org ;;
enable) touch "$HOME/enabled" ;;
disable) rm -f "$HOME/enabled" ;;
esac
''')
    command('bangke-pinyin-config', '''echo '{"version":1,"style":{}}'
''')
    env = {**os.environ, 'HOME': str(home), 'XDG_CONFIG_HOME': str(home / '.config'),
           'XDG_DATA_HOME': str(home / '.local/share'), 'PATH': f'{bin_dir}:{os.environ["PATH"]}',
           'XDG_CURRENT_DESKTOP': 'ubuntu:GNOME'}
    script = repo / 'dist/lifecycle/gnome.sh'
    def run(action='install', **extra):
        subprocess.run(['bash', str(script), action], env={**env, **extra}, check=True)
    record = home / '.local/share/bangke-pinyin/gnome-panel-enabled'
    appearance = home / '.config/bangke-panel/appearance.json'
    run(PANEL_CONFLICT='1')
    assert not record.exists() and not appearance.exists()
    run(UNKNOWN_EXTENSION='1')
    assert not record.exists() and not (home / 'enabled').exists()
    run()
    assert record.exists() and (home / 'enabled').exists() and appearance.exists()
    run()
    run('remove')
    assert not record.exists() and not (home / 'enabled').exists()
    assert appearance.exists()  # uninstall retains user configuration
    (home / 'enabled').touch()  # user manually enabled an extension
    run(); run('remove')
    assert (home / 'enabled').exists() and not record.exists()
print('PASS: Ubuntu package layout, panel conflict, deferred first login, idempotent enable/remove, user choice preservation')

# A clean Git checkout has no vendored Lua plugin and may contain a macOS-only
# AI symlink. Exercise the actual staging commands with isolated source inputs.
build = (repo / 'dist/build-22.04.sh').read_text()
start = build.index('  rm -rf /out/librime/plugins/')
end = build.index('  find /out/librime', start)
with tempfile.TemporaryDirectory() as root:
    root = Path(root)
    plugins = root / 'out/librime/plugins'
    ai, lua, external = root / 'plugin', root / 'vendor/librime-lua', root / 'external'
    for path in (plugins, ai, lua, external): path.mkdir(parents=True)
    (ai / 'CMakeLists.txt').write_text('# AI source\n')
    (lua / 'CMakeLists.txt').write_text('# Lua source\n')
    (external / 'keep').touch()
    (plugins / 'ai-predict').symlink_to(external, target_is_directory=True)
    stage = build[start:end]
    for source, target in (('/out', root / 'out'), ('/plugin/', ai), ('/vendor', root / 'vendor')):
        stage = stage.replace(source, str(target) + ('/' if source.endswith('/') else ''))
    subprocess.run(['bash', '-ec', stage], check=True)
    assert (plugins / 'lua/CMakeLists.txt').read_text() == '# Lua source\n'
    assert (plugins / 'librime-ai-predict/CMakeLists.txt').read_text() == '# AI source\n'
    assert not (plugins / 'ai-predict').exists()
    assert (external / 'keep').exists()
print('PASS: clean Ubuntu source staging includes Lua and AI without retaining host symlinks')

# Default package metadata must follow the frontend version, while explicit
# Debian test versions remain supported.
package = (repo / 'dist/repack-container.sh').read_text()
version = package[package.index('VERSION='):package.index('\nARCH=')]
with tempfile.TemporaryDirectory() as root:
    root = Path(root)
    (root / 'CMakeLists.txt').write_text('project(fcitx5-rime VERSION 6.7.8)\n')
    script = version.replace('/priv', str(root)) + '\nprintf "%s" "$VERSION"'
    environment = {key: value for key, value in os.environ.items() if key != 'VERSION'}
    for override, expected in ((None, '6.7.8'), ('5.0.99+gnome1', '5.0.99+gnome1')):
        env = dict(environment)
        if override: env['VERSION'] = override
        result = subprocess.run(['bash', '-ec', script], env=env,
                                check=True, capture_output=True, text=True)
        assert result.stdout == expected, result.stdout
print('PASS: Ubuntu package version follows CMake and preserves explicit overrides')

# The Ubuntu entry point must preserve the caller's package version. System
# os-release also defines VERSION (a human-readable string with spaces).
entry = (repo / 'dist/build-ubuntu24.sh').read_text()
check = entry[:entry.index('bash /priv/dist/build-22.04.sh')]
with tempfile.TemporaryDirectory() as root:
    root = Path(root)
    os_release = root / 'os-release'
    os_release.write_text('ID=ubuntu\nVERSION_ID=24.04\nVERSION="24.04 LTS (Noble Numbat)"\n')
    check = check.replace('/etc/os-release', str(os_release))
    result = subprocess.run(['bash', '-c', check + '\nprintf "%s" "$VERSION"'],
                            env={**os.environ, 'VERSION': '5.0.19+gnome1'}, check=True, capture_output=True, text=True)
    assert result.stdout == '5.0.19+gnome1', result.stdout
print('PASS: Ubuntu environment check preserves the package version')

# Package managers may start the lifecycle without XWayland authentication.
# Reproduce that environment and verify the restarted IM inherits it from systemd.
with tempfile.TemporaryDirectory() as root:
    root = Path(root)
    home, commands, base = root / 'home', root / 'bin', root / 'base'
    for path in (home, commands, base): path.mkdir()
    auth = str(root / 'desktop auth')
    fixtures = {
        'id': 'echo 1000\n',
        'flock': 'exit 0\n',
        'systemctl': '''case "$*" in
*show-environment*) printf 'DISPLAY=:99\\nXAUTHORITY=%s\\nXDG_CURRENT_DESKTOP=GNOME\\n' "$TEST_DESKTOP_AUTH" ;;
*is-active*) exit 0 ;;
*" start "*) printf '%s' "$XAUTHORITY" > "$HOME/restarted-auth" ;;
esac
''',
    }
    for name, body in fixtures.items():
        path = commands / name
        path.write_text('#!/bin/bash\nset -eu\n' + body)
        path.chmod(0o755)
    (base / 'user-data.py').write_text('import os\nassert os.environ["XAUTHORITY"] == os.environ["TEST_DESKTOP_AUTH"]\n')
    (base / 'ubuntu').touch()
    (base / 'gnome.sh').write_text('test "$XAUTHORITY" = "$TEST_DESKTOP_AUTH"\n')
    env = {**os.environ, 'HOME': str(home), 'XDG_CONFIG_HOME': str(home / '.config'),
           'XDG_CACHE_HOME': str(home / '.cache'), 'XDG_RUNTIME_DIR': str(root / 'runtime'),
           'PATH': f'{commands}:{os.environ["PATH"]}', 'BANGKE_LIFECYCLE_DIR': str(base),
           'TEST_DESKTOP_AUTH': auth}
    env.pop('XAUTHORITY', None)
    subprocess.run(['bash', str(repo / 'dist/lifecycle/session.sh'), 'install'], env=env, check=True)
    assert (home / 'restarted-auth').read_text() == auth
print('PASS: package lifecycle restores desktop authentication before restarting the input method')
