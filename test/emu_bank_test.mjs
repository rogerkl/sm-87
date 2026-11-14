// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Roger Klaveness
// The emulator's "Load .fwsc" (app/web/emu/app.js, app_worklet.js; s700/s7mem.c sm87_bank_*; and Axs7's OPEN IN
// EMULATOR, through web/sm87emu.js): a package's samples, as
// the page gives them in the load message, played in place of the build's own. Each case in an emulator of its own
// (the harness takes one a process):
//   - none given: the build's own samples, and the page told none was given;
//   - a package's (test/bank_test.py's samples and kit, each sample to its END, put into its orig.fwsc as Axs7 puts
//     them): taken; a sample in its slot, word for word (RSD), its kit's sounds the pads';
//   - the same, damaged: not taken, the build's own play.
//   node test/emu_bank_test.mjs [app/build/emu] [build/test/bank]
import { execFileSync } from "node:child_process";
import { readFileSync } from "node:fs";
import { resolve } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import { startEmu, check, done } from "../fm1-base/tests/emu_harness.mjs";

const dir = resolve(process.argv[2] || "app/build/emu");
const banks = resolve(process.argv[3] || "build/test/bank");
const caseName = process.argv[4];

if (caseName) {                                   // one case: what the emulator made of it, as JSON
  const sb = await import(pathToFileURL(resolve(dir, "sm87bank.js")).href);
  const { emuSamples } = await import(pathToFileURL(resolve(dir, "sm87emu.js")).href);
  const held = new sb.Sm87Package(readFileSync(resolve(banks, "baked.fwsc")), "baked.fwsc").contents();
  const samples = held.samples.map((x) => {      // (the fixture's END is 0: RSD sends START to END)
    const params = Uint8Array.from(x.params);
    params[134] = x.words.length & 0xff;
    params[135] = x.words.length >> 8;
    return { ...x, params };
  });
  const pkg = new sb.Sm87Package(readFileSync(resolve(banks, "orig.fwsc")), "orig.fwsc");
  pkg.put(sb.makeBank(samples, held.kit));
  const k = emuSamples(pkg.bytes(), "test.fwsc");
  let bank = null;
  if (caseName === "bank") bank = k.bank;
  if (caseName === "damaged") {
    const b = new Uint8Array(k.bank.slice(0));
    b[200] ^= 0x55;                               // (in the first sample's entry: its CRC no longer checks)
    bank = b.buffer;
  }
  const emu = await startEmu(dir, bank ? { parts: { app: bank } } : {});
  emu.run(300);
  const told = emu.msgs.filter((m) => m.type === "app" && m.sm87bank).map((m) => m.sm87bank);
  // the second sample's slot back (RSD: 1,001 words), its words
  const s = samples[1];
  emu.send({ type: "app", part: "midi", data: Uint8Array.from([0xf0, 0x7e, 0x00, s.slot, 0x00, 0xf7]) });
  let back = [];
  for (let t = 0; t < 40 && !back.length; t++) {
    emu.run(100);
    back = emu.take("midi").flatMap((m) => m.msgs || []).filter((m) => m[0] === 0xf0 && m[1] === 0x7e && m[2] === 0x01);
  }
  let same = back.length === 1;
  if (same) {
    const d = back[0], n = d[9] | (d[10] << 7) | (d[11] << 14);
    same = n === s.words.length;
    for (let i = 0; same && i < n; i++) {
      const at = 19 + Math.floor(i / 60) * 122 + 1 + 2 * (i % 60);
      same = ((d[at] << 5) | (d[at + 1] >> 2)) === s.words[i];
    }
  }
  console.log(JSON.stringify({ told, taken: emu.ex.sm87_bank_taken(), count: emu.ex.sm87_bank_count(),
                               kit: emu.ex.sm87_bank_kit(), same, samples: k.samples, kitSounds: k.kit }));
  process.exit(0);
}

const run = (c) => JSON.parse(execFileSync(process.execPath, [fileURLToPath(import.meta.url), dir, banks, c],
                                           { encoding: "utf8" }).trim().split("\n").pop());
const none = run("none"), bank = run("bank"), damaged = run("damaged");
check(none.taken === 0 && none.told.length === 1 && none.told[0].taken === 0,
  `none given: the build's own (${none.count} samples, ${none.kit} kit sounds), the page told so`);
check(bank.taken === 1 && bank.count === bank.samples && bank.kit === bank.kitSounds && bank.told[0].taken === 1,
  `a package's: taken, ${bank.count} samples and a kit of ${bank.kit} sounds (${bank.samples}, ${bank.kitSounds})`);
check(bank.same, "a sample of it in its slot, word for word (RSD)");
check(damaged.taken === -1 && damaged.count === none.count && damaged.kit === none.kit && damaged.told[0].taken === -1,
  `damaged: not taken, the build's own play (${damaged.count} samples), the page told so`);
done();
