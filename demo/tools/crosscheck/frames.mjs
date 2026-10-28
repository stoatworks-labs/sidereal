// The JavaScript half of the browser demo's FRAME cross-check: demo/plugin.js
// from its BEGIN PORT marker to its END FRAME marker (sliced out as text and
// run here, so what is checked is exactly what the page runs), driven through
// scenario.mjs's frames, printing what uniforms.cpp prints from the real
// plugin: every table uploaded, the CPU state after each frame, and every
// uniform each of the four programs holds -- a program keeps a uniform's value
// until a pass sets it again, so a pass that does not run this frame (the
// sprites, when there is nothing to draw) prints what it was last given. A
// uniform the port has never set prints nothing here; compare.py requires the
// plugin's to be GL's initial zero.
//
//     node demo/tools/crosscheck/frames.mjs < scenario.txt > js-frames.txt

import { readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const source = readFileSync(join(here, '..', '..', 'plugin.js'), 'utf8');
const begin = source.indexOf('// ==== BEGIN PORT');
const end = source.indexOf('// ==== END FRAME');
if (begin < 0 || end < begin || source.indexOf('// ==== BEGIN FRAME') < begin) {
  process.stderr.write('frames.mjs: the PORT and FRAME markers are missing from demo/plugin.js\n');
  process.exit(2);
}
// eslint-disable-next-line no-new-func
const P = new Function(`${source.slice(begin, end)}\nreturn { SiderealFrames, SupernovaCurve };`)();

const view = new DataView(new ArrayBuffer(8));
const h32 = (v) => (v >>> 0).toString(16).padStart(8, '0');
const d = (v) => { view.setFloat64(0, v); return h32(view.getUint32(0)) + h32(view.getUint32(4)); };
const f = (v) => { view.setFloat32(0, v); return h32(view.getUint32(0)); };
const fromBits32 = (hex) => { view.setUint32(0, parseInt(hex, 16)); return view.getFloat32(0); };
const fromBits64 = (hex) => { view.setUint32(0, parseInt(hex.slice(0, 8), 16)); view.setUint32(4, parseInt(hex.slice(8), 16)); return view.getFloat64(0); };

const out = [];
let inst = null;
let width = 0;
let height = 0;
let state = null;
let frameNumber = -1;

const table = (u) => out.push(`T ${frameNumber} ${u.format} ${u.width}x${u.height} ${Array.from(u.data, f).join(',')}`);

/** One uniform as uniforms.cpp prints it: arrays element by element. */
function hold(held, [name, kind, values]) {
  const per = { v1: 1, v3: 3, v4: 4 }[kind];
  if (per) {
    for (let e = 0; e * per < values.length; e += 1) {
      held.set(`${name}[${e}]`, `f:${Array.from(values.slice(e * per, (e + 1) * per), f).join(',')}`);
    }
    return;
  }
  if (kind === 'i' || kind === 'u') held.set(name, `${kind}:${h32(values[0])}`);
  else held.set(name, `f:${Array.from(values, f).join(',')}`);
}

for (const line of readFileSync(0, 'utf8').split('\n')) {
  const words = line.trim().split(/\s+/);
  const verb = words[0];
  if (verb === 'plugin') {
    const effect = words[1] === 'over';
    [width, height] = words[2].split('x').map(Number);
    out.push(`P ${words[1]} ${width}x${height}`);
    frameNumber = -1;
    inst = new P.SiderealFrames(effect);
    table(P.SupernovaCurve());
    state = { field: new Map(), light: new Map(), sprite: new Map(), composite: new Map() };
  } else if (verb === 'raster') {
    [width, height] = words[1].split('x').map(Number);
    out.push(`R ${width}x${height}`);
  } else if (verb === 'set') {
    inst.set(Number(words[1]), fromBits32(words[2]));
  } else if (verb === 'frame') {
    frameNumber = Number(words[1]);
    const now = fromBits64(words[2]);
    const clip = inst.isEffect ? { width, height, hardwareWidth: width, hardwareHeight: height } : null;
    const fr = inst.frame(now, width, height, clip);
    for (const u of fr.uploads) table(u);
    const nova = inst.events.nova;
    out.push(`C ${frameNumber} now=${d(inst.now)} target=${inst.target.name} inst=${inst.palette.instrument} field=${fr.fieldWidth}x${fr.fieldHeight} extent=${d(inst.fieldExtent[0])},${d(inst.fieldExtent[1])} core=${d(inst.coreSigmaPx)} seeing=${d(inst.seeingSigmaPx)} spike=${d(inst.spikeLengthPx)} exposure=${d(inst.exposure)} meteors=${inst.events.meteors.length} launched=${inst.events.launched.length} nova=${nova.active ? 1 : 0} days=${d(nova.days)}`);
    for (const name of ['field', 'light', 'sprite', 'composite']) {
      const held = state[name];
      for (const u of fr.passes[name] ?? []) hold(held, u);
      const fields = [...held].map(([k, v]) => `${k}=${v}`).sort();
      out.push(`U ${frameNumber} ${name}${fields.map((x) => ` ${x}`).join('')}`);
    }
  } else if (verb) {
    process.stderr.write(`frames.mjs: unknown '${verb}'\n`);
    process.exit(2);
  }
}

process.stdout.write(`${out.join('\n')}\n`);
