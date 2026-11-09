// SM-87 in the browser emulator (fm1-base tools/build.py ../app --web), under node with fm1-base's harness: the
// worklet and app.wasm as a browser runs them. As Axs7 talks to an X7000 through the emulator's MIDI part:
// ROVS, a whole dump (32,752 words, 66,632 bytes, one frame), RSPRM, RSD back word for word, then the slot played.
//   node test/emu_test.mjs [app/build/emu]
import { startEmu, check, done } from "../fm1-base/tests/emu_harness.mjs";

const dir = process.argv[2] || new URL("../app/build/emu", import.meta.url).pathname;
const emu = await startEmu(dir);
const midi = (data) => emu.send({ type: "app", part: "midi", data: Uint8Array.from(data) });
const out = () => emu.take("midi").flatMap((m) => m.msgs || []);
const hex = (b) => [...b].map((x) => x.toString(16).padStart(2, "0")).join(" ");

emu.run(800);
out();

// ROVS: the overall settings, channel 1
midi([0xf0, 0x47, 0x00, 0x01, 0x41, 0x00, 0x00, 0xf7]);
emu.run(100);
let got = out().filter((m) => m[0] === 0xf0);
check(got.length === 1 && got[0].length === 33 && got[0][3] === 0x08, `ROVS answered with OVS (${got.map(hex)})`);

// a whole dump into slot 7 (nn = 6): a ramp through every 12-bit value, looping from 1000 to the end
const N = 32752, SLOT = 6;
const words = Array.from({ length: N }, (_, i) => (i * 7) % 4096);
const tb = (v) => [v & 0x7f, (v >> 7) & 0x7f, (v >> 14) & 0x7f];
const dump = [0xf0, 0x7e, 0x01, SLOT, 0x00, 12, ...tb(25281), ...tb(N), ...tb(1000), ...tb(N), 0];
for (let b = 0; b * 60 < N; b++) {
  const blk = [b & 0x7f];
  for (let i = 0; i < 60; i++) {
    const w = b * 60 + i < N ? words[b * 60 + i] : 0;
    blk.push((w >> 5) & 0x7f, (w & 0x1f) << 2);
  }
  blk.push(blk.slice(1).reduce((a, x) => a ^ x, 0));
  dump.push(...blk);
}
dump.push(0xf7);
check(dump.length === 66632, `the dump is ${dump.length} bytes`);
midi(dump);
emu.run(1500);                                // into the flash (the simulated one): the sound stops a moment

// RSPRM: its parameters, as the sampler leaves them after a dump
midi([0xf0, 0x47, 0x00, 0x04, 0x41, SLOT, 0x00, 0xf7]);
emu.run(100);
got = out().filter((m) => m[0] === 0xf0 && m[3] === 0x0a);
const v = (k, m) => m[7 + 2 * k] | (m[8 + 2 * k] << 7);
const w2 = (k, m) => v(k, m) | (v(k + 1, m) << 8);
check(got.length === 1 && got[0].length === 319, "RSPRM answered with SPRM");
if (got.length) {
  const m = got[0];
  check(w2(134, m) === N && w2(130, m) === 0 && w2(136, m) === 1000 && v(129, m) === 2 && v(148, m) === 76,
    `END ${w2(134, m)}, START ${w2(130, m)}, LOOP ${w2(136, m)}, mode ${v(129, m)}, rate code ${v(148, m)}`);
}

// in a program: P1, keys 100.. none; put it in P2 keys 24-99 with SPRM (as Axs7 sends after a dump)
if (got.length) {
  const m = Uint8Array.from(got[0]);
  m[7 + 2 * 4] = 24; m[8 + 2 * 4] = 0;       // P2 KEYL
  m[7 + 2 * 5] = 99; m[8 + 2 * 5] = 0;       // P2 KEYH
  m[317] = m.slice(7, 317).reduce((a, x) => a ^ x, 0);
  midi(m);
  emu.run(50);
}

// RSD: the slot back, as the sampler sends it, a few blocks a frame: whole when it gets to the page
midi([0xf0, 0x7e, 0x00, SLOT, 0x00, 0xf7]);
let back = [];
for (let t = 0; t < 40 && !back.length; t++) {
  emu.run(100);
  back = out().filter((m) => m[0] === 0xf0 && m[1] === 0x7e && m[2] === 0x01);
}
check(back.length === 1 && back[0].length === dump.length, `RSD: a dump of ${back[0] ? back[0].length : 0} bytes`);
if (back.length) {
  let bad = 0;
  for (let i = 0; i < N; i++) {
    const at = 19 + Math.floor(i / 60) * 122 + 1 + 2 * (i % 60);
    if (((back[0][at] << 5) | (back[0][at + 1] >> 2)) !== words[i]) bad++;
  }
  check(bad === 0, `every word back as sent (${bad} differ)`);
}

// played: program 2, a key
midi([0xc0, 1]);
midi([0x90, 60, 100]);
const peak = emu.run(300);
midi([0x80, 60, 64]);
check(peak > 0.01, `it sounds (peak ${peak.toFixed(3)})`);
done();
