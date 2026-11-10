// SM-87 in the browser emulator (fm1-base tools/build.py ../app --web), under node with fm1-base's harness: the
// worklet and app.wasm as a browser runs them. As Axs7 talks to an X7000 through the emulator's MIDI part:
// ROVS, a whole dump (32,752 words, 66,632 bytes, one frame) and its ACKS, RSPRM, RSD back word for word, then the
// slot played;
// an MMT-8 memory dump in and back; a drum pattern recorded from MIDI and played out.
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
// ACKS, as an X7000 answers a dump even when the sender doesn't wait: the header's and each block's
const acks = out().filter((m) => m.length === 4 && m[0] === 0xf0 && m[1] === 0x7e && m[2] === 0x7f && m[3] === 0xf7);
check(acks.length === 1 + 546, `ACKS for the header and each block: ${acks.length}`);

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

// an MMT-8's memory dump, nearly full (documentation/MMT-8 SYSEX.odt; the BCD beats + 1): eight parts of 1,000 notes
// on track 1, 64 beats each; it replaces the sequencer's memory, and MIDI DUMP (ALGO) sends it back the same
const mem = new Uint8Array(0xff00 - 0x400);
let at = 0x200;
const le = (v) => { mem[at++] = v & 255; mem[at++] = v >> 8; };
for (let p = 0; p < 8; p++) {
  const part = at;
  mem[2 * p] = (part + 0x400) >> 8; mem[2 * p + 1] = (part + 0x400) & 255;
  at += 0x2a;
  mem[part + 0x12] = 0x65; mem[part + 0x13] = 0x00;     // 64 beats: BCD 65
  "PART " .concat(String(p)).padEnd(14).split("").forEach((c, i) => { mem[part + 0x1c + i] = c.charCodeAt(0); });
  for (let j = 0; j < 8; j++) {                        // tracks 8 .. 1
    mem[part + 2 + 2 * j] = (at - part) & 255; mem[part + 3 + 2 * j] = (at - part) >> 8;
    if (j === 7)
      for (let i = 0; i < 1000; i++) {                 // track 1: a note every 6 clocks
        mem[at++] = 0x80 | (36 + (i % 48)); le(i * 6); mem[at++] = 1 + (i % 127); mem[at++] = 0; mem[at++] = 0; mem[at++] = 5;
      }
    mem[at++] = 0x80; le(64 * 96); mem[at++] = 0; mem[at++] = 0x80; mem[at++] = 0; mem[at++] = 0;
  }
  mem[part] = (at - part) & 255; mem[part + 1] = (at - part) >> 8;
}
mem[0xcf] = (at + 0x400) & 255; mem[0xd0] = (at + 0x400) >> 8;
mem[0xd3] = (0xff00 - at - 0x400) & 255; mem[0xd4] = (0xff00 - at - 0x400) >> 8;
mem[0xd5] = 0x5a;                                      // a byte not to alter: sent back as it came
const used = mem.subarray(0, at);
const bits = [];
for (const x of used) for (let b = 7; b >= 0; b--) bits.push((x >> b) & 1);
while (bits.length % 7) bits.push(0);
const mdump = [0xf0, 0x00, 0x00, 0x0e, 0x00];
for (let i = 0; i < bits.length; i += 7) mdump.push(bits.slice(i, i + 7).reduce((a, b) => (a << 1) | b, 0));
mdump.push(0xf7);
check(mdump.length > 65000, `the MMT-8 dump: ${at} bytes, ${mdump.length} sent`);
midi(mdump);
emu.run(500);
out();
// SEQ, ALGO to MIDI DUMP (the last function), REC
const tap = (b) => { emu.send({ type: "buttons", mask: 1 << b }); emu.run(60); emu.send({ type: "buttons", mask: 0 }); emu.run(60); };
tap(9);
emu.send({ type: "enc", role: 1, n: 11 });
emu.run(100);
tap(11);
let mback = [];
for (let t = 0; t < 60 && !mback.length; t++) {
  emu.run(100);
  mback = out().filter((m) => m[0] === 0xf0 && m[1] === 0x00 && m[2] === 0x00 && m[3] === 0x0e);
}
check(mback.length === 1 && mback[0].length === mdump.length, `MIDI DUMP: ${mback[0] ? mback[0].length : 0} bytes back`);
if (mback.length) {
  let bad = 0;
  for (let i = 0; i < mdump.length; i++) if (mback[0][i] !== mdump[i]) bad++;
  check(bad === 0, `the same dump, byte for byte (${bad} differ)`);
}

// the HR-16: its page (ARP), REC armed, PLAY: a kick from MIDI channel 10 goes into the pattern as it loops, and
// plays out (on 10) the second time round (a new pattern's 8 beats at 120 BPM: 4 s)
tap(8);
tap(11);
emu.send({ type: "buttons", mask: 1 << 10 });
emu.run(30);
midi([0x99, 36, 100]);
emu.send({ type: "buttons", mask: 0 });
emu.run(3900);
check(!out().some((m) => m[0] === 0x99), "the first time round: the kick not played again");
emu.run(400);
const kicks = out().filter((m) => m[0] === 0x99 && m[1] === 36 && m[2] === 100);
check(kicks.length === 1, `the second time round: the kick out on channel 10 (${kicks.length})`);
tap(10);
done();
