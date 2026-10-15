import Clutter from 'gi://Clutter';
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import Mtk from 'gi://Mtk';
import Pango from 'gi://Pango';
import Shell from 'gi://Shell';
import St from 'gi://St';
import * as Main from 'resource:///org/gnome/shell/ui/main.js';
import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';
import {RoundedClip, BackgroundCutout} from './rounded.js';
import {CandidateState, IDENTITY, OUR_IM, boolOption, candidateParts, interfaceXml,
  placePanel, validateAppearance} from './model.js';

const PATH = '/org/kde/impanel';
const SIGNALS = `<node><interface name="org.kde.impanel">
  <signal name="PanelCreated"/><signal name="SelectCandidate"><arg type="i"/></signal>
  <signal name="LookupTablePageUp"/><signal name="LookupTablePageDown"/>
</interface></node>`;
const HELPER = `<node><interface name="org.fcitx.GnomeHelper"><method name="LockXkbGroup">
  <arg direction="in" type="i"/></method></interface></node>`;
const rgba = (color, alpha = 1) => `rgba(${color.slice(0, 3).map(v => Math.round(v * 255)).join(',')},${color[3] * alpha})`;
const fallback = {back: [.13, .13, .15, 1], text: [.92, .92, .92, 1],
  accent: [1, .49, .24, 1], on_accent: [1, 1, 1, 1], muted: [.62, .55, .5, 1], border: [0, 0, 0, 0]};

export default class BangkePanel extends Extension {
  enable() {
    this._state = new CandidateState();
    this._owned = false;
    this._subscriptions = [];
    this._connections = [];
    this._appearance = null;
    this._rect = {x: 0, y: 0, width: 1, height: 20};
    this._file = Gio.File.new_for_path(GLib.build_filenamev([GLib.get_user_config_dir(), 'bangke-panel', 'appearance.json']));
    GLib.mkdir_with_parents(this._file.get_parent().get_path(), 0o700);
    this._actor = new St.Widget({layout_manager: new Clutter.BinLayout(), visible: false});
    // Blur only the backdrop; text and borders are separate actors and stay sharp.
    this._backdrop = new St.Widget({width: 0, height: 0, x_expand: true, y_expand: true,
      x_align: Clutter.ActorAlign.FILL, y_align: Clutter.ActorAlign.FILL, clip_to_allocation: true});
    // Clone the compositor's window group, which excludes Shell chrome and our
    // panel. ACTOR blur then composes correctly inside a rounded offscreen mask;
    // BACKGROUND blur would sample the mask's empty framebuffer instead.
    this._scene = new Clutter.Actor();
    this._sceneSources = new Map();
    this._backdrop.add_child(this._scene);
    this._refreshClones();
    this._connections.push([global.window_group, global.window_group.connect('child-added', () => this._refreshClones())]);
    this._connections.push([global.window_group, global.window_group.connect('child-removed', () => this._refreshClones())]);
    const backgrounds = Main.layoutManager._backgroundGroup;
    if (backgrounds) {
      this._connections.push([backgrounds, backgrounds.connect('child-added', () => this._refreshClones())]);
      this._connections.push([backgrounds, backgrounds.connect('child-removed', () => this._refreshClones())]);
    }
    this._blur = new Shell.BlurEffect({mode: Shell.BlurMode.ACTOR, radius: 24, brightness: 1});
    this._backdrop.add_effect_with_name('bangke-blur', this._blur);
    this._fill = new St.Widget({x_expand: true, y_expand: true});
    this._fillClip = new BackgroundCutout();
    this._fill.add_effect_with_name('bangke-exclusive-fill', this._fillClip);
    this._outline = new St.Widget({x_expand: true, y_expand: true, reactive: false});
    this._content = new St.BoxLayout({vertical: true, reactive: true});
    this._actor.add_child(this._backdrop);
    this._actor.add_child(this._fill);
    this._actor.add_child(this._content);
    this._actor.add_child(this._outline);
    this._clip = new RoundedClip();
    this._actor.add_effect_with_name('bangke-rounded', this._clip);
    Main.layoutManager.addChrome(this._actor, {affectsInputRegion: true});
    this._actor.connect('notify::allocation', () => this._position());
    this._connections.push([global.display, global.display.connect('notify::focus-window', () => {
      this._refreshClones();
      this._state.origin = null; this._state.width = 0; this._state.height = 0;
      this._state.lookupVisible = false;
      this._state.preeditVisible = false;
      this._queue();
    })]);
    this._connections.push([Main.overview, Main.overview.connect('showing', () => this._actor.hide())]);
    this._connections.push([Main.layoutManager, Main.layoutManager.connect('monitors-changed', () => {
      this._refreshClones();
      this._state.origin = null; this._state.width = 0; this._state.height = 0; this._queue();
    })]);
    this._settings = new Gio.Settings({schema_id: 'org.gnome.desktop.interface'});
    this._settingsSignal = this._settings.connect('changed::color-scheme', () => this._queue());
    this._connections.push([Main.sessionMode, Main.sessionMode.connect('updated', () => this._queue())]);
    this._monitor = this._file.get_parent().monitor_directory(Gio.FileMonitorFlags.NONE, null);
    this._monitor.connect('changed', () => this._reload());
    this._reload();
    this._v1 = Gio.DBusExportedObject.wrapJSObject(SIGNALS, this);
    this._v2 = Gio.DBusExportedObject.wrapJSObject(interfaceXml(), this);
    this._helper = Gio.DBusExportedObject.wrapJSObject(HELPER, this);
    // Do not replace another Kimpanel. The installer detects conflicts too.
    this._owner = Gio.bus_own_name(Gio.BusType.SESSION, 'org.kde.impanel', Gio.BusNameOwnerFlags.NONE,
      null, () => this._acquired(), () => this._lost());
    this._watch = Gio.bus_watch_name(Gio.BusType.SESSION, 'org.fcitx.Fcitx5', Gio.BusNameWatcherFlags.NONE,
      (_bus, _name, owner) => { this._sender = owner; this._announce(); },
      () => { this._sender = null; this._state.reset(); this._queue(); });
    for (const iface of ['org.kde.kimpanel.inputmethod', 'org.bangke.Candidate1']) {
      this._subscriptions.push(Gio.DBus.session.signal_subscribe(null, iface, null, null, null,
        Gio.DBusSignalFlags.NONE, (_bus, sender, _path, source, signal, args) => {
          if (sender !== this._sender || !this._owned) return;
          this._signal(source, signal, args.deep_unpack());
        }));
    }
  }

  _refreshClones() {
    const background = Main.layoutManager._backgroundGroup;
    const sources = [...(background?.get_children() ?? []),
      ...global.window_group.get_children().filter(actor => actor !== background)];
    for (const [source, entry] of this._sceneSources) {
      if (sources.includes(source)) continue;
      for (const id of entry.signals) source.disconnect(id);
      entry.clone.destroy(); this._sceneSources.delete(source);
    }
    for (const source of sources) {
      if (!this._sceneSources.has(source)) {
        const clone = new Clutter.Clone({source, reactive: false});
        const signals = [source.connect('notify::allocation', () => this._layoutClones()),
          source.connect('notify::visible', () => this._layoutClones()),
          source.connect('destroy', () => {
            this._sceneSources.delete(source);
            clone.destroy();
          })];
        this._sceneSources.set(source, {clone, signals});
        this._scene.add_child(clone);
      }
      this._scene.set_child_above_sibling(this._sceneSources.get(source).clone, null);
    }
    this._layoutClones();
  }
  _layoutClones() {
    for (const [source, {clone}] of this._sceneSources) {
      clone.set_position(...source.get_transformed_position());
      clone.set_size(source.width, source.height);
      clone.visible = source.visible;
    }
  }
  _acquired() {
    if (!this._actor) return;
    this._owned = true;
    this._v1.export(Gio.DBus.session, PATH);
    this._v2.export(Gio.DBus.session, PATH);
    this._helper.export(Gio.DBus.session, '/org/fcitx/GnomeHelper');
    this._helperOwner = Gio.bus_own_name(Gio.BusType.SESSION, 'org.fcitx.GnomeHelper', Gio.BusNameOwnerFlags.NONE,
      null, null, null);
    this._announce();
  }
  _lost() {
    if (!this._actor) return;
    this._owned = false;
    this._v1.unexport(); this._v2.unexport(); this._helper.unexport();
    this._actor.hide();
    if (this._helperOwner) { Gio.bus_unown_name(this._helperOwner); this._helperOwner = 0; }
    console.warn('Bangke: Kimpanel bus name unavailable; keeping existing panel');
  }
  _announce() {
    if (!this._owned) return;
    this._v1.emit_signal('PanelCreated', null);
    this._v2.emit_signal('PanelCreated2', null);
  }
  _emit(name, index) {
    this._v1.emit_signal(name, index === undefined ? null : new GLib.Variant('(i)', [index]));
  }
  _signal(iface, signal, args) {
    const s = this._state;
    if (iface === 'org.bangke.Candidate1') {
      if (signal === 'Candidates') s.details = args;
      if (signal === 'Composition') s.composition(...args);
    } else {
      switch (signal) {
      case 'RegisterProperties': s.properties(args[0]); break;
      case 'UpdateProperty': s.properties([args[0]]); break;
      case 'UpdatePreeditText': s.preedit = args[0]; break;
      case 'ShowPreedit': s.preeditVisible = args[0]; break;
      case 'ShowLookupTable': s.lookupVisible = args[0]; break;
      case 'Enable': s.enabled = args[0]; break;
      case 'UpdateLookupTableCursor': s.cursor = args[0]; break;
      case 'UpdateLookupTable': s.table(args[0], args[1], false, false, s.cursor); break;
      case 'UpdateSpotLocation': this._spot(args[0], args[1], 1, 0, false, 1); break;
      }
    }
    this._queue();
  }
  _queue() {
    if (this._idle) return;
    this._idle = GLib.idle_add(GLib.PRIORITY_DEFAULT_IDLE, () => {
      this._idle = 0;
      this._render();
      return GLib.SOURCE_REMOVE;
    });
  }
  _reload() {
    try {
      const [ok, bytes] = this._file.load_contents(null);
      if (ok) {
        const data = validateAppearance(new TextDecoder().decode(bytes));
        this._appearance = data;
      }
    } catch (error) {
      if (!error.matches?.(Gio.IOErrorEnum, Gio.IOErrorEnum.NOT_FOUND)) console.warn(`Bangke appearance: ${error.message}`);
    }
    this._queue();
  }
  _save(data) {
    this._file.replace_contents(JSON.stringify(data), null, false, Gio.FileCreateFlags.PRIVATE, null);
    this._appearance = data;
    this._state.height = 0;
    this._queue();
  }
  GetPanelIdentity() { return IDENTITY; }
  GetAppearance() { return JSON.stringify(this._appearance); }
  SetAppearance(raw) {
    const data = validateAppearance(raw);
    this._save(data);
    return 'applied:1';
  }
  SetLayout(layout) {
    if (!['horizontal', 'vertical'].includes(layout)) throw new Error('Invalid layout');
    if (!this._appearance) throw new Error('Save appearance first');
    const data = JSON.parse(JSON.stringify(this._appearance));
    data.style.candidate_list_layout = layout === 'vertical' ? 'stacked' : 'linear';
    this._save(data);
    return layout;
  }
  GetPanelState() {
    return JSON.stringify({backend: 'gnome', visible: this._actor.visible, im: this._state.im,
      cells: this._scroll?.get_child()?.get_children().map(button => ({
        x: button.x, y: button.y, width: button.width, height: button.height,
        label: button.get_child()?.get_transformed_position()})) ?? [],
      candidates: this._state.texts.length, composition_active: this._state.active,
      origin: this._state.origin, locked_height: this._state.height, geometry: {x: this._actor.x, y: this._actor.y, width: this._actor.width, height: this._actor.height, backdrop: [this._backdrop.width, this._backdrop.height], clones: this._sceneSources.size}, style: this._appearance?.style ?? {}, blur: this._blur.enabled});
  }
  ReloadConfig() { this._reload(); }
  LockXkbGroup(index) { global.backend.lock_layout_group(index); }
  SetLookupTable(labels, texts, _attrs, previous, next, cursor, _layout) {
    this._state.table(labels, texts, previous, next, cursor);
    this._queue();
    return true;
  }
  SetSpotRect(x, y, w, h) { this._spot(x, y, w, h, false, 1); }
  SetRelativeSpotRect(x, y, w, h) { this._spot(x, y, w, h, true, 1); }
  SetRelativeSpotRectV2(x, y, w, h, scale) { this._spot(x, y, w, h, true, scale); }
  _spot(x, y, width, height, relative, scale) {
    if (!Number.isFinite(scale) || scale <= 0) return;
    const win = global.display.focus_window;
    const factor = relative ? St.ThemeContext.get_for_stage(global.stage).scale_factor / scale : 1;
    let rect = new Mtk.Rectangle({x: Math.round(x * factor), y: Math.round(y * factor),
      width: Math.round(width * factor), height: Math.round(height * factor)});
    // GNOME 46 lacks protocol_to_stage_rect on some builds; use surface-local
    // coordinates plus the compositor actor origin there (not the decorated frame).
    if (win?.protocol_to_stage_rect) rect = win.protocol_to_stage_rect(rect);
    if (relative && win) {
      const actor = win.get_compositor_private();
      if (actor) { rect.x += actor.x; rect.y += actor.y; }
    }
    if (!rect.height) { rect.y -= 20; rect.height = 20; }
    this._rect = rect;
    this._queue();
  }

  _label(text, color) {
    const label = new St.Label({text, x_expand: true, style: `color: ${color};`});
    label.clutter_text.ellipsize = Pango.EllipsizeMode.NONE;
    label.clutter_text.line_wrap = true;
    label.clutter_text.line_wrap_mode = Pango.WrapMode.WORD_CHAR;
    return label;
  }
  _render() {
    const s = this._state;
    if (!s) return;
    if (!this._owned || !s.enabled || Main.overview.visible || Main.sessionMode.isLocked ||
        !(s.lookupVisible && s.texts.length || s.preeditVisible && s.preedit)) {
      this._actor.hide(); return;
    }
    const own = s.im === OUR_IM;
    const style = own ? this._appearance?.style ?? {} : {};
    const dark = this._settings.get_string('color-scheme') === 'prefer-dark';
    const colors = own ? this._appearance?.[dark ? 'dark' : 'light'] ?? fallback : fallback;
    const alpha = Number(style.alpha ?? 1);
    const radius = Math.round(Number(style.corner_radius ?? 8) * 4 / 3);
    this._radius = radius;
    const border = Math.max(0, Math.min(Number(style.border_width ?? 0), Number(style.border_height ?? 0))) * 4 / 3;
    this._fill.set_style(`background-color: ${rgba(colors.back, alpha)}; border-radius: ${radius}px;`);
    this._blur.enabled = own && boolOption(style, 'translucency');
    this._backdrop.visible = this._blur.enabled;
    this._exclusive = boolOption(style, 'mutual_exclusive');
    this._itemRadius = 0;
    const font = String(style.font_face || 'sans-serif').replace(/[;{}"'\\]/g, '');
    this._outline.set_style(`border: ${border}px solid ${rgba(colors.border)}; border-radius: ${radius}px;`);
    this._content.set_style(`font-family: "${font}"; font-size: ${Number(style.font_point ?? 14)}pt;
      padding: 0; spacing: 0;`);
    const scrollValue = this._scrollToken === s.token && this._scroll ?
      [this._scroll.get_hadjustment().value, this._scroll.get_vadjustment().value] : [0, 0];
    this._scroll = null;
    this._selectedButton = null;
    this._content.destroy_all_children();
    this._content.set_width(-1);
    this._content.set_height(-1);
    if (s.preeditVisible && s.preedit && !boolOption(style, 'inline_preedit', true)) {
      const preedit = this._label(s.preedit, rgba(colors.preedit ?? colors.text));
      preedit.set_style(`color: ${rgba(colors.preedit ?? colors.text)}; padding: 4px 8px;
        margin-bottom: ${Math.max(0, Number(style.spacing ?? 3)) * 4 / 3}px;`);
      this._content.add_child(preedit);
    }
    if (s.lookupVisible && s.texts.length) {
      const scroll = new St.ScrollView({hscrollbar_policy: St.PolicyType.AUTOMATIC,
        vscrollbar_policy: St.PolicyType.AUTOMATIC, overlay_scrollbars: true});
      const stacked = style.candidate_list_layout === 'stacked';
      const list = new St.BoxLayout({vertical: stacked, y_expand: true, x_expand: true});
      scroll.set_child(list);
      this._scroll = scroll;
      scroll.get_hadjustment().connect('notify::value', () => this._position());
      scroll.get_vadjustment().connect('notify::value', () => this._position());
      this._scrollToken = s.token;
      scroll.connect('notify::allocation', () => {
        for (const [adjustment, value] of [[scroll.get_hadjustment(), scrollValue[0]], [scroll.get_vadjustment(), scrollValue[1]]])
          adjustment.value = Math.max(adjustment.lower, Math.min(value, adjustment.upper - adjustment.page_size));
      });
      s.texts.forEach((text, index) => {
        const selected = index === s.cursor;
        // Keep labels adjacent while preserving a stable outer panel size.
        // Only the final cell absorbs spare space, never every candidate.
        const last = index === s.texts.length - 1;
        const button = new St.Button({can_focus: false, button_mask: St.ButtonMask.ONE,
          x_expand: stacked || last, y_expand: !stacked || last});
        if (selected) {
          this._selectedButton = button;
          button.connect('notify::allocation', () => this._position());
        }
        const selectedColor = colors.on_accent;
        const fill = selected ? rgba(colors.accent, alpha) : 'transparent';
        const shadow = selected ? Math.max(0, Number(style.shadow_size ?? 0)) * 4 / 3 : 0;
        const itemRadius = 0;
        button.set_style(`background-color: ${fill}; border-radius: ${itemRadius}px;
          padding: ${Math.max(2, 4 + Number(style.line_spacing ?? 0) / 2)}px 8px;
          box-shadow: ${shadow > 0 ? `0 0 ${shadow}px ${rgba(colors.accent, alpha)}` : 'none'};`);
        const label = this._label('', rgba(selected ? selectedColor : colors.text));
        label.y_align = stacked ? Clutter.ActorAlign.START : Clutter.ActorAlign.CENTER;
        const roleColors = {label: selected ? colors.selected_label ?? selectedColor : colors.muted,
          candidate: selected ? selectedColor : colors.text,
          comment: selected ? colors.selected_comment ?? selectedColor : colors.comment ?? colors.muted,
          literal: selected ? selectedColor : colors.text};
        const markup = candidateParts(style.candidate_format, s.labels[index], text, s.comments[index] ?? '')
          .map(part => {
            const color = '#' + roleColors[part.role].slice(0, 3).map(v => Math.round(v * 255).toString(16).padStart(2, '0')).join('');
            return `<span foreground="${color}">${GLib.markup_escape_text(style.text_orientation === 'vertical' ? Array.from(part.text).join('\n') : part.text, -1)}</span>`;
          }).join('');
        label.clutter_text.set_markup(markup);
        button.set_child(label);
        button.connect('clicked', () => this._emit('SelectCandidate', index));
        list.add_child(button);
      });
      const area = this._workArea();
      scroll.set_style(`max-width: ${Math.max(80, area.width - 32)}px; max-height: ${Math.max(60, area.height - 64)}px;`);
      this._content.add_child(scroll);
      if (boolOption(style, 'show_paging') && (s.previous || s.next)) {
        const paging = new St.BoxLayout();
        for (const [text, available, signal] of [['‹', s.previous, 'LookupTablePageUp'], ['›', s.next, 'LookupTablePageDown']]) {
          const button = new St.Button({label: text, reactive: available, can_focus: false});
          button.connect('clicked', () => this._emit(signal)); paging.add_child(button);
        }
        this._content.add_child(paging);
      }
    }
    this._memorize = own && boolOption(style, 'memorize_size');
    if (!this._content.get_n_children()) { this._actor.hide(); return; }
    if (this._scroll && own && s.active && !s.height) {
      const width = Math.min(this._content.get_preferred_width(-1)[1], this._workArea().width - 32);
      s.height = Math.min(this._content.get_preferred_height(width)[1], this._workArea().height - 64);
    }
    this._actor.show();
    this._actor.queue_redraw();
    this._position();
  }
  _workArea() {
    const index = global.display.get_monitor_index_for_rect(this._rect);
    return Main.layoutManager.getWorkAreaForMonitor(index >= 0 ? index : Main.layoutManager.primaryIndex);
  }
  _position() {
    if (!this._actor?.visible) return;
    const s = this._state;
    if (!s.origin || !s.active || s.im !== OUR_IM) s.origin = {x: this._rect.x, y: this._rect.y, width: this._rect.width, height: this._rect.height};
    const scale = St.ThemeContext.get_for_stage(global.stage).scale_factor;
    if (this._memorize && s.active) {
      const natural = this._content.get_preferred_width(-1)[1];
      const width = Math.min(Math.max(s.width, natural), Math.max(80, this._workArea().width - 16));
      if (width !== s.width) s.width = width;
      if (this._content.width !== width) this._content.set_width(width);
    }
    // Lock the visible height at the first candidate table of this composition.
    // Candidate refreshes change the scrollable contents, never the panel outline.
    if (this._scroll && s.im === OUR_IM && s.active && s.height > 0) {
      const extras = this._content.get_children().filter(child => child !== this._scroll)
        .reduce((height, child) => height + child.get_preferred_height(this._actor.width)[1], 0);
      const viewport = Math.max(1, s.height - extras);
      if (this._scroll.height !== viewport) this._scroll.set_height(viewport);
      if (this._content.height !== s.height) this._content.set_height(s.height);
    }
    this._clip.update(this._actor.width, this._actor.height, (this._radius ?? 0) * scale);
    this._fillClip.update(this._actor.width, this._actor.height, (this._radius ?? 0) * scale);
    let hole = null;
    if (this._exclusive && this._selectedButton) {
      const [x, y] = this._selectedButton.get_transformed_position();
      const [ax, ay] = this._actor.get_transformed_position();
      const [w, h] = this._selectedButton.get_transformed_size();
      hole = [(x - ax) * scale, (y - ay) * scale, w * scale, h * scale];
    }
    this._fillClip.cut(hole, this._itemRadius * scale);
    const point = placePanel(s.origin, {width: this._actor.width, height: this._actor.height}, this._workArea());
    this._actor.set_position(...point);
    this._scene.set_position(-point[0], -point[1]);
    this._layoutClones();
  }
  disable() {
    if (this._idle) { GLib.source_remove(this._idle); this._idle = 0; }
    this._monitor?.cancel();
    this._settings?.disconnect(this._settingsSignal);
    for (const [object, id] of this._connections ?? []) object.disconnect(id);
    for (const id of this._subscriptions ?? []) Gio.DBus.session.signal_unsubscribe(id);
    if (this._watch) Gio.bus_unwatch_name(this._watch);
    if (this._owner) Gio.bus_unown_name(this._owner);
    if (this._helperOwner) Gio.bus_unown_name(this._helperOwner);
    this._v1?.unexport(); this._v2?.unexport(); this._helper?.unexport();
    for (const [source, entry] of this._sceneSources ?? [])
      for (const id of entry.signals) source.disconnect(id);
    this._sceneSources?.clear();
    this._actor?.destroy();
    this._actor = null; this._state = null; this._settings = null;
    this._owner = 0; this._watch = 0; this._helperOwner = 0;
  }
}
