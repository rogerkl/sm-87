// web/sm87bank.js against tools/mkrom.py: the same bank from the same samples, and the same package with them put
// in, byte for byte (so fm1-base's tools/fm1pkg_make.py's: test/bank_test.py checks mkrom.py's against it). The
// packages are bank_test.py's, left in DIR; the device build's too when there was one.
//   .venv/bin/python test/bank_test.py DIR && node test/bank_js_test.mjs DIR      (test/run.sh does both)
import { existsSync, readFileSync } from "fs";
import { join } from "path";
import {
  HR16_DRUMS, NO_SOUND, Sm87BankError, Sm87Package, bankName, crc16, crc32, hr16Pads, makeBank, packWords, packedSize,
  readBank, unpackWords,
} from "../web/sm87bank.js";

const dir = process.argv[2];
if (!dir) {
  console.error("bank_js_test: DIR, from test/bank_test.py DIR");
  process.exit(2);
}
const file = (n) => new Uint8Array(readFileSync(join(dir, n)));
const J = JSON.parse(readFileSync(join(dir, "samples.json"), "utf8"));
let checks = 0, failed = 0;
const check = (ok, what) => {
  checks++;
  if (!ok) {
    failed++;
    console.log(`FAIL: ${what}`);
  }
};
const same = (a, b) => a.length === b.length && a.every((x, i) => x === b[i]);
const refused = (fn, code, what) => {
  try {
    fn();
    check(false, `${what}: not refused`);
  } catch (e) {
    check(e instanceof Sm87BankError && e.code === code, `${what}: ${e.code} (${e.message})`);
  }
};

// the checksums
const ascii = (s) => Uint8Array.from(s, (c) => c.charCodeAt(0));
check(crc32(ascii("123456789")) === 0xCBF43926, "CRC-32 of 123456789");
check(crc16(ascii("123456789")) === 0x31C3, "CRC-16 (0x1021, 0) of 123456789");

// packing: two words to three bytes and a pad byte, and back
const w = [0, 4095, 2048, 1, 0xABC];
check(packedSize(5) === 10 && same(packWords(w), [0x00, 0xF0, 0xFF, 0x00, 0x18, 0x00, 0xBC, 0x0A, 0x00, 0x00]),
  `packed: ${[...packWords(w)].map((x) => x.toString(16))}`);
check(same(unpackWords(packWords(w), 5), w), "unpacked again");
const nm = bankName('vår "beste" \\ korlyd 1234567890');
check(nm === "v?r ?beste? ? korlyd 12", `a name cleaned and cut: ${nm}`);

// the bank: the same bytes as mkrom.py's, from the samples in another order, and the kit
const bank = makeBank(J.samples, J.kit);
check(same(bank, file("bank.bin")), `the bank: mkrom.py's bytes (${bank.length})`);
const c = readBank(bank);
const held = c && c.samples;
const bySlot = [...J.samples].sort((a, b) => a.slot - b.slot);
check(held && held.length === bySlot.length && held.every((h, i) => h.slot === bySlot[i].slot &&
  h.name === J.names[i] && h.pressure === bySlot[i].pressure && same(h.params, bySlot[i].params) &&
  same(h.words, bySlot[i].words)), "read back: slots, names, pressure, parameters, words");
check(c && c.size === bank.length && c.kit && c.kit.sounds.length === J.kit.sounds.length &&
  c.kit.sounds.every((x, i) => x.name === J.kit.sounds[i].name && x.rate === J.kit.sounds[i].rate &&
    x.vel === J.kit.sounds[i].vel && same(x.words, J.kit.sounds[i].words)) &&
  JSON.stringify(c.kit.pads) === JSON.stringify(J.kit.pads), "the kit read back: sounds with their words, pads");
const noKit = readBank(makeBank(J.samples));
check(noKit && noKit.kit === null, "without a kit: none");
const kitAlone = readBank(makeBank([], J.kit));
check(kitAlone && kitAlone.samples.length === 0 && kitAlone.kit.pads.length === J.kit.pads.length, "a kit alone");

// the HR-16's drums on SM-87's pads
check(HR16_DRUMS.length === 16 && new Set(HR16_DRUMS.map((d) => d.pad)).size === 16 &&
  HR16_DRUMS.every((d) => d.name.length <= 6), "the HR-16's 16 drums: each on a pad of its own, names of up to 6");
check(JSON.stringify(HR16_DRUMS.map((d) => d.pad)) === "[0,1,2,6,3,7,14,15,8,9,10,11,5,4,12,13]",
  "on the pads an HR-16 dump puts them on (hrdump.h HD_PAD)");
const byDrum = new Array(16).fill(null);
byDrum[0] = { sound: 0 };
byDrum[3] = { sound: 1, tune: -3, level: 70, pan: 2 };   // MID HAT
byDrum[13] = { sound: 0, tune: 5 };                       // CRASH, the kick's sound tuned up
const hp = hr16Pads(byDrum);
check(hp.length === 16 && hp[0].name === "KICK" && hp[0].sound === 0 && hp[0].note === 36 && hp[0].level === 85 &&
  hp[6].name === "MDHAT" && hp[6].sound === 1 && hp[6].tune === -3 && hp[6].level === 70 && hp[6].pan === 2 &&
  hp[6].group === 1 && hp[4].name === "CRASH" && hp[4].tune === 5 && hp[4].poly === 2 && hp[4].note === 49 &&
  hp[1].name === "SNARE" && hp[1].sound === NO_SOUND && hp[1].note === 38, "hr16Pads: the pads in SM-87's order");
const hk = readBank(makeBank([], { sounds: J.kit.sounds.slice(0, 2), pads: hp }));
check(hk && hk.kit.pads.length === 16 && hk.kit.pads[6].tune === -3 && hk.kit.pads[4].sound === 0,
  "a kit of hr16Pads' pads: in and back");

// a package as built: what it is
const orig = file("orig.fwsc");
const pkg = new Sm87Package(orig, "orig");
check(pkg.identity === J.identity && pkg.imageEnd === J.imageEnd && pkg.slotSize === J.slot &&
  pkg.room === J.slot - J.imageEnd, `identity ${pkg.identity}, image ${pkg.imageEnd}, room ${pkg.room}`);
check(pkg.samples() === null, "no samples in it");
check(same(pkg.bytes(), orig), "closed again unchanged: the same bytes");

// samples in: mkrom.py's package; read back; out again: the package as built
pkg.put(bank);
const baked = pkg.bytes();
check(same(baked, file("baked.fwsc")), "samples in: mkrom.py's package");
const pb = new Sm87Package(baked, "baked");
const got = pb.samples();
check(got && got.length === 3 && got[2].slot === 9 && same(got[2].words, bySlot[2].words) && pb.kit() &&
  pb.kit().pads.length === J.kit.pads.length, "read from the package: samples, kit");
pb.put(null);
check(same(pb.bytes(), orig), "out again: the package as built, byte for byte");
const pa = new Sm87Package(baked, "baked");
pa.put(makeBank(J.samples.slice(0, 1)));
const one = new Sm87Package(pa.bytes(), "one").samples();
check(one && one.length === 1 && one[0].slot === J.samples[0].slot, "in again, fewer: the old ones gone");

// another chip key and identity
const po = new Sm87Package(file("other.fwsc"), "other");
check(po.key === 0x1234 && po.identity === "FM-1_87010203", `chip key 0x${po.key.toString(16)}, ${po.identity}`);
po.put(bank);
check(same(po.bytes(), file("other-baked.fwsc")), "another chip key: mkrom.py's package");

// the device build's package (JieLi's SDK files in it), when bank_test.py had one
if (existsSync(join(dir, "real.fwsc"))) {
  const pr = new Sm87Package(file("real.fwsc"), "sm87.fwsc");
  pr.put(bank);
  check(same(pr.bytes(), file("real-baked.fwsc")), `the device build's package (${pr.identity}): mkrom.py's`);
} else {
  console.log("bank_js_test: no device build's package, that check not made");
}

// refused
refused(() => new Sm87Package(file("nodesc.fwsc"), "x"), "no-bank", "a firmware without the bank's description");
refused(() => new Sm87Package(file("v1.fwsc"), "x"), "format", "another bank format (1)");
const bad = orig.slice();
bad[5000] ^= 1;
refused(() => new Sm87Package(bad, "x"), "not-package", "a damaged package");
refused(() => new Sm87Package(orig.subarray(0, 900), "x"), "not-package", "a package cut short");
refused(() => new Sm87Package(new Sm87Package(file("stray.fwsc"), "x").put(bank)), "stray",
  "something above the firmware that isn't samples");
const full = new Sm87Package(orig, "orig");
const long = (slot) => ({ slot, name: `s${slot}`, params: J.samples[0].params,
                          words: Array.from({ length: 32752 }, (_, i) => (i * (slot + 1)) % 4096) });
const each = 188 + ((packedSize(32752) + 3) & ~3);
const many = Array.from({ length: Math.floor(full.room / each) + 1 }, (_, i) => long(i));
refused(() => full.put(makeBank(many)), "too-big", `${many.length} full samples`);
full.put(makeBank(many.slice(1)));
check(new Sm87Package(full.bytes(), "full").samples().length === many.length - 1,
  `${many.length - 1} full samples: in`);
const damaged = baked.slice();
const dp = new Sm87Package(damaged, "d");
dp.app[dp.slotSize - 100] ^= 1;               // a word of the last sample
refused(() => dp.samples(), "damaged-bank", "samples that don't check");
const s0 = J.samples[0];
refused(() => makeBank([s0, { ...s0 }]), "bad-sample", "two in a slot");
refused(() => makeBank([{ ...s0, slot: 16 }]), "bad-sample", "slot 17");
refused(() => makeBank([{ ...s0, words: [] }]), "bad-sample", "no words");
refused(() => makeBank([{ ...s0, words: new Array(32753).fill(2048) }]), "bad-sample", "too many words");
refused(() => makeBank([{ ...s0, words: [2048, 4096] }]), "bad-sample", "a word out of 12 bits");
refused(() => makeBank([{ ...s0, params: s0.params.slice(1) }]), "bad-sample", "154 parameters");
refused(() => makeBank([{ ...s0, pressure: 100 }]), "bad-sample", "pressure 100");
refused(() => makeBank(Array.from({ length: 17 }, (_, i) => ({ ...s0, slot: i % 16 }))), "bad-sample", "17 samples");
const k0 = J.kit;
const kitWith = (what, i, change) => ({ ...k0, [what]: k0[what].map((x, j) => (j === i ? { ...x, ...change } : x)) });
refused(() => makeBank([], kitWith("sounds", 0, { rate: 35 })), "bad-sample", "a kit sound's rate code 35");
refused(() => makeBank([], kitWith("sounds", 0, { vel: 100 })), "bad-sample", "vel 100");
refused(() => makeBank([], kitWith("sounds", 1, { name: "SEVENCH" })), "bad-sample", "a name of 7");
refused(() => makeBank([], kitWith("sounds", 1, { words: [] })), "bad-sample", "a sound without words");
refused(() => makeBank([], kitWith("pads", 0, { sound: k0.sounds.length + 16 })), "bad-sample",
  "a sound past the slots");
refused(() => makeBank([], kitWith("pads", 0, { tune: 25 })), "bad-sample", "tune 25");
refused(() => makeBank([], kitWith("pads", 0, { pan: -4 })), "bad-sample", "pan -4");
refused(() => makeBank([], kitWith("pads", 0, { poly: 0 })), "bad-sample", "poly 0");
refused(() => makeBank([], kitWith("pads", 0, { group: 10 })), "bad-sample", "group 10");
refused(() => makeBank([], { sounds: [], pads: new Array(17).fill(k0.pads[0]) }), "bad-sample", "17 pads");
refused(() => makeBank([], { sounds: new Array(65).fill(k0.sounds[0]), pads: [] }), "bad-sample", "65 sounds");
const dk = new Sm87Package(baked, "dk");
const kitPad = dk.slotSize - dk.contents().size + 3 * 188 + J.kit.sounds.length * 16;   // its first pad
dk.app[kitPad + 9] = 30;                      // tune 30
refused(() => dk.kit(), "damaged-bank", "a kit pad that doesn't check (its CRC first)");

console.log(`bank_js_test: ${checks - failed} of ${checks} checks passed`);
process.exit(failed ? 1 : 0);
