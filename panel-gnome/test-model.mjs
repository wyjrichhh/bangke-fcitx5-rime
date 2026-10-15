import assert from 'node:assert/strict';
import {test} from 'node:test';
import {CandidateState, OUR_IM, boolOption, candidateParts, formatCandidate, placePanel,
  validateAppearance, interfaceXml} from './bangke-panel@bangke.org/model.js';
const palette = Object.fromEntries(['back', 'text', 'accent', 'on_accent', 'muted', 'border'].map(k => [k, [0, .5, 1, 1]]));
const appearance = () => ({version: 1, style: {alpha: '0.6', font_point: '14'}, light: palette, dark: palette});
test('Qt string style values validate; unsafe colors and ranges fail', () => {
  assert.equal(validateAppearance(JSON.stringify(appearance())).style.alpha, '0.6');
  for (const value of ['NaN', 'Infinity', -1, 1.1])
    assert.throws(() => validateAppearance({...appearance(), style: {alpha: value}}));
  assert.throws(() => validateAppearance({...appearance(), dark: {...palette, back: [0, 1, 2, 1]}}));
  assert.throws(() => validateAppearance({...appearance(), style: {candidate_list_layout: 'diagonal'}}));
  assert.equal(boolOption({translucency: 'false'}, 'translucency'), false);
  assert.equal(boolOption({translucency: 'true'}, 'translucency'), true);
});
test('AI table updates preserve composition origin; commit and new composition reset it', () => {
  const s = new CandidateState();
  s.composition('session:1', true); s.origin = {x: 10, y: 20}; s.height = 120;
  s.table(['1'], ['first'], false, true, 0);
  s.composition('session:1', true); s.table(['1'], ['longer AI answer'], false, true, 0);
  assert.deepEqual(s.origin, {x: 10, y: 20}); assert.equal(s.height, 120);
  s.composition('session:1', false); assert.equal(s.origin, null); assert.equal(s.height, 0);
  s.origin = {}; s.composition('session:2', true); assert.equal(s.origin, null);
});
test('separate comments apply only to matching Bangke candidate tables', () => {
  const s = new CandidateState();
  s.properties([`/Fcitx/im:${OUR_IM}:icon:description`]);
  s.details = [['你好 注释'], ['你好'], ['注释']];
  s.table(['1'], ['你好 注释'], false, false, 0);
  assert.deepEqual(s.texts, ['你好']); assert.deepEqual(s.comments, ['注释']);
  s.table(['1'], ['other'], false, false, 0); assert.deepEqual(s.comments, []);
  s.properties(['/Fcitx/im:Other:icon:description']);
  assert.equal(s.details, null);
});
test('candidate substitutions are literal and support long Unicode content', () => {
  assert.deepEqual(candidateParts('[unknown]', '1', 'text', ''), [{role: 'literal', text: '[unknown]'}]);
  const text = '长文本[comment]'.repeat(1000);
  assert.equal(formatCandidate('[label]. [candidate] [comment]', '1', text, 'AI'), `1. ${text} AI`);
});
test('placement handles bottom edge, negative monitor origins and oversized panels', () => {
  const area = {x: -1920, y: 0, width: 1920, height: 1080};
  assert.deepEqual(placePanel({x: -10, y: 1000, height: 20}, {width: 400, height: 200}, area), [-400, 795]);
  assert.deepEqual(placePanel({x: -1800, y: 20, height: 20}, {width: 300, height: 100}, area), [-1800, 45]);
  assert.deepEqual(placePanel({x: 0, y: 0, height: 20}, {width: 3000, height: 2000}, area), [-1920, 0]);
});
test('Kimpanel introspection preserves V2 scaling and boolean table response', () => {
  const xml = interfaceXml();
  assert.match(xml, /SetRelativeSpotRectV2.*direction="in" type="d"/);
  assert.match(xml, /SetLookupTable.*direction="out" type="b"/);
});

test('role formatting preserves user text and separates label/comment colors', () => {
  assert.deepEqual(candidateParts('[label]. [candidate] [comment]', '1', '<b>[comment]</b>', 'AI'), [
    {role: 'literal', text: ''}, {role: 'label', text: '1'}, {role: 'literal', text: '. '},
    {role: 'candidate', text: '<b>[comment]</b>'}, {role: 'literal', text: ' '},
    {role: 'comment', text: 'AI'}, {role: 'literal', text: ''},
  ]);
});
