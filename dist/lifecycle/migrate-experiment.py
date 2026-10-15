#!/usr/bin/env python3
"""Retire only known experimental panel/caret startup entries."""
import os, pathlib, re, shutil, sys
home = pathlib.Path.home()
config = pathlib.Path(os.environ.get('XDG_CONFIG_HOME', home / '.config'))
conf = config / 'hypr/hyprland.conf'
if conf.exists():
    text = conf.read_text()
    lines = text.splitlines(keepends=True)
    kept = [line for line in lines if not (re.match(r'\s*exec-once\s*=', line) and
        ('/bangke-share/panel-omarchy/hyprland-caret-plugin/bangke-im-caret.so' in line or
         '/usr/local/bin/bangke-panel' in line))]
    if kept != lines:
        shutil.copyfile(conf, conf.with_suffix('.conf.bangke-package-backup'))
        conf.write_text(''.join(kept))
unit = config / 'systemd/user/bangke-panel.service'
if unit.exists() and 'ExecStart=/usr/local/bin/bangke-panel' in unit.read_text():
    shutil.copyfile(unit, unit.with_suffix('.service.bangke-package-backup'))
    unit.unlink()
# Unload the old shared-directory plugin before loading the packaged replacement.
if os.environ.get('HYPRLAND_INSTANCE_SIGNATURE'):
    import subprocess
    result = subprocess.run(['hyprctl', '-j', 'plugin', 'list'], capture_output=True, text=True)
    # The known old absolute path is safe even if that plugin is not loaded.
    old = home / 'bangke-share/panel-omarchy/hyprland-caret-plugin/bangke-im-caret.so'
    subprocess.run(['hyprctl', 'plugin', 'unload', str(old)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    if '--remove' in sys.argv:
        cache = pathlib.Path(os.environ.get('XDG_CACHE_HOME', home / '.cache'))
        subprocess.run(['hyprctl', 'plugin', 'unload', str(cache / 'bangke-pinyin/caret/bangke-im-caret.so')], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
