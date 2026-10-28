// The JavaScript half of the browser demo's PORT cross-check: the PORT block
// of demo/plugin.js (between its BEGIN PORT and END PORT markers, sliced out
// as text and run here, so what is checked is exactly what the page runs),
// driven through printer.cpp's scenario and printed the same way.
// crosscheck.sh compares the two. Read this beside printer.cpp: each block
// mirrors one there.
//
//     node demo/tools/crosscheck/run.mjs > js.txt

import { readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const source = readFileSync(join(here, '..', '..', 'plugin.js'), 'utf8');
const begin = source.indexOf('// ==== BEGIN PORT');
const end = source.indexOf('// ==== END PORT');
if (begin < 0 || end < begin) {
  process.stderr.write('run.mjs: the PORT markers are missing from demo/plugin.js\n');
  process.exit(2);
}
const names = [
  'PT', 'InfoOf', 'OptionName', 'HostOrder', 'OptionIndex',
  'GasFromParam', 'TurbulenceFromParam', 'FlowFromParam', 'HazeFromParam', 'DustFromParam', 'StarlightFromParam', 'HeatFromParam',
  'LightAngleFromParam', 'StarsFromParam', 'SpikesFromParam', 'SeeingFromParam', 'ZoomFromParam', 'PanFromParam', 'SpinFromParam',
  'DriftFromParam', 'DepthFromParam', 'FlyFromParam', 'MeteorsFromParam', 'SupernovaSpeedFromParam', 'EchoFromParam',
  'ExposureFromParam', 'StretchFromParam', 'SaturationFromParam',
  'Planck', 'PhotonTail', 'OxygenHardness', 'ReducedEnergy', 'Extinction', 'TempOfSample', 'SampleOfTemp', 'kTempSamples',
  'FilterNamed', 'lineResponseOf', 'continuumOf', 'GalaxyResponse', 'kLineComponents', 'kInstruments', 'InstrumentOf',
  'NebulaMix', 'PlanetaryMix', 'RemnantMix', 'StarlightMix', 'BuildPalette', 'DisplayOf', 'DisplayOfContinuum', 'kTauSamples',
  'AngularDiameterDistance', 'SupernovaLight', 'DecayHeating', 'SupernovaTemperature', 'RemnantRadius', 'MeteorLight', 'SupernovaPeakDay',
  'TargetCount', 'Build', 'kFirstGenerative', 'Events',
];
// eslint-disable-next-line no-new-func
const P = new Function(`${source.slice(begin, end)}\nreturn { ${names.join(', ')} };`)();

const out = [];
const print = (line) => out.push(line);
const view = new DataView(new ArrayBuffer(8));
const h32 = (v) => (v >>> 0).toString(16).padStart(8, '0');
const f = (v) => { view.setFloat32(0, v); return h32(view.getUint32(0)); };
const d = (v) => { view.setFloat64(0, v); return h32(view.getUint32(0)) + h32(view.getUint32(4)); };
const floats = (a) => Array.from(a, f).join(',');
const F = Math.fround;

function target(index, seed) {
  const t = P.Build(index, seed);
  print(`target ${index} ${seed} ${t.name} as=${t.asReleased} haze=${f(t.haze)} shade=${f(t.hazeShade)} hscale=${f(t.hazeScale)} wscale=${f(t.warpScale)} warp=${f(t.warp)} field=${f(t.fieldStars)} emb=${f(t.embedded)} dust=${f(t.dustRatio)} exp=${f(t.exposure)} str=${f(t.stretch)} sn=${f(t.snX)},${f(t.snY)} echo=${f(t.echoDistance)} gal=${t.galaxyScene ? 1 : 0}`);
  print(`mix ${index} ${seed} ${t.mix.line.map(d).join(' ')} ${d(t.mix.star)} ${d(t.mix.starTemp)} ${d(t.mix.dust)} ${d(t.mix.dustTemp)}`);
  t.elements.forEach((e, i) => print(`el ${i} ${e.shape} ${e.channel} ${[e.x, e.y, e.x2, e.y2, e.a, e.b, e.angle, e.density, e.thickness, e.edge, e.rough, e.detail, e.depth, e.extra, e.extra2].map(f).join(' ')} ${e.polyStart} ${e.polyCount}`));
  print(`poly ${t.polygon.length} ${floats(t.polygon)}`);
  for (const s of t.sources) print(`src ${[s.x, s.y, s.h, s.luminosity, s.temperature, s.soft].map(f).join(' ')} ${s.visible ? 1 : 0}`);
  for (const s of t.stars) print(`star ${[s.x, s.y, s.flux, s.temperature, s.depth].map(f).join(' ')} ${s.layer}`);
  for (const g of t.galaxies) {
    print(`galaxy ${g.kind} ${[g.x, g.y, g.scale, g.incline, g.pa].map(f).join(' ')} ${g.arms} ${[g.pitch, g.armContrast, g.bulge, g.bulgeRe, g.sersic, g.bulgeAxis, g.dust, g.dustRing, g.hii, g.brightness, g.diskT, g.bulgeT, g.armT, g.phase, g.depth].map(f).join(' ')}`);
  }
  const deep = t.deep;
  print(`deep ${deep.on ? 1 : 0} ${f(deep.density)} ${deep.lens ? 1 : 0} ${[deep.lensX, deep.lensY, deep.einstein, deep.zLens, deep.members, deep.coreRadius].map(f).join(' ')}`);
  print(`band ${[t.band.amount, t.band.angle, t.band.width, t.band.offset].map(f).join(' ')}`);
}

//---------------------------------------------------------------------------
// Controls.
//---------------------------------------------------------------------------
for (let k = -10; k <= 1010; k += 1) {
  const v = F(k / 1000.0);
  print(`ctl ${k} ${f(v)} gas=${d(P.GasFromParam(v))} turb=${d(P.TurbulenceFromParam(v))} flow=${d(P.FlowFromParam(v))} haze=${d(P.HazeFromParam(v))} dust=${d(P.DustFromParam(v))} light=${d(P.StarlightFromParam(v))} heat=${d(P.HeatFromParam(v))} angle=${d(P.LightAngleFromParam(v))} stars=${d(P.StarsFromParam(v))} spikes=${d(P.SpikesFromParam(v))} seeing=${d(P.SeeingFromParam(v))} zoom=${d(P.ZoomFromParam(v))} pan=${d(P.PanFromParam(v))} spin=${d(P.SpinFromParam(v))} drift=${d(P.DriftFromParam(v))} depth=${d(P.DepthFromParam(v))} fly=${d(P.FlyFromParam(v))} met=${d(P.MeteorsFromParam(v))} sn=${d(P.SupernovaSpeedFromParam(v))} echo=${d(P.EchoFromParam(v))} exp=${d(P.ExposureFromParam(v))} str=${d(P.StretchFromParam(v))} sat=${d(P.SaturationFromParam(v))}`);
}
for (let k = -40; k <= 2440; k += 1) {
  const v = F(k / 100.0);
  print(`opt ${k} ${P.OptionIndex(v, 22)} ${P.OptionIndex(v, 8)} ${P.OptionIndex(v, 2)} ${P.OptionIndex(-v, 4)}`);
}
for (let effect = 0; effect < 2; effect += 1) print(`order ${effect} ${P.HostOrder(effect === 1).join(' ')}`);
for (let id = 0; id < P.PT.ABOUT_TEXT; id += 1) {
  const info = P.InfoOf(id);
  const options = Array.from({ length: info.optionCount }, (_, i) => `|${P.OptionName(id, i)}`).join('');
  print(`param ${id} ${info.name}|${info.group} kind=${info.kind} default=${f(info.defaultValue)} options=${info.optionCount}${options}`);
}

//---------------------------------------------------------------------------
// Physics.
//---------------------------------------------------------------------------
for (let k = 0; k <= 400; k += 1) {
  const t = 2000.0 * Math.pow(60.0, k / 400.0);
  print(`bb ${k} ${d(P.Planck(0.1 + 0.05 * k, t))} ${d(P.PhotonTail(k / 20.0))} ${d(P.OxygenHardness(t))} ${d(P.ReducedEnergy(13.598, t))} ${d(P.Extinction(0.1 + 0.06 * k))} ${d(P.Extinction(0.1 + 0.06 * k, 5.0))}`);
}
for (let i = 0; i < P.kTempSamples; i += 1) {
  print(`temp ${i} ${d(P.TempOfSample(0, i))} ${d(P.TempOfSample(2, i))} ${d(P.SampleOfTemp(0, 2000.0 + 900.0 * i))} ${d(P.SampleOfTemp(2, 10.0 + 10.0 * i))}`);
}
for (const name of ['F502N', 'F657N', 'F673N', 'F435W', 'F555W', 'F606W', 'F775W', 'F814W', 'F850LP', 'F658N',
  'F110W', 'F160W', 'F090W', 'F150W', 'F162M', 'F187N', 'F200W', 'F212N', 'F277W', 'F300M',
  'F335M', 'F356W', 'F405N', 'F444W', 'F470N', 'F770W', 'F1130W', 'F1500W', 'OSC-B', 'OSC-G', 'OSC-R', 'NOPE']) {
  const filter = P.FilterNamed(name);
  const parts = [`filter ${name} ${filter.name} ${d(filter.pivot)} ${d(filter.width)}`];
  for (let c = 0; c < P.kLineComponents; c += 1) parts.push(d(P.lineResponseOf(filter, c)));
  for (let kind = 0; kind < 3; kind += 1) for (const t of [20.0, 90.0, 3000.0, 5800.0, 12000.0, 40000.0]) parts.push(d(P.continuumOf(filter, kind, t)));
  for (const z of [0.0, 0.5, 2.0, 6.0]) parts.push(d(P.GalaxyResponse(filter, 6500.0, z)));
  print(parts.join(' '));
}
for (let id = 0; id < P.kInstruments + 1; id += 1) {
  const inst = P.InstrumentOf(id);
  const keys = inst.filters.map((name, k) => ` ${name}=${inst.colour[k].map(f).join(',')}`).join('');
  print(`inst ${id} ${inst.name} n=${inst.filterCount} ap=${d(inst.aperture)} sp=${inst.spikes} ang=${d(inst.spikeAngle)} see=${d(inst.seeing)} glow=${d(inst.dustGlow)} wb=${d(inst.whiteBalance)} exp=${d(inst.exposure)}${keys}`);
}
const mixes = [P.NebulaMix(), P.PlanetaryMix(), P.RemnantMix(), P.StarlightMix()];
for (let id = 0; id < P.kInstruments; id += 1) {
  for (let m = 0; m < 4; m += 1) {
    const p = P.BuildPalette(id, mixes[m]);
    let line = `pal ${id} ${m} n=${p.filterCount}`;
    for (let k = 0; k < p.filterCount; k += 1) line += ` ${[p.gain[k], p.ext[k], p.pivot[k], ...p.colour[k]].map(d).join(',')}`;
    print(line);
    const n = P.kTauSamples * 3;
    for (let c = 0; c < P.kLineComponents; c += 1) print(`lines ${id} ${m} ${c} ${floats(p.lines.subarray(c * n, (c + 1) * n))}`);
    for (let r = 0; r < 3 * P.kTempSamples; r += 1) print(`cont ${id} ${m} ${r} ${floats(p.continua.subarray(r * n, (r + 1) * n))}`);
    for (let r = 0; r < P.kTempSamples; r += 1) print(`galx ${id} ${m} ${r} ${floats(p.galaxies.subarray(r * 96, (r + 1) * 96))}`);
    for (let c = 0; c < P.kLineComponents; c += 1) print(`disp ${id} ${m} ${c} ${P.DisplayOf(p, c, 0.7).map(d).join(' ')}`);
    for (let kind = 0; kind < 3; kind += 1) print(`dispc ${id} ${m} ${kind} ${P.DisplayOfContinuum(p, kind, kind === 2 ? 60.0 : 6100.0, 1.3).map(d).join(' ')}`);
  }
}
for (let i = 0; i <= 64; i += 1) print(`cosmo ${i} ${d(P.AngularDiameterDistance(0.125 * i))}`);
for (let i = -10; i <= 2000; i += 1) {
  print(`sn ${i} ${d(P.SupernovaLight(0.45 * i))} ${d(P.DecayHeating(0.45 * i))} ${d(P.SupernovaTemperature(0.45 * i))} ${d(P.RemnantRadius(i / 300.0))} ${d(P.MeteorLight(i / 500.0))}`);
}
print(`snpeak ${d(P.SupernovaPeakDay())}`);

//---------------------------------------------------------------------------
// Scene.
//---------------------------------------------------------------------------
print(`targets ${P.TargetCount()} first=${P.kFirstGenerative}`);
for (let index = 0; index < P.TargetCount(); index += 1) {
  for (const seed of [0, 1, 2, 7, 137, 499, 500, 731, 999, 1000]) target(index, seed);
}
for (let seed = 0; seed <= 1000; seed += 3) {
  for (let index = P.kFirstGenerative; index < P.TargetCount(); index += 1) target(index, seed);
}

//---------------------------------------------------------------------------
// Events.
//---------------------------------------------------------------------------
for (let run = 0; run < 6; run += 1) {
  const events = new P.Events();
  const fps = run % 2 ? 50.0 : 60.0;
  for (let frame = 0; frame < 4000; frame += 1) {
    const now = 3.25 + frame / fps;
    const dt = frame === 0 ? 0.0 : 1.0 / fps;
    const input = {
      perMinute: P.MeteorsFromParam(F(((Math.trunc(frame / 400) % 5) * 0.25 * (Math.trunc(run / 2) + 1)) / 3.0)),
      shower: (Math.trunc(frame / 700) + run) % 2 === 0,
      presses: frame % 233 === 17 ? 1 + (frame % 3) : 0,
      supernova: frame === 30 || frame === 2100,
      daysPerSecond: P.SupernovaSpeedFromParam(F(0.3 + 0.15 * run)),
      snX: F(F(0.1) * run),
      snY: F(F(-0.05) * run),
      radiantX: F(-1.3),
      radiantY: F(0.9),
    };
    if (frame === 1500) events.Reset();
    events.Advance(now, dt, input, (run * 4096 + 17 * run) >>> 0);
    const n = events.nova;
    print(`ev ${run} ${frame} m=${events.meteors.length} l=${events.launched.length} nova=${n.active ? 1 : 0} ${f(n.x)} ${f(n.y)} ${d(n.days)} ${f(n.flux)} ${f(n.temperature)} ${f(n.remnantRadius)} ${f(n.remnantGlow)} ${f(n.echoCt)} ${d(now)}`);
    if (input.presses > 0 || frame % 97 === 0) {
      for (const m of events.meteors) {
        print(`met ${run} ${frame} ${[m.x, m.y, m.dx, m.dy, m.length].map(f).join(' ')} ${d(m.start)} ${[m.duration, m.rate, m.peak, m.fast, m.train, m.trainLife, m.speed].map(f).join(' ')}`);
      }
    }
  }
}

process.stdout.write(`${out.join('\n')}\n`);
