#!/usr/bin/env python3
"""蚌壳拼音的候选面板（Omarchy / wlroots 版：Wayland + layer-shell）。

为什么另起一个面板，而不是继续用 classicui 主题：
  classicui 画不出圆角高亮/单行布局/动画，而且它的主题是**全局**的（改配色会连带影响
  别的输入法）。自绘面板这两件事都能做，且**不需要改 fcitx5**。

数据来源：蚌壳 addon 的独立 D-Bus 通道 org.bangke.Panel。
仅蚌壳输入上下文使用此面板，其他输入法保留 Fcitx5 原有 UI。
定位方式：layer-shell（合成器给"贴在指定位置"的正规口子），锚在左上、用 margin 顶到
光标下方 —— 这是 Wayland 下客户端唯一能自己定位的正规途径。

GNOME/Mutter 没有 layer-shell，这个程序在那边会退回普通窗口（贴不住光标，仅作降级）。
"""
import os
import sys

import glob
import json
import shutil
import socket
import subprocess
import time

import cairo  # noqa: F401  —— GTK3 的 draw 回调靠它把 cairo.Context 交给 Python
import dbus
import dbus.exceptions
import dbus.mainloop.glib
import dbus.service
import gi

gi.require_version("Gtk", "3.0")
gi.require_version("PangoCairo", "1.0")
# Gdk 必须单独钉版本：Arch 上 GTK3/GTK4 并存时，不钉的话 PyGObject 会把无版本的
# `Gdk` 解析成 4.0，然后 `Gtk 3.0` 反过来说"4.0 已经加载了" —— 进程直接崩在 import。
gi.require_version("Gdk", "3.0")
from gi.repository import GLib, Gio, Gtk, Pango, PangoCairo  # noqa: E402

HAS_LAYER_SHELL = False
try:
    gi.require_version("GtkLayerShell", "0.1")
    from gi.repository import GtkLayerShell  # noqa: E402

    HAS_LAYER_SHELL = True
except (ValueError, ImportError):
    GtkLayerShell = None

# 每次改动都往上加一 —— 排查时一眼能确认"跑的是哪一版"
PANEL_VERSION = "2026-10-22.1-private-input-panel"

SERVICE = "org.bangke.Panel"
PATH = "/org/bangke/Panel"
IFACE = "org.bangke.Panel1"

# 我们自己的物料配色（底/正文/强调），以及给别的输入法用的中性配色
OURS = {"back": (0xFA / 255, 0xF1 / 255, 0xEA / 255), "text": (0x2B / 255, 0x26 / 255, 0x22 / 255)}
NEUTRAL = {"back": (0.13, 0.13, 0.15), "text": (0.92, 0.92, 0.92)}
ACCENT = (0xFF / 255, 0x7E / 255, 0x3D / 255)
LABEL = (0.62, 0.55, 0.50)
OUR_IM = "蚌壳拼音"
FONT = os.environ.get("BANGKE_PANEL_FONT", "LXGW WenKai")
SPOT_GAP = 5             # 光标与面板之间那条缝 —— 和 mac 版 BangkeTheme.offsetHeight 一致

# fcitx5 → 面板的方法表（签名是总线实测出来的，见 README）
METHODS = {
    "GetPanelIdentity": ("", "s"),
    "SetLayout": ("s", "s"),
    "SetAppearance": ("s", "s"),
    "GetAppearance": ("", "s"),
    "GetPanelState": ("", "s"),
    "RegisterProperties": ("as", ""),
    "UpdateProperty": ("a(ssssss)", ""),
    "UpdateAux": ("ss", ""),
    "UpdateLookupTable": ("asasasbbii", "b"),
    "UpdateLookupTableFast": ("asasasbbii", "b"),
    "UpdateLookupTableCursor": ("i", ""),
    "SetLookupTable": ("asasasbbii", "b"),
    "UpdatePreeditText": ("ssib", ""),
    "UpdatePreeditCaret": ("i", ""),
    "UpdateSpotLocation": ("ii", ""),
    "SetSpotRect": ("iiii", ""),
    # **必须声明**它，而且**当绝对坐标处理**。fcitx5 握手后 Introspect 面板能力，发现支持
    # 相对坐标就改用这个方法发；而它**从不检查返回值** —— 面板不声明就静默全丢（实测：
    # 不声明时坐标一条都收不到）。上游实现里相对版传的就是同一份 cursorRect 数值，
    # 不做任何换算，区别只是方法名。
    "SetRelativeSpotRect": ("iiii", ""),
    # 多带一个缩放（double）。前四个同样是 cursorRect 的物理像素 —— 有它就不必再靠
    # "超出逻辑屏幕范围 = 物理值" 去猜（那条猜法对屏幕左半边的坐标是失效的）。
    "SetRelativeSpotRectV2": ("iiiid", ""),
    "UpdateScreen": ("i", ""),
    "ShowPreedit": ("b", ""),
    "ShowLookupTable": ("b", ""),
    "ShowAux": ("b", ""),
    "Show": ("b", ""),
    "Enable": ("b", ""),
    "ExecDialog": ("s", ""),
    "ExecMenu": ("as", ""),
    "Configure": ("", ""),
    "Exit": ("", ""),
    "ReloadConfig": ("", ""),
    "Ping": ("s", "s"),
}

S = {"labels": [], "cands": [], "comments": [], "cursor": -1, "spot": (0, 0, 0, 0),
     "composition": None, "composition_active": None, "details": None, "enabled": True, "lookup_visible": True, "preedit": "", "preedit_visible": False, "prev_page": False, "next_page": False,
     "spot_scale": 0.0, "spot_win": None, "im": ""}


_WIN_CACHE = {"t": 0.0, "key": None, "rect": None, "desc": ""}
_CUR_CACHE = {"t": 0.0, "pos": None}


def focused_window():
    """当前聚焦窗口 (key, rect, desc)：几何、标识、以及一行"是谁"（打日志用）。

    key 用来判断攒下的光标还作不作数；desc 用来回答"哪些应用给得出坐标" ——
    日志里每条光标后面跟一个 desc，一眼能看出是哪类应用给的。

    为什么要它：原生 Wayland 应用的光标坐标走的是 app→合成器，**不经过 fcitx5**
    （fcitx5 的候选窗是交给合成器去摆的，见 README「位置」一节）—— 所以坐标只在
    经 fcitx5 前端的应用里才拿得到，foot 这类拿不到，得退到鼠标/窗口。
    Hyprland 用 `hyprctl`，sway 用 `swaymsg -t get_tree`。
    """
    now = time.monotonic()
    if now - _WIN_CACHE["t"] < 0.5:          # 每次按键都 fork 一个进程太浪费
        return _WIN_CACHE["key"], _WIN_CACHE["rect"], _WIN_CACHE["desc"]
    key, rect, desc = None, None, ""
    try:
        if shutil.which("hyprctl"):
            d = json.loads(subprocess.run(["hyprctl", "-j", "activewindow"],
                                          capture_output=True, text=True, timeout=1).stdout)
            if d.get("at") and d.get("size"):
                key = str(d.get("address") or d.get("class") or "")
                rect = (int(d["at"][0]), int(d["at"][1]), int(d["size"][0]), int(d["size"][1]))
                desc = f"{d.get('class')}|xwayland={d.get('xwayland')}"
        elif shutil.which("swaymsg"):
            env = dict(os.environ)
            if "SWAYSOCK" not in env:      # 跑成 systemd 服务时未必带这个变量
                found = glob.glob(os.path.join(env.get("XDG_RUNTIME_DIR", ""), "sway-ipc.*"))
                if found:
                    env["SWAYSOCK"] = found[0]
            proc = subprocess.run(["swaymsg", "-t", "get_tree", "-r"],
                                  capture_output=True, text=True, timeout=2, env=env)
            if proc.returncode != 0:
                log("窗口探测(swaymsg)失败:", (proc.stderr or "").strip()[:120])
                _WIN_CACHE.update(t=now, key=None, rect=None, desc="")
                return None, None, ""
            tree = json.loads(proc.stdout)

            def walk(node):
                if node.get("focused"):
                    return node
                for ch in list(node.get("nodes", [])) + list(node.get("floating_nodes", [])):
                    got = walk(ch)
                    if got:
                        return got
                return None

            n = walk(tree)
            if n and n.get("rect"):
                r = n["rect"]
                key = str(n.get("id") or "")
                rect = (int(r["x"]), int(r["y"]), int(r["width"]), int(r["height"]))
                desc = f"{n.get('app_id') or (n.get('window_properties') or {}).get('class')}"
    except Exception as e:  # noqa: BLE001  —— 探测失败就退回屏幕兜底
        log("窗口探测失败:", type(e).__name__, str(e)[:100])
        key, rect, desc = None, None, ""
    _WIN_CACHE.update(t=now, key=key, rect=rect, desc=desc)
    return key, rect, desc


_IPC_CACHE = {"t": 0.0, "sock": None}


def hypr_socket():
    """Hyprland 的 IPC socket。跑成 systemd 服务时环境里没有 HYPRLAND_INSTANCE_SIGNATURE，
    所以找不到就自己在 $XDG_RUNTIME_DIR/hypr/*/ 里挑一个。"""
    now = time.monotonic()
    if now - _IPC_CACHE["t"] < 30 and _IPC_CACHE["sock"]:
        return _IPC_CACHE["sock"]
    runtime = os.environ.get("XDG_RUNTIME_DIR", "")
    sig = os.environ.get("HYPRLAND_INSTANCE_SIGNATURE")
    cands = ([os.path.join(runtime, "hypr", sig, ".socket.sock")] if sig else []) + \
        sorted(glob.glob(os.path.join(runtime, "hypr", "*", ".socket.sock")))
    sock = next((c for c in cands if os.path.exists(c)), None)
    _IPC_CACHE.update(t=now, sock=sock)
    return sock


def plugin_caret():
    """问 Hyprland 的 bangke-im-caret 插件要输入光标矩形 (x, y, w, h)，全局逻辑像素。

    原生 Wayland 应用的光标只发给合成器（见 README），所以**只有装了那个插件**才拿得到；
    没装就返回 None，退回下面那几条。装了以后 X11 应用也照样能拿到（那条路本来就有坐标）。
    """
    sock = hypr_socket()
    if not sock:
        return None
    try:
        with socket.socket(socket.AF_UNIX) as s:
            s.settimeout(0.15)
            s.connect(sock)
            s.sendall(b"bangkeimcaret")
            chunks = []
            while True:
                b = s.recv(4096)
                if not b:
                    break
                chunks.append(b)
        d = json.loads(b"".join(chunks).decode("utf-8", "replace").strip())
        if d.get("ok"):
            return (int(d["x"]), int(d["y"]), int(d["w"]), int(d["h"]))
    except Exception:  # noqa: BLE001  —— 没装插件 / 连接失败，都走兜底
        return None
    return None


_INSET_CACHE = {"t": 0.0, "inset": (0, 0, 0, 0)}


def monitor_inset():
    """聚焦显示器被状态栏占掉的边距 (上, 下, 左, 右)，逻辑像素。

    **layer-shell 的 margin 是从工作区量起的**（合成器会把别的 surface 声明的
    exclusive zone 刨掉），不是从显示器边缘 —— 不刨的话面板整体偏一个状态栏的高度。
    实测：算 694、渲染在 720，差的 26px 正好是 omarchy 顶栏的高度。用 hyprctl monitors
    的 `reserved` 拿（[左, 上, 右, 下]），GDK 在 Wayland 上给不出这个。
    """
    now = time.monotonic()
    if now - _INSET_CACHE["t"] < 5:
        return _INSET_CACHE["inset"]
    inset = (0, 0, 0, 0)
    try:
        if shutil.which("hyprctl"):
            mons = json.loads(subprocess.run(["hyprctl", "-j", "monitors"],
                                             capture_output=True, text=True, timeout=1).stdout)
            m = next((x for x in mons if x.get("focused")), mons[0] if mons else None)
            if m and m.get("reserved"):
                left, top, right, bottom = (int(v) for v in m["reserved"][:4])
                inset = (top, bottom, left, right)
    except Exception as e:  # noqa: BLE001
        log("状态栏边距探测失败:", type(e).__name__, str(e)[:80])
    _INSET_CACHE.update(t=now, inset=inset)
    return inset


class Area:
    """工作区矩形（显示器几何刨掉状态栏）。够 place_target 用的那四个字段。"""

    __slots__ = ("x", "y", "width", "height")

    def __init__(self, x, y, width, height):
        self.x, self.y, self.width, self.height = int(x), int(y), int(width), int(height)


def pointer_pos():
    """鼠标位置 (x, y)，逻辑像素（和窗口几何同一套坐标）。

    光标拿不到时的第一备选：点了终端再打字，鼠标就落在要打字的那一行附近 ——
    比"贴窗口左下角"可信得多。拿不到就返回 None，退回窗口那条。
    """
    now = time.monotonic()
    if now - _CUR_CACHE["t"] < 0.5:
        return _CUR_CACHE["pos"]
    pos = None
    try:
        if shutil.which("hyprctl"):
            d = json.loads(subprocess.run(["hyprctl", "-j", "cursorpos"],
                                          capture_output=True, text=True, timeout=1).stdout)
            if "x" in d and "y" in d:
                pos = (int(d["x"]), int(d["y"]))
    except Exception as e:  # noqa: BLE001
        log("鼠标探测失败:", type(e).__name__, str(e)[:80])
    _CUR_CACHE.update(t=now, pos=pos)
    return pos


def _below_cursor(x, y, ch, ph, geo):
    """贴光标下方；下面放不下就翻到光标上方 —— 和 mac 版前端同规则。"""
    py = y + (ch or 24) + SPOT_GAP
    if geo and py + ph > geo.y + geo.height - 4:
        py = y - SPOT_GAP - ph
    return x, py


def _fit(px, py, w, ph, geo):
    """夹进屏幕：右边放不下左移、下边放不下上移（mac 版同款，放在摆位之后做）。"""
    if not geo:
        return int(px), int(py)
    if w <= geo.width - 8:
        px = max(geo.x + 4, min(px, geo.x + geo.width - 4 - w))
    if ph <= geo.height - 8:
        py = max(geo.y + 4, min(py, geo.y + geo.height - 4 - ph))
    return int(px), int(py)


def place_target(w, ph, geo, scale):
    """面板该放哪儿 → (px, py, 来源说明)。

    顺序：① 合成器插件给的光标矩形（装了插件才有，原生 Wayland 应用也只有它给得出）
    ② fcitx5 推的那条（**还属于当前窗口**才用 —— 从 X11 应用切到 foot 时，手里那条是
    上一个窗口的，用了就飞到别的窗口去）③ 鼠标（在聚焦窗口内）④ 窗口左下角内侧 ⑤ 屏幕左下角。
    有光标时：面板左边 = 光标左边、面板上边 = 光标下边 + SPOT_GAP，最后统一夹进屏幕 ——
    这三条都照 mac 版前端（BangkePanel 横向布局那一支 + offsetHeight）。
    """
    win_key, win, win_desc = focused_window()
    cursor = plugin_caret()
    if cursor:
        x, y = _below_cursor(cursor[0], cursor[1], cursor[3], ph, geo)
        return (*_fit(x, y, w, ph, geo), f"光标(合成器) rect={cursor} ← {win_desc}",cursor)
    x, y, cw, ch = S["spot"]
    stale = (S["spot_win"] is not None and win_key is not None
             and S["spot_win"] != win_key)
    if (x or y) and not stale:
        x, y, _cw, ch = to_logical_rect(x, y, cw, ch, geo, S["spot_scale"] or scale,
                                        known=bool(S["spot_scale"]))
        px, py = _below_cursor(x, y, ch, ph, geo)
        return (*_fit(px, py, w, ph, geo), f"光标 ← {win_desc}",(x,y,_cw,ch))
    if win:
        wx, wy, ww, wh = to_logical_rect(win[0], win[1], win[2], win[3], geo, scale)
        cur = pointer_pos()
        if cur and wx <= cur[0] <= wx + ww and wy <= cur[1] <= wy + wh:
            return (*_fit(cur[0] + 16, cur[1] + 22, w, ph, geo),
                    f"鼠标({cur[0]},{cur[1]}) ← {win_desc}",None)
        return (*_fit(wx + 16, wy + wh - ph - 16, w, ph, geo),
                f"窗口({wx},{wy} {ww}x{wh}) ← {win_desc}",None)
    if geo:
        return (*_fit(24, geo.y + geo.height - ph - 24, w, ph, geo), "兜底(屏幕左下)",None)
    return 24, 24, "兜底(无屏幕信息)",None


def to_logical_rect(x, y, w, h, geo, scale, known=False):
    """把物理像素矩形换成逻辑像素（layer-shell 的 margin 是逻辑像素）。

    **四个都要换**：只换 x,y 的话高宽还是物理值，光标下方那条 6px 的缝就变成
    "加一个物理行高"，面板整体偏低约一个行高（56 物理 = 28 逻辑，正是实测偏差）。

    `known` = 缩放是协议告诉我们的（V2 带的那个参数）→ 无条件换算；
    否则只能猜：坐标超出逻辑屏幕范围才当物理值 —— 这条对屏幕左半边是失效的。
    """
    if not geo or scale <= 1:
        return x, y, w, h
    if known or x > geo.x + geo.width or y > geo.y + geo.height:
        return (int(x / scale), int(y / scale), max(1, int(w / scale)), max(1, int(h / scale)))
    return x, y, w, h


DEBUG = os.environ.get("BANGKE_PANEL_DEBUG") == "1"

def log(*a):
    print("[panel]", *a, flush=True)


import cairo as _cairo  # noqa: E402

CAIRO_SURFACE = _cairo.ImageSurface(_cairo.FORMAT_ARGB32, 1, 1)
CAIRO_CTX = _cairo.Context(CAIRO_SURFACE)  # 量宽度用（create_layout 要的是 Context 不是 Surface）


def rounded(ctx, x, y, w, h, r):
    ctx.new_sub_path()
    ctx.arc(x + w - r, y + r, r, -1.5708, 0)
    ctx.arc(x + w - r, y + h - r, r, 0, 1.5708)
    ctx.arc(x + r, y + h - r, r, 1.5708, 3.1416)
    ctx.arc(x + r, y + r, r, 3.1416, 4.7124)
    ctx.close_path()


def highlight_path(ctx, cell, width, height, panel_radius, radius):
    """Outer panel corners keep their shape; inner corners use the highlight radius."""
    x,y,w,h = cell
    inner = 0  # Continuous edge-attached highlight: internal boundaries stay square.
    outer = min(panel_radius,w/2,h/2)
    left,top = abs(x)<.01,abs(y)<.01
    right,bottom = abs(x+w-width)<.01,abs(y+h-height)<.01
    tl = outer if left and top else inner
    tr = outer if right and top else inner
    br = outer if right and bottom else inner
    bl = outer if left and bottom else inner
    ctx.new_sub_path()
    ctx.move_to(x+tl,y)
    ctx.line_to(x+w-tr,y)
    if tr: ctx.arc(x+w-tr,y+tr,tr,-1.5707963267948966,0)
    ctx.line_to(x+w,y+h-br)
    if br: ctx.arc(x+w-br,y+h-br,br,0,1.5707963267948966)
    ctx.line_to(x+bl,y+h)
    if bl: ctx.arc(x+bl,y+h-bl,bl,1.5707963267948966,3.141592653589793)
    ctx.line_to(x,y+tl)
    if tl: ctx.arc(x+tl,y+tl,tl,3.141592653589793,4.71238898038469)
    ctx.close_path()


def appearance_path():
    from pathlib import Path
    return Path(os.environ.get("XDG_CONFIG_HOME", str(Path.home() / ".config"))) / "bangke-panel/appearance.json"


def validate_appearance(raw):
    data = json.loads(raw)
    if data.get("version") != 1 or not isinstance(data.get("style"), dict):
        raise ValueError("Unsupported appearance version")
    for mode in ("light", "dark"):
        palette = data[mode]
        for key in ("back", "text", "accent", "on_accent", "muted", "border"):
            value = palette[key]
            if not isinstance(value, list) or len(value) != 4 or any(not isinstance(v, (float,int)) or not 0 <= v <= 1 for v in value):
                raise ValueError("Invalid palette: " + key)
    options = data["style"]
    for key, lo, hi in (("font_point",8,48),("corner_radius",0,30),("hilited_corner_radius",0,30),
                        ("line_spacing",-10,30),("spacing",-10,30),("shadow_size",0,30),
                        ("border_width",-10,20),("border_height",-10,20),("alpha",0,1)):
        if key in options and not lo <= float(options[key]) <= hi:
            raise ValueError("Invalid option: " + key)
    if options.get("text_orientation", "horizontal") not in ("horizontal", "vertical"):
        raise ValueError("Invalid orientation")
    if options.get("candidate_list_layout","linear") not in ("linear","stacked"):
        raise ValueError("Invalid candidate layout")
    return data


def apply_backdrop(enabled):
    if not HAS_LAYER_SHELL:
        if enabled: raise ValueError("毛玻璃需要 Hyprland Wayland 会话")
        return
    try:
        result = subprocess.run(["hyprctl","bangkeimblur","1" if enabled else "0"],capture_output=True,text=True,timeout=2)
        reply = json.loads(result.stdout)
        if not reply.get("ok") or reply.get("enabled") != enabled: raise ValueError("合成器未确认背景模糊设置")
    except (OSError,ValueError,subprocess.TimeoutExpired) as error:
        if enabled: raise ValueError("无法启用毛玻璃，请检查 bangke-caret 服务："+str(error))


def saved_appearance():
    try:
        return validate_appearance(appearance_path().read_text())
    except (OSError, ValueError, KeyError, TypeError):
        return None


def option_bool(options, key, default=False):
    return str(options.get(key, str(default))).lower() in ("true", "1")


def format_candidate(template, label, candidate, comment):
    # Preserve each role's colour even when literals/order are customized.
    import re
    parts = []
    for item in re.split(r"(\[label\]|\[candidate\]|\[comment\])", template):
        text, role = {"[label]":(label,"label"), "[candidate]":(candidate,"text"),
                      "[comment]":(comment,"comment")}.get(item, (item,"text"))
        if text:
            parts.append((text,role))
    return parts


def candidate_layout(label, candidate, comment, font_size, options, make_layout, width=None, colors=None, prepared=None):
    """Shape one complete rich paragraph. Never shorten or rescale its text."""
    runs = format_candidate(options.get("candidate_format","[label]. [candidate] [comment]"),label.rstrip("."),candidate,comment)
    text = "".join(value for value,role in runs)
    layout = prepared.copy() if prepared is not None else make_layout(text,font_size)
    layout.set_single_paragraph_mode(False)
    layout.set_ellipsize(Pango.EllipsizeMode.NONE)
    layout.set_wrap(Pango.WrapMode.WORD_CHAR)
    layout.set_justify(False)
    layout.set_alignment(Pango.Alignment.LEFT)
    if hasattr(layout.get_context(),"set_round_glyph_positions"):
        layout.get_context().set_round_glyph_positions(True)
    if width is not None:
        layout.set_width(max(1,int(width))*Pango.SCALE)
    if colors:
        attributes=Pango.AttrList();offset=0
        vertical_text=options.get("text_orientation")=="vertical" and "candidate_list_layout" in options
        for value,role in runs:
            if not value: continue
            if vertical_text:
                value=("\n" if offset else "")+"\n".join(value)
            length=len(value.encode("utf-8"));color=colors[role]
            foreground=Pango.attr_foreground_new(*(int(channel*65535) for channel in color[:3]))
            foreground.start_index=offset;foreground.end_index=offset+length;attributes.insert(foreground)
            alpha=Pango.attr_foreground_alpha_new(int((color[3] if len(color)>3 else 1)*65535))
            alpha.start_index=offset;alpha.end_index=offset+length;attributes.insert(alpha)
            offset+=length
        layout.set_attributes(attributes)
    return layout


def candidate_geometry(candidates, labels, comments, vertical, limit, make_layout, font_size=18, options=None):
    """Complete paragraphs: horizontal candidates flow to new rows, never share a shrinking budget."""
    options=options or {};limit=max(25,int(limit));pad,gap=12,8
    reference=make_layout("国Ag",font_size)
    line_height=reference.get_pixel_size()[1];baseline=reference.get_baseline()/Pango.SCALE
    spacing=float(options.get("line_spacing",5))*4/3
    padding=max(1,round((4 if vertical else 16)+spacing-20/3)//2)
    rows,widths,layouts=[],[],[]
    for i,candidate in enumerate(candidates):
        label=labels[i].strip().rstrip(".、")+"." if i<len(labels) and labels[i].strip() else str(i+1)+"."
        comment=comments[i] if i<len(comments) else ""
        lw=make_layout(label,font_size).get_pixel_size()[0]
        tw=make_layout(candidate,font_size).get_pixel_size()[0]
        cw=make_layout(comment,font_size).get_pixel_size()[0] if comment else 0
        layout=candidate_layout(label,candidate,comment,font_size,options,make_layout)
        width=min(limit,max(2*pad+1,layout.get_pixel_size()[0]+2*pad))
        layout=candidate_layout(label,candidate,comment,font_size,options,make_layout,width-2*pad)
        widths.append(width);layouts.append(layout);rows.append((label,candidate,comment,lw,tw,cw))
    cells=[];tops=[]
    if vertical:
        width=max(widths,default=1);y=0
        for i,layout in enumerate(layouts):
            top=padding+(4 if i==0 else 0);bottom=padding+(4 if i==len(rows)-1 else 0)
            h=layout.get_pixel_size()[1]+top+bottom
            cells.append((0,y,width,h));tops.append(top);y+=h
        height=y
    else:
        x=y=row_height=width=0
        for i,layout in enumerate(layouts):
            cell_width=widths[i];h=layout.get_pixel_size()[1]+2*padding
            if x and x+cell_width>limit:
                y+=row_height;x=0;row_height=0
            cells.append((x,y,cell_width,h));tops.append(padding)
            x+=cell_width;width=max(width,x);row_height=max(row_height,h)
        height=y+row_height
        # Fill each row's cell height, keeping text top-aligned instead of
        # vertically recentering unchanged text when another paragraph grows.
        heights={}
        for x,y,w,h in cells: heights[y]=max(heights.get(y,0),h)
        cells=[(x,y,w,heights[y]) for x,y,w,h in cells]
    return {"width":max(1,int(width)),"height":max(1,int(height)),"cells":cells,"rows":rows,
            "layouts":layouts,"text_tops":tops,"font":font_size,"pad":pad,"gap":gap,
            "line_height":line_height,"baseline":baseline,"options":options}


class CompositionOrigin:
    """Keep an input session's top-left stable as asynchronous results change size."""
    def __init__(self):
        self.reset()

    def reset(self):
        self.key, self.point, self.quality, self.bottom = None, None, -1, None

    def width_limit(self, key, area, fallback):
        if self.key != key or self.point is None or area is None:
            return fallback
        return max(1,min(fallback,area.x+area.width-4-self.point[0]))

    def apply(self, key, target, width, height, area, quality, caret=None):
        if self.key != key or self.point is None or quality > self.quality:
            self.key,self.point,self.quality=key,tuple(target),quality
            self.bottom=area.y+area.height-4 if area else None
            if caret and target[1]<caret[1]:
                self.bottom=min(self.bottom,caret[1]-SPOT_GAP) if self.bottom else caret[1]-SPOT_GAP
        return int(self.point[0]),int(self.point[1])

    def height_limit(self,key,area,fallback):
        if self.key!=key or self.point is None or area is None: return fallback
        bottom=self.bottom if self.bottom is not None else area.y+area.height-4
        return max(1,min(fallback,int(bottom-self.point[1])))


class CompositionSize:
    """Remember width until candidates disappear or the window/layout changes."""
    def __init__(self):
        self.reset()

    def reset(self):
        self.key, self.width = None, 0

    def apply(self, geometry, key, limit, vertical):
        if key != self.key:
            self.width = 0
            self.key = key
        self.width = min(limit, max(self.width, geometry["width"]))
        extra = self.width - geometry["width"]
        geometry["width"] = self.width
        if vertical:
            geometry["cells"] = [(x,y,self.width,h) for x,y,w,h in geometry["cells"]]
        elif geometry["cells"] and extra:
            x,y,w,h = geometry["cells"][-1]
            geometry["cells"][-1] = (x,y,w+extra,h)
        return geometry


def paint_candidates(ctx, geometry, selected, palette, make_layout, radius=9):
    """Clip one continuous highlight to the panel: first/last inherit its corners."""
    width, height = geometry["width"], geometry["height"]
    ctx.save()
    # GTK retains backing pixels between frames. Clear before painting alpha;
    # otherwise each redraw makes the same translucent background more opaque.
    ctx.set_operator(_cairo.OPERATOR_CLEAR)
    ctx.paint()
    ctx.set_operator(_cairo.OPERATOR_OVER)
    rounded(ctx, 0, 0, width, height, min(radius, width / 2, height / 2))
    ctx.clip()
    options = geometry.get("options", {})
    def source(color, alpha=1):
        ctx.set_source_rgba(*color[:3], (color[3] if len(color)>3 else 1)*alpha)
    ctx.push_group()
    source(palette["back"])
    ctx.paint()
    for i, (cell, row) in enumerate(zip(geometry["cells"], geometry["rows"])):
        x, y, w, h = cell
        active = i == selected
        if active:
            highlight_radius = min(float(options.get("hilited_corner_radius",0))*4/3, w/2, h/2)
            shadow = float(options.get("shadow_size",0))*4/3
            if shadow:
                for spread in range(min(30,int(shadow)),0,-1):
                    ctx.set_source_rgba(0,0,0,.025)
                    rounded(ctx,x-spread/2,y-spread/2,w+spread,h+spread,highlight_radius+spread/2)
                    ctx.fill()
            if option_bool(options,"mutual_exclusive"):
                ctx.set_operator(_cairo.OPERATOR_SOURCE)
            source(palette["accent"])
            highlight_path(ctx,cell,width,height,radius,highlight_radius)
            ctx.fill()
            ctx.set_operator(_cairo.OPERATOR_OVER)
    # Fade the complete background stack once, including selection/shadow.
    # Applying alpha only to the base leaves an opaque selection at alpha=0.
    ctx.pop_group_to_source()
    ctx.paint_with_alpha(float(options.get("alpha", 1)))
    for i, (cell, row) in enumerate(zip(geometry["cells"], geometry["rows"])):
        x, y, w, h = cell
        active = i == selected
        label,candidate,comment,lw,tw,cw=row
        colors={"label":palette.get("selected_label",palette["on_accent"]) if active else palette["muted"],
                "text":palette["on_accent"] if active else palette["text"],
                "comment":palette.get("selected_comment",palette["on_accent"]) if active else palette.get("comment",palette["accent"])}
        layout=geometry["layouts"][i]
        # Reuse exactly the shaped and wrapped paragraph used for measurement.
        colored=candidate_layout(label,candidate,comment,geometry["font"],options,make_layout,
                                 layout.get_width()/Pango.SCALE,colors,layout)
        ctx.save();ctx.rectangle(x,y,w,h);ctx.clip()
        ctx.move_to(round(x+geometry["pad"]),round(y+geometry["text_tops"][i]))
        PangoCairo.show_layout(ctx,colored)
        ctx.restore()
    border = max(0,min(float(options.get("border_width",0)),float(options.get("border_height",0))))*4/3
    if border:
        source(palette.get("border", (0,0,0,0)))
        ctx.set_line_width(border)
        rounded(ctx,border/2,border/2,max(1,width-border),max(1,height-border),max(0,radius-border/2))
        ctx.stroke()
    ctx.restore()


def panel_preferences():
    """Read saved layout without writing to fcitx5's shared classicui theme."""
    from pathlib import Path
    import re
    config_home = Path(os.environ.get("XDG_CONFIG_HOME", str(Path.home() / ".config")))
    conf = config_home / "fcitx5/conf/classicui.conf"
    vertical = False
    try:
        vertical = bool(re.search(r"(?m)^Vertical Candidate List=True\s*$", conf.read_text()))
    except OSError:
        pass
    data_home = Path(os.environ.get("XDG_DATA_HOME", str(Path.home() / ".local/share")))
    try:
        saved = (data_home / "fcitx5/bangke/bangke.custom.yaml").read_text()
        orientation = re.search(r"(?m)^\s*style/text_orientation:\s*['\"]?(horizontal|vertical)", saved)
        if orientation:
            vertical = orientation[1] == "vertical"
    except OSError:
        pass
    try:
        saved_layout = (config_home / "bangke-panel/layout").read_text().strip()
        if saved_layout in ("horizontal", "vertical"):
            vertical = saved_layout == "vertical"
    except OSError:
        pass
    saved = saved_appearance()
    if saved:
        vertical = saved["style"].get("candidate_list_layout", "stacked" if saved["style"].get("text_orientation")=="vertical" else "linear") == "stacked"
    override = os.environ.get("BANGKE_PANEL_LAYOUT", "")
    if override in ("stacked", "linear"):
        vertical = override == "stacked"
    return vertical


def panel_palette(ours):
    """Follow the current Omarchy colours; keep other input methods neutral."""
    from pathlib import Path
    import re
    if not ours:
        return dict(back=NEUTRAL["back"], text=NEUTRAL["text"], accent=(.45,.48,.55),
                    muted=(.65,.65,.68), on_accent=(1,1,1))
    state = Path(os.environ.get("XDG_STATE_HOME", str(Path.home() / ".local/state")))
    config = Path(os.environ.get("XDG_CONFIG_HOME", str(Path.home() / ".config")))
    for root in (state / "omarchy/current/theme", config / "omarchy/current/theme"):
        try:
            data = dict(re.findall(r'^([a-z_]+)\s*=\s*"(#[0-9a-fA-F]{6})"', (root / "colors.toml").read_text(), re.M))
            def color(key, fallback):
                value = data.get(key)
                return tuple(int(value[i:i+2],16)/255 for i in (1,3,5)) if value else fallback
            accent = color("accent", ACCENT)
            on_accent = (0.08,.08,.1) if sum(c*k for c,k in zip(accent,(.299,.587,.114))) > .6 else (1,1,1)
            return dict(back=color("background", NEUTRAL["back"]), text=color("foreground", NEUTRAL["text"]),
                        muted=color("dark_foreground", LABEL), accent=accent, on_accent=on_accent)
        except OSError:
            continue
    return dict(back=OURS["back"], text=OURS["text"], accent=ACCENT, muted=LABEL, on_accent=(1,1,1))


class Panel(Gtk.Window):
    def __init__(self):
        super().__init__(type=Gtk.WindowType.TOPLEVEL)
        self.set_decorated(False)
        self.set_app_paintable(True)
        self.connect("draw",self._clear_canvas)
        self.get_style_context().add_class("bangke-panel")
        transparent = Gtk.CssProvider()
        transparent.load_from_data(b"window.bangke-panel, window.bangke-panel * { background-color: transparent; background-image: none; }")
        self.get_style_context().add_provider(transparent,Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION)
        vis = self.get_screen().get_rgba_visual()
        if vis:
            self.set_visual(vis)
        if HAS_LAYER_SHELL and GtkLayerShell.is_supported():
            # 必须先于 realize 调用
            GtkLayerShell.init_for_window(self)
            GtkLayerShell.set_namespace(self, "bangke-panel")
            GtkLayerShell.set_layer(self, GtkLayerShell.Layer.OVERLAY)
            GtkLayerShell.set_anchor(self, GtkLayerShell.Edge.TOP, True)
            GtkLayerShell.set_anchor(self, GtkLayerShell.Edge.LEFT, True)
            # 不抢键盘：敲键继续进应用，鼠标点击仍能落在面板上（选候选）
            GtkLayerShell.set_keyboard_mode(self, GtkLayerShell.KeyboardMode.NONE)
        self.area = Gtk.DrawingArea()
        self.area.get_style_context().add_provider(transparent,Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION)
        self.area.connect("draw", self.on_draw)
        self.area.connect("size-allocate", self._ensure_selection_visible)
        from gi.repository import Gdk
        self.area.add_events(Gdk.EventMask.BUTTON_PRESS_MASK | Gdk.EventMask.SCROLL_MASK)
        self.area.connect("button-press-event", self.on_click)
        self.area.connect("scroll-event", self.on_scroll)
        self.options = {}
        self._blur_applied = None
        self.scroll=Gtk.ScrolledWindow()
        self.scroll.set_halign(Gtk.Align.START)
        self.scroll.set_valign(Gtk.Align.START)
        self.scroll.set_policy(Gtk.PolicyType.NEVER,Gtk.PolicyType.AUTOMATIC)
        self.scroll.set_overlay_scrolling(True)
        self.scroll.get_style_context().add_provider(transparent,Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION)
        self.scroll.add(self.area)
        self.scroll.set_shadow_type(Gtk.ShadowType.NONE)
        viewport=self.scroll.get_child()
        viewport.get_style_context().add_provider(transparent,Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION)
        self.add(self.scroll)
        self._selected_before=None
        self._scroll_pending=False
        self._visible_size=(1,1)
        self.palette = NEUTRAL
        self._last_spot = (0, 0)
        self._widths = []
        self._composition_size = CompositionSize()
        self._composition_origin = CompositionOrigin()
        self._placement_key = None
        self._placement_area = None
        self._requested_size = None
        self._refresh_source = 0
        self._theme_watches = []
        self._theme_reload_source = 0
        self._theme_generation = 0
        self._watch_themes()
        GLib.idle_add(self.reload_appearance)

    def _watch_themes(self):
        from pathlib import Path
        for watch in self._theme_watches:
            watch.cancel()
        self._theme_watches = []
        roots = [Path(os.environ.get("XDG_STATE_HOME", str(Path.home()/".local/state"))),
                 Path(os.environ.get("XDG_CONFIG_HOME", str(Path.home()/".config")))]
        for root in roots:
            # Watch the parent as well: theme/current can be atomically replaced.
            for path in (root/"omarchy",root/"omarchy/current",root/"omarchy/current/theme"):
                if path.is_dir():
                    try:
                        watch=Gio.File.new_for_path(str(path)).monitor_directory(Gio.FileMonitorFlags.WATCH_MOVES,None)
                        watch.connect("changed",self._theme_changed)
                        self._theme_watches.append(watch)
                    except GLib.Error:
                        pass

    def _theme_changed(self,*args):
        self._theme_generation += 1
        if self._theme_reload_source:
            GLib.source_remove(self._theme_reload_source)
        # Omarchy stages several files. Resolve once after the atomic switch.
        self._theme_reload_source=GLib.timeout_add(200,self._reload_theme)

    def _reload_theme(self):
        self._theme_reload_source=0
        self._watch_themes()
        return self.reload_appearance()

    def reload_appearance(self):
        self._blur_applied = None
        # Resolve YAML and system roles with the same Qt code as the preview.
        # Keep IO off GTK and reject results from superseded theme switches/saves.
        import threading
        before = saved_appearance()
        generation = self._theme_generation
        def worker():
            try:
                result = subprocess.run(["bangke-pinyin-config","--dump-panel-appearance"],capture_output=True,text=True,timeout=8)
                if result.returncode: raise ValueError(result.stderr.strip())
                data = validate_appearance(result.stdout)
                def apply():
                    if generation != self._theme_generation or saved_appearance() != before: return False
                    import tempfile
                    path = appearance_path();path.parent.mkdir(parents=True,exist_ok=True)
                    with tempfile.NamedTemporaryFile(mode="w",dir=path.parent,delete=False) as output:
                        json.dump(data,output);temporary=output.name
                    os.replace(temporary,path)
                    self.queue_refresh()
                    return False
                GLib.idle_add(apply)
            except (OSError,ValueError,KeyError,TypeError,subprocess.TimeoutExpired) as error:
                log("系统主题重载失败，保留当前外观:",str(error))
        threading.Thread(target=worker,daemon=True).start()
        return False

    # 布局：单行，宽度按候选字数估
    def _layout(self, cand, size, bold=False):
        layout = PangoCairo.create_layout(CAIRO_CTX)
        desc = Pango.FontDescription(self.options.get("font_face", FONT))
        desc.set_absolute_size(size * Pango.SCALE)
        if bold:
            desc.set_weight(Pango.Weight.BOLD)
        layout.set_font_description(desc)
        if self.options.get("text_orientation") == "vertical" and "candidate_list_layout" in self.options:
            cand = "\n".join(cand)
        layout.set_text(cand, -1)
        return layout

    def queue_refresh(self):
        # One event-driven frame update for a burst of candidate/spot messages.
        if not self._refresh_source:
            self._refresh_source = GLib.timeout_add(16, self._flush_refresh)

    def _flush_refresh(self):
        self._refresh_source = 0
        self.refresh()
        return GLib.SOURCE_REMOVE

    def measure(self):
        geo = self._screen()
        limit = max(120, min(1000, geo.width - 8)) if geo else 1000
        limit = self._composition_origin.width_limit(self._placement_key,self._placement_area,limit)
        vertical = panel_preferences()
        geometry = candidate_geometry(S["cands"] if S["lookup_visible"] else [], S["labels"], S["comments"],
                                      vertical, limit, self._layout,
                                      float(self.options.get("font_point",13.5))*4/3, self.options)
        self._preedit_height = 0
        if S["preedit_visible"] and S["preedit"]:
            layout = self._layout(S["preedit"],geometry["font"])
            layout.set_width(max(1,limit-24)*Pango.SCALE)
            layout.set_wrap(Pango.WrapMode.WORD_CHAR)
            layout.set_ellipsize(Pango.EllipsizeMode.NONE)
            layout.set_single_paragraph_mode(False)
            self._preedit_layout=layout
            self._preedit_height = max(layout.get_pixel_size()[1]+2,layout.get_pixel_size()[1]+float(self.options.get("spacing",8))*4/3)
            geometry["width"] = min(limit,max(geometry["width"],layout.get_pixel_size()[0]+24))
        self._paging_height = 24 if option_bool(self.options,"show_paging") and (S["prev_page"] or S["next_page"]) else 0
        screen_key = (geo.x, geo.y, geo.width, geo.height) if geo else None
        key = (focused_window()[0], vertical, screen_key)
        if option_bool(self.options,"memorize_size",True):
            self._geometry = self._composition_size.apply(geometry,key,limit,vertical)
        else:
            self._composition_size.reset()
            self._geometry = geometry
        if self._preedit_height:
            self._geometry["cells"] = [(x,y+self._preedit_height,w,h) for x,y,w,h in self._geometry["cells"]]
        self._geometry["height"] += self._preedit_height + self._paging_height
        return self._geometry["width"]

    def _screen(self):
        d = self.get_display()
        mon = None
        if self.get_window():
            mon = d.get_monitor_at_window(self.get_window())
        mon = mon or d.get_primary_monitor() or d.get_monitor(0)
        return mon.get_geometry() if mon else None

    def refresh(self):
        if S["im"] != OUR_IM or not S["enabled"] or (not (S["lookup_visible"] and S["cands"]) and not (S["preedit_visible"] and S["preedit"])):
            self._composition_size.reset()
            if S["composition_active"] is not True: self._composition_origin.reset()
            self._last_spot = None
            self.hide()
            return
        self.palette = panel_palette(S["im"] == OUR_IM)
        self.options = {}
        appearance = saved_appearance() if S["im"] == OUR_IM else None
        if appearance:
            self.options = appearance["style"]
            from pathlib import Path
            config = Path(os.environ.get("XDG_CONFIG_HOME",str(Path.home()/".config")))
            state = Path(os.environ.get("XDG_STATE_HOME",str(Path.home()/".local/state")))
            dark = True
            for root in (state,config):
                try:
                    import re
                    mode = (root/"omarchy/current/theme/colors.toml").read_text()
                    dark = not bool(re.search(r'^mode\s*=\s*[\"\']light',mode,re.M))
                    if not re.search(r'^mode\s*=',mode,re.M):
                        colors=dict(re.findall(r'^([a-z_]+)\s*=\s*"(#[0-9a-fA-F]{6})"',mode,re.M))
                        if "background" in colors and "foreground" in colors:
                            lum=lambda value:sum(int(value[i:i+2],16)*k for i,k in zip((1,3,5),(.299,.587,.114)))
                            dark=lum(colors["background"]) < lum(colors["foreground"])
                    break
                except OSError: pass
            palette = appearance["dark" if dark else "light"]
            self.palette = palette
        blur = option_bool(self.options,"translucency") and S["im"] == OUR_IM
        if blur != self._blur_applied:
            try: apply_backdrop(blur);self._blur_applied=blur
            except ValueError as error: log(str(error))
        self._radius = max(0,float(self.options.get("corner_radius",6.75))*4/3)
        geo = self._screen()
        t,b,l,r = monitor_inset() if HAS_LAYER_SHELL else (0,0,0,0)
        self._placement_area = Area(geo.x+l,geo.y+t,geo.width-l-r,geo.height-t-b) if geo else None
        win_id,win_rect,_ = focused_window()
        self._placement_key = (win_id,tuple(win_rect) if win_rect else None,
                               (geo.x,geo.y,geo.width,geo.height) if geo else None,
                               panel_preferences(),S["composition"])
        w = self.measure()
        content_height=self._geometry["height"]
        ph=min(content_height,max(1,self._placement_area.height-8)) if self._placement_area else content_height
        if HAS_LAYER_SHELL:
            geo = self._screen()
            scale = 1
            d = self.get_display()
            mon = d.get_monitor_at_window(self.get_window()) if self.get_window() else None
            mon = mon or d.get_primary_monitor() or d.get_monitor(0)
            if mon:
                scale = mon.get_scale_factor()
            t, b, l, r = monitor_inset()
            area = Area(geo.x + l, geo.y + t, geo.width - l - r, geo.height - t - b) if geo else None
            px, py, src, caret = place_target(w, ph, area, scale)
            if S["im"] == OUR_IM:
                px,py = self._composition_origin.apply(self._placement_key,(px,py),w,ph,area,2 if src.startswith("光标") else 1,caret)
            # 贴边翻转：下面放不下就挪到光标上方，右边放不下就右对齐。
            # layer-shell 的规矩是「锚到哪条边，margin 就从那条边量」，
            # 所以翻转 = 换锚边 + 换算 margin；竖直/水平各只能锚一条边，否则会被拉伸。
            flip_v = bool(area) and (py + ph) > (area.y + area.height - 4)
            flip_h = bool(area) and (px + w) > (area.x + area.width - 4)
            key = (px, py, flip_v, flip_h)
            if key != self._last_spot:
                GtkLayerShell.set_anchor(self, GtkLayerShell.Edge.TOP, not flip_v)
                GtkLayerShell.set_anchor(self, GtkLayerShell.Edge.BOTTOM, flip_v)
                GtkLayerShell.set_anchor(self, GtkLayerShell.Edge.LEFT, not flip_h)
                GtkLayerShell.set_anchor(self, GtkLayerShell.Edge.RIGHT, flip_h)
                if area:
                    GtkLayerShell.set_margin(
                        self, GtkLayerShell.Edge.BOTTOM if flip_v else GtkLayerShell.Edge.TOP,
                        max(0, (area.y + area.height - py - ph) if flip_v else py - area.y))
                    GtkLayerShell.set_margin(
                        self, GtkLayerShell.Edge.RIGHT if flip_h else GtkLayerShell.Edge.LEFT,
                        max(0, (area.x + area.width - px - w) if flip_h else px - area.x))
            if key != self._last_spot:
                if DEBUG: log(f"落点: {px},{py} 尺寸 {w}x{ph} 屏幕 {geo.width}x{geo.height}@{geo.x},{geo.y} "
                    f"工作区 {area.width}x{area.height}@{area.x},{area.y}(状态栏 {t}/{b}/{l}/{r}) "
                    f"缩放 {scale} 翻转 竖={flip_v} 横={flip_h} 来源={src}")
            # Anchors/margins update the existing layer surface. Unmapping here
            # recreates it and replays compositor open/close animations per update.
            self._last_spot = key
        else:
            x, y, _cw, ch = S["spot"]
            y += (ch or 24) + 6
            if S["im"] == OUR_IM:
                x,y=self._composition_origin.apply(self._placement_key,(x,y),w,ph,self._placement_area,2,(x,y-(ch or 24)-6,_cw,ch))
            self.move(x, y)
        # layer surface 的大小取自子控件的尺寸请求（resize() 对它无效）——
        # 少这一行，面板会缩成一条细缝。
        canvas_width=self._composition_origin.width_limit(self._placement_key,self._placement_area,1000) if S["im"]==OUR_IM else w
        canvas_height=self._composition_origin.height_limit(self._placement_key,self._placement_area,
                           self._placement_area.height-8 if self._placement_area else ph) if S["im"]==OUR_IM else ph
        self._visible_size=(w,min(content_height,canvas_height))
        self.area.set_size_request(w,max(canvas_height,content_height))
        self.scroll.set_size_request(w,canvas_height)
        if self._requested_size!=(canvas_width,canvas_height):
            self.set_size_request(canvas_width,canvas_height)
            self.resize(canvas_width,canvas_height)
            self._requested_size=(canvas_width,canvas_height)
        if S["cursor"]!=self._selected_before:
            self._scroll_pending=True
            GLib.idle_add(self._ensure_selection_visible)
        self._selected_before=S["cursor"]
        if not self.get_visible():
            self.show_all()
        self.queue_draw()
        if self.get_window():
            region=_cairo.Region(_cairo.RectangleInt(0,0,*map(int,self._visible_size)))
            self.get_window().input_shape_combine_region(region,0,0)

    def _clear_canvas(self,widget,ctx):
        ctx.save();ctx.set_operator(_cairo.OPERATOR_CLEAR);ctx.paint();ctx.restore()
        return False

    def _ensure_selection_visible(self,*unused):
        if not self._scroll_pending or not hasattr(self,"_geometry") or not self._requested_size: return False
        cursor=S["cursor"]
        if 0<=cursor<len(self._geometry["cells"]):
            x,y,w,h=self._geometry["cells"][cursor];adjustment=self.scroll.get_vadjustment()
            height=self._requested_size[1]
            if y<adjustment.get_value(): adjustment.set_value(y)
            elif y+min(h,height)>adjustment.get_value()+height:
                adjustment.set_value(max(0,y+min(h,height)-height))
        self._scroll_pending=False
        return False

    def on_draw(self, _w, ctx):
        if hasattr(self, "_geometry"):
            paint_candidates(ctx, self._geometry, S["cursor"], self.palette, self._layout, self._radius)
            if self._preedit_height:
                layout = self._preedit_layout
                ctx.set_source_rgb(*self.palette["text"][:3])
                ctx.move_to(12,2)
                PangoCairo.show_layout(ctx,layout)
            if self._paging_height:
                layout = self._layout(("‹" if S["prev_page"] else " ")+"     "+("›" if S["next_page"] else " "),14)
                ctx.set_source_rgb(*self.palette["text"][:3]);ctx.move_to(12,self._geometry["height"]-24)
                PangoCairo.show_layout(ctx,layout)

    def on_click(self, _widget, event):
        if event.button != 1 or not hasattr(self,"_geometry"): return False
        if self._paging_height and event.y >= self._geometry["height"]-24:
            (agent.LookupTablePageUp if event.x < 36 else agent.LookupTablePageDown)()
            return True
        for i,(x,y,w,h) in enumerate(self._geometry["cells"]):
            if x <= event.x < x+w and y <= event.y < y+h:
                agent.SelectCandidate(i);return True
        return False

    def on_scroll(self, _widget, event):
        if self._geometry["height"]>self._requested_size[1]: return False
        from gi.repository import Gdk
        if event.direction == Gdk.ScrollDirection.UP: agent.LookupTablePageUp()
        elif event.direction == Gdk.ScrollDirection.DOWN: agent.LookupTablePageDown()
        return True


if HAS_LAYER_SHELL and not GtkLayerShell.is_supported():
    HAS_LAYER_SHELL = False
panel = Panel()


def _apply_properties(entries):
    """属性里找当前输入法：路径以 /Fcitx/im 结尾的那条，第二段是输入法显示名。"""
    for e in entries or []:
        parts = [str(p) for p in e] if not isinstance(e, str) else str(e).split(":")
        if len(parts) < 2:
            continue
        if parts[0].endswith("/Fcitx/im"):
            name = OUR_IM if OUR_IM in (parts[1], parts[3] if len(parts) > 3 else "") else parts[1]
            if name != S["im"]:
                S["details"] = None
                panel._composition_origin.reset()
                panel._composition_size.reset()
                S["im"] = name
                if DEBUG: log("当前输入法:", S["im"])


def _handle(name, args):
    if name == "Composition":
        token,active = str(args[0]),bool(args[1])
        if token != S["composition"] or not active:
            panel._composition_origin.reset()
            panel._composition_size.reset()
            panel.scroll.get_vadjustment().set_value(0)
            panel._selected_before=None
        S["composition"],S["composition_active"] = token,active
        return None
    if name == "CandidateDetails":
        S["details"] = tuple([str(value) for value in values] for values in args)
        return None
    if name == "GetPanelState":
        geometry=getattr(panel,"_geometry",{})
        cells=geometry.get("cells",[])
        first_baseline=[geometry["pad"],round(cells[0][1]+geometry["text_tops"][0])+geometry["layouts"][0].get_baseline()/Pango.SCALE] if cells else None
        return json.dumps(dict(first_baseline=first_baseline,origin=panel._composition_origin.point,composition_active=S["composition_active"],size=panel._requested_size,buffer_size=[panel.get_allocated_width(),panel.get_allocated_height()],content_size=[geometry.get("width"),geometry.get("height")],visible_size=panel._visible_size,scroll=[panel.scroll.get_vadjustment().get_value(),panel.scroll.get_vadjustment().get_upper(),panel.scroll.get_vadjustment().get_page_size()],im=S["im"],visible=panel.get_visible(),style=panel.options,
                               candidates=len(S["cands"]),comments=sum(bool(value) for value in S["comments"]),preedit_visible=S["preedit_visible"],
                               preedit_height=getattr(panel,"_preedit_height",0),paging_height=getattr(panel,"_paging_height",0)))
    if name == "ShowLookupTable":
        S["lookup_visible"]=bool(args[0]);panel.queue_refresh();return None
    if name == "Enable":
        S["enabled"]=bool(args[0]);panel.queue_refresh();return None
    if name == "GetAppearance":
        return json.dumps(saved_appearance())
    if name == "SetAppearance":
        import tempfile
        data = validate_appearance(str(args[0]))
        apply_backdrop(option_bool(data["style"],"translucency"))
        path = appearance_path();path.parent.mkdir(parents=True,exist_ok=True)
        with tempfile.NamedTemporaryFile(mode="w",dir=path.parent,delete=False) as output:
            json.dump(data,output);temporary=output.name
        os.replace(temporary,path)
        panel._composition_size.reset()
        panel._selected_before=None
        panel.refresh()
        log("完整外观已应用",data["style"])
        return "applied:1"
    if name == "UpdatePreeditText":
        S["preedit"] = str(args[0]);panel.queue_refresh();return None
    if name == "ShowPreedit":
        S["preedit_visible"] = bool(args[0]);panel.queue_refresh();return None
    if name == "GetPanelIdentity":
        return "bangke-panel:" + PANEL_VERSION
    if name == "SetLayout":
        from pathlib import Path
        import tempfile
        orientation = str(args[0])
        if orientation not in ("horizontal", "vertical"):
            raise ValueError("Invalid candidate layout")
        config = Path(os.environ.get("XDG_CONFIG_HOME", str(Path.home() / ".config"))) / "bangke-panel"
        config.mkdir(parents=True, exist_ok=True)
        with tempfile.NamedTemporaryFile(mode="w", dir=config, delete=False) as output:
            output.write(orientation + "\n")
            temporary = output.name
        os.replace(temporary, config / "layout")
        appearance = saved_appearance()
        if appearance:
            appearance["style"]["candidate_list_layout"] = "stacked" if orientation == "vertical" else "linear"
            _handle("SetAppearance",[json.dumps(appearance)])
        panel.refresh()
        log("布局已应用:", orientation)
        return orientation
    if name == "ReloadConfig":
        panel.refresh()
        return None
    if name == "UpdateLookupTableCursor":
        S["cursor"] = int(args[0])
        panel.queue_refresh()
        return None
    if name in ("SetLookupTable", "UpdateLookupTable", "UpdateLookupTableFast"):
        if len(args) == 2:
            args = [args[0],args[1],[],False,False,S["cursor"],0]
        labels, cands, comments, _p, _n, cursor, _layout = args
        S["labels"] = [str(s) for s in labels]
        S["cands"] = [str(s) for s in cands]
        S["comments"] = []  # The third kimpanel array contains attributes, not comments.
        details = S["details"]
        if S["im"] == OUR_IM and details and S["cands"] == details[0]:
            S["cands"], S["comments"] = details[1], details[2]
        S["cursor"] = int(cursor)
        S["prev_page"], S["next_page"] = bool(_p), bool(_n)
        if DEBUG: log(f"{name}: {len(S['cands'])} 个候选，光标位 {S['cursor']}")
        panel.queue_refresh()
        return dbus.Boolean(True)
    if name == "UpdateSpotLocation":
        # v1 风格：只有 x,y（元素左上角），没有高宽。老前端用它，新版 fcitx5 也可能发。
        x, y = (int(a) for a in args)
        if DEBUG: log(f"UpdateSpotLocation: {x},{y}")
        if x or y:
            _, _, w, h = S["spot"]
            S["spot"] = (x, y, w, h or 24)
            panel.queue_refresh()
        return None
    if name in ("SetRelativeSpotRect", "SetRelativeSpotRectV2"):
        # 当**绝对坐标**收下：上游这三个方法发的都是同一份 cursorRect 数值，
        # 只有 V2 多带一个缩放（双精度），那是把物理像素换回逻辑像素的依据。
        x, y, w, h = (int(a) for a in args[:4])
        S["spot_scale"] = float(args[4]) if len(args) > 4 else 0.0
        if DEBUG: log(f"{name}: {x},{y} {w}x{h} 缩放 {S['spot_scale'] or '-'} ← {focused_window()[2]}")
        if not (x or y) and (S["spot"][0] or S["spot"][1]):
            if DEBUG: log("  （全零，保留上一次的光标）")
            return None
        S["spot"] = (x, y, w, h)
        S["spot_win"] = focused_window()[0]      # 记下这条光标是哪个窗口的
        panel.queue_refresh()
        return None
    if name == "SetSpotRect":
        x, y, w, h = (int(a) for a in args)
        if DEBUG: log(f"SetSpotRect: {x},{y} {w}x{h} ← {focused_window()[2]}")
        # 失焦时 fcitx5 会推一条全零 —— 别拿它把刚才拿到的光标冲掉
        if not (x or y) and (S["spot"][0] or S["spot"][1]):
            if DEBUG: log("  （全零，保留上一次的光标）")
            return None
        S["spot"] = (x, y, w, h)
        S["spot_win"] = focused_window()[0]
        panel.queue_refresh()
        return None
    if name == "RegisterProperties":
        _apply_properties(args[0]);panel.queue_refresh()
        return None
    if name == "UpdateProperty":
        _apply_properties([args[0]] if isinstance(args[0],str) else args[0]);panel.queue_refresh()
        return None
    return None


def _arg_count(sig):
    n = depth = 0
    for ch in sig:
        if ch == "a":
            continue
        if ch == "(":
            if depth == 0:
                n += 1
            depth += 1
        elif ch == ")":
            depth -= 1
        elif depth == 0 and ch in "ybnqiuxtdsogh":
            n += 1
    return n


def _noop(self):
    pass


def panel_class():
    ns = {"_handle": staticmethod(_handle)}
    meta = type(dbus.service.Object)
    for mname, (sig_in, sig_out) in METHODS.items():
        cnt = _arg_count(sig_in)
        params = ", ".join(f"a{i}" for i in range(cnt))
        call = ", ".join(f"a{i}" for i in range(cnt))
        tmp = {}
        exec(
            f"def {mname}(self{', ' + params if params else ''}):\n"
            f"    return self._handle({mname!r}, [{call}])\n",
            tmp,
        )
        ns[mname] = dbus.service.method(IFACE, in_signature=sig_in, out_signature=sig_out)(tmp[mname])
    return meta("KimpanelAgent", (dbus.service.Object,), ns)


class PrivateAgent:
    def __init__(self, bus):
        self.bus = bus

    def _call(self, method, *args):
        message = dbus.lowlevel.MethodCallMessage("org.fcitx.Fcitx5", "/bangke/panel", IFACE, method)
        message.append(*args, signature="i" if method == "SelectCandidate" else "b" if method == "Page" else "")
        self.bus.send_message(message)

    def SelectCandidate(self, index): self._call("SelectCandidate", index)
    def LookupTablePageUp(self): self._call("Page", True)
    def LookupTablePageDown(self): self._call("Page", False)


def main():
    global agent
    dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
    try:
        bus = dbus.SessionBus()
    except dbus.exceptions.DBusException as e:
        log("!! 连不上会话总线，退出:", e)
        return 1
    log("总线:", os.environ.get("DBUS_SESSION_BUS_ADDRESS") or "(没有！会另起私有总线，fcitx5 看不到)")
    name = dbus.service.BusName(SERVICE, bus, do_not_queue=True)
    exported = panel_class()(bus_name=name, object_path=PATH)
    agent = PrivateAgent(bus)
    bus.add_signal_receiver(lambda *args: _handle("Composition",list(args)),
                            dbus_interface="org.bangke.Candidate1",signal_name="Composition",
                            bus_name="org.fcitx.Fcitx5",path="/bangke")
    bus.add_signal_receiver(lambda *args: _handle("CandidateDetails",list(args)),
                            dbus_interface="org.bangke.Candidate1",signal_name="Candidates",
                            bus_name="org.fcitx.Fcitx5",path="/bangke")
    log("已占住", SERVICE, "| layer-shell:", "开" if HAS_LAYER_SHELL else "关（降级为普通窗口）")
    log("面板版本:", PANEL_VERSION)
    def backend_changed(_name, _old, owner):
        S["lookup_visible"] = False
        S["preedit_visible"] = False
        panel.queue_refresh()
        if owner: agent._call("Refresh")
    bus.add_signal_receiver(backend_changed, signal_name="NameOwnerChanged",
                            dbus_interface="org.freedesktop.DBus",arg0="org.fcitx.Fcitx5")
    if bus.name_has_owner("org.fcitx.Fcitx5"): agent._call("Refresh")
    Gtk.main()


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(0)
