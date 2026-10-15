// No Shell imports: protocol and geometry can be regression-tested outside GNOME.
export const IDENTITY = 'bangke-panel:gnome-1';
export const OUR_IM = '蚌壳拼音';
export const METHODS = {
  GetPanelIdentity: ['', 's'], GetAppearance: ['', 's'], SetAppearance: ['s', 's'],
  GetPanelState: ['', 's'], SetLayout: ['s', 's'], ReloadConfig: ['', ''],
  SetSpotRect: ['iiii', ''], SetRelativeSpotRect: ['iiii', ''],
  SetRelativeSpotRectV2: ['iiiid', ''], SetLookupTable: ['asasasbbii', 'b'],
};
export function interfaceXml() {
  return `<node><interface name="org.kde.impanel2">${Object.entries(METHODS).map(([name, [input, output]]) => {
    const types = input.match(/as|[ibds]/g) ?? [];
    return `<method name="${name}">${types.map(type => `<arg direction="in" type="${type}"/>`).join('')}${output ? `<arg direction="out" type="${output}"/>` : ''}</method>`;
  }).join('')}<signal name="PanelCreated2"/></interface></node>`;
}
export function validateAppearance(raw) {
  const data = typeof raw === 'string' ? JSON.parse(raw) : raw;
  if (data?.version !== 1 || !data.style || Array.isArray(data.style) || typeof data.style !== 'object')
    throw new Error('Unsupported appearance version');
  for (const mode of ['light', 'dark']) {
    for (const key of ['back', 'text', 'accent', 'on_accent', 'muted', 'border', 'preedit', 'selected_label', 'comment', 'selected_comment']) {
      if (['preedit', 'selected_label', 'comment', 'selected_comment'].includes(key) && !(key in (data[mode] ?? {}))) continue;
      const color = data[mode]?.[key];
      if (!Array.isArray(color) || color.length !== 4 || color.some(v => !Number.isFinite(v) || v < 0 || v > 1))
        throw new Error(`Invalid palette: ${mode}.${key}`);
    }
  }
  for (const [key, low, high] of [['font_point', 8, 48], ['corner_radius', 0, 30],
    ['hilited_corner_radius', 0, 30], ['alpha', 0, 1], ['spacing', -10, 30],
    ['line_spacing', -10, 30], ['shadow_size', 0, 30], ['border_width', -10, 20], ['border_height', -10, 20]]) {
    if (key in data.style && (!Number.isFinite(Number(data.style[key])) || Number(data.style[key]) < low || Number(data.style[key]) > high))
      throw new Error(`Invalid option: ${key}`);
  }
  if (!['horizontal', 'vertical'].includes(data.style.text_orientation ?? 'horizontal') ||
      !['linear', 'stacked'].includes(data.style.candidate_list_layout ?? 'linear'))
    throw new Error('Invalid layout');
  for (const key of ['candidate_format', 'font_face'])
    if (key in data.style && typeof data.style[key] !== 'string') throw new Error(`Invalid option: ${key}`);
  return data;
}
export function boolOption(style, key, fallback = false) {
  return key in style ? style[key] === true || style[key] === 'true' : fallback;
}
export function candidateParts(template, label, text, comment) {
  return (template || '[label] [candidate] [comment]').split(/(\[(?:label|candidate|comment)\])/g).map(part => {
    const role = /^\[(label|candidate|comment)\]$/.exec(part)?.[1] ?? 'literal';
    return {role, text: role === 'literal' ? part : ({label, candidate: text, comment})[role] ?? ''};
  });
}
export function formatCandidate(template, label, text, comment) {
  return (template || '[label] [candidate] [comment]').replace(/\[(label|candidate|comment)\]/g,
    (_, role) => ({label, candidate: text, comment})[role] ?? '').trim();
}
export function placePanel(rect, size, area) {
  const x = Math.max(area.x, Math.min(rect.x, area.x + area.width - size.width));
  const below = rect.y + rect.height + 5;
  const y = below + size.height <= area.y + area.height ? below : rect.y - size.height - 5;
  return [Math.round(x), Math.round(Math.max(area.y, Math.min(y, area.y + area.height - size.height)))];
}
export class CandidateState {
  constructor() { this.reset(); }
  reset() {
    this.im = ''; this.labels = []; this.texts = []; this.comments = []; this.details = null;
    this.cursor = 0; this.lookupVisible = false; this.preeditVisible = false;
    this.preedit = ''; this.enabled = true; this.previous = false; this.next = false;
    this.token = ''; this.active = false; this.origin = null; this.width = 0; this.height = 0;
  }
  properties(entries) {
    for (const entry of entries) {
      const fields = typeof entry === 'string' ? entry.split(':') : entry;
      if (fields[0]?.endsWith('/Fcitx/im')) {
        const im = fields.includes(OUR_IM) ? OUR_IM : fields[1];
        if (im !== this.im) { this.details = null; this.origin = null; this.width = 0; this.height = 0; }
        this.im = im;
      }
    }
  }
  composition(token, active) {
    if (token !== this.token || !active) { this.origin = null; this.width = 0; this.height = 0; }
    this.token = token; this.active = active;
  }
  table(labels, texts, previous, next, cursor) {
    this.labels = labels; this.texts = texts; this.comments = [];
    // Never mistake the kimpanel attribute array for candidate comments.
    if (this.im === OUR_IM && this.details && JSON.stringify(texts) === JSON.stringify(this.details[0]))
      [, this.texts, this.comments] = this.details;
    this.previous = previous; this.next = next; this.cursor = cursor;
  }
}
