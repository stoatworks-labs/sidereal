// The frame cross-check's scenario: what a host does to the plugin, frame by
// frame, written out ONCE so the C++ (uniforms.cpp) and the port (frames.mjs)
// read the same thing. Every number is written as its bits, so neither side
// parses a decimal.
//
//     node demo/tools/crosscheck/scenario.mjs > scenario.txt
//
// Lines:
//   plugin source|over WxH    a new instance (InitGL), at that raster
//   raster WxH                the host's raster changes under it
//   set ID BITS32             SetFloatParameter by ParamId (SetById: a control
//                             the plugin does not declare is ignored)
//   frame N BITS64            SetTime( t ), then ProcessOpenGL; print
//
// It visits every target and every telescope, generative targets at several
// seeds, moves every control that feeds a uniform or a table, runs meteors of
// both kinds and presses Fire Meteor, sets off a supernova and runs its light
// curve, flies, and makes the clock do what hosts do: jitter, a stall, a step
// back, a jump of more than a tenth of a second, irregular steps.

const view = new DataView(new ArrayBuffer(8));
const b32 = (v) => { view.setFloat32(0, v); return view.getUint32(0).toString(16).padStart(8, '0'); };
const b64 = (v) => { view.setFloat64(0, v); return view.getUint32(0).toString(16).padStart(8, '0') + view.getUint32(4).toString(16).padStart(8, '0'); };

const PT = {
  TARGET: 0, TELESCOPE: 1, SEED: 2, GAS: 3, TURBULENCE: 4, FLOW: 5, HAZE: 6, DUST: 7, STARLIGHT: 8, STAR_HEAT: 9, LIGHT_ANGLE: 10,
  STARS: 11, SPIKES: 12, SEEING: 13, ZOOM: 14, PAN_X: 15, PAN_Y: 16, SPIN: 17, DRIFT: 18, DEPTH: 19, FLY: 20,
  METEORS: 21, METEOR_KIND: 22, FIRE_METEOR: 23, SUPERNOVA: 24, SN_SPEED: 25, ECHO: 26,
  EXPOSURE: 27, STRETCH: 28, SATURATION: 29, CLIP_AS: 30, MIX: 31,
};
const STANDARD = [
  [PT.GAS, 0.5], [PT.TURBULENCE, 0.5], [PT.FLOW, 0.25], [PT.HAZE, 0.5], [PT.DUST, 0.5], [PT.STARLIGHT, 0.5], [PT.STAR_HEAT, 0.5],
  [PT.LIGHT_ANGLE, 0.5], [PT.STARS, 0.5], [PT.SPIKES, 0.5], [PT.SEEING, 0.0], [PT.ZOOM, 0.5], [PT.PAN_X, 0.5], [PT.PAN_Y, 0.5],
  [PT.SPIN, 0.5], [PT.DRIFT, 0.25], [PT.DEPTH, 0.5], [PT.FLY, 0.0], [PT.SN_SPEED, 0.5], [PT.ECHO, 0.5], [PT.EXPOSURE, 0.5],
  [PT.STRETCH, 0.5], [PT.SATURATION, 0.5], [PT.MIX, 1.0],
];

const lines = [];
let frame = 0;
let time = 0.0;
let seed = 12345;
const rand = () => { seed = (Math.imul(seed, 1103515245) + 12345) >>> 0; return seed / 4294967296; };

function plugin(kind, w, h) { lines.push(`plugin ${kind} ${w}x${h}`); frame = 0; time = 1000.0 + 0.37; }
function raster(w, h) { lines.push(`raster ${w}x${h}`); }
function set(id, v) { lines.push(`set ${id} ${b32(v)}`); }
function tick(dt) { lines.push(`frame ${frame} ${b64(time)}`); frame += 1; time += dt; }
/** n frames at a host's ~60 fps: a step of 1/60 s, jittered by up to a millisecond. */
function run(n, fps = 60) {
  for (let i = 0; i < n; i += 1) tick(1 / fps + (rand() - 0.5) * 0.002);
}
function press(id) { set(id, 1); tick(1 / 60); set(id, 0); }

function tour(effect) {
  run(40);
  // Every target as released, then every telescope on three of them.
  for (let t = 0; t < 22; t += 1) { set(PT.TARGET, t); run(5); }
  for (const t of [0, 11, 17]) {
    set(PT.TARGET, t);
    for (let k = 0; k < 8; k += 1) { set(PT.TELESCOPE, k); run(3); }
  }
  set(PT.TELESCOPE, 0);
  // Generative targets at several seeds.
  for (const t of [15, 16, 17, 18, 19, 20, 21]) {
    set(PT.TARGET, t);
    for (const s of [0.0, 0.137, 0.5004, 0.999, 1.0]) { set(PT.SEED, s); run(2); }
  }
  set(PT.SEED, 0.0);
  // Every standard control, to both ends of its range and back, on a nebula
  // with stars in front (Orion) and on a galaxy (the Whirlpool).
  for (const t of [3, 11]) {
    set(PT.TARGET, t);
    for (const [id, home] of STANDARD) {
      set(id, 0.0); run(2);
      set(id, 0.13); run(2);
      set(id, 0.87); run(2);
      set(id, 1.0); run(2);
      set(id, home);
    }
  }
  // Meteors: a shower, then sporadic, then presses, on the Pleiades.
  set(PT.TARGET, 9);
  set(PT.METEORS, 0.9); run(240);
  set(PT.METEOR_KIND, 0); run(200);
  set(PT.METEORS, 0.0);
  press(PT.FIRE_METEOR); run(20); press(PT.FIRE_METEOR); press(PT.FIRE_METEOR); run(90);
  set(PT.METEOR_KIND, 1); press(PT.FIRE_METEOR); run(60);
  // A supernova on the Pillars, its light curve run fast, the echo up.
  set(PT.TARGET, 0);
  set(PT.SN_SPEED, 0.95); set(PT.ECHO, 1.0);
  press(PT.SUPERNOVA); run(300);
  set(PT.SN_SPEED, 1.0); run(200);
  press(PT.SUPERNOVA); run(60);
  set(PT.SN_SPEED, 0.5); set(PT.ECHO, 0.5);
  // Flying, with the camera moved.
  set(PT.TARGET, 21); set(PT.FLY, 0.7); set(PT.ZOOM, 0.7); set(PT.SPIN, 0.62); run(90);
  set(PT.FLY, 0.0); set(PT.ZOOM, 0.5); set(PT.SPIN, 0.5);
  if (effect) {
    set(PT.TARGET, 0);
    set(PT.CLIP_AS, 1); run(40);
    set(PT.MIX, 0.4); run(20);
    set(PT.CLIP_AS, 0); run(20);
    set(PT.MIX, 1.0);
  }
  // The clock misbehaving: 30 fps, a stall, a step back, jumps, irregular steps.
  set(PT.TARGET, 1); set(PT.DRIFT, 1.0); set(PT.FLOW, 1.0);
  run(60, 30);
  tick(0); tick(0); tick(1 / 60);
  time -= 0.4; run(10);
  time += 3.0; run(10);
  for (let i = 0; i < 120; i += 1) tick(0.002 + rand() * 0.15);
}

plugin('source', 320, 180);
tour(false);
raster(640, 360); run(20);
raster(333, 187); run(20);

plugin('over', 320, 180);
tour(true);
raster(480, 270); run(20);

process.stdout.write(`${lines.join('\n')}\n`);
