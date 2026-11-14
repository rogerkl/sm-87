// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Roger Klaveness
// web/sm87emu.js, what another page of the site (Axs7) and the emulator share: a package's samples as the emulator
// keeps them, and the BroadcastChannel between them (node has one): an emulator switched on is heard, told of samples
// kept for it, and gone when its page goes. Keeping them (IndexedDB) and opening the emulator's window need a browser:
// test/run.sh doesn't, they were tried by hand in Chromium.
//   node test/sm87emu_test.mjs build/test/bank
import { readFileSync } from "node:fs";
import { resolve } from "node:path";
import { CHANNEL, EMU_ID, emuAnnounce, emuHolds, emuSamples, emuUrl, emuWatch } from "../web/sm87emu.js";
import { Sm87BankError, Sm87Package } from "../web/sm87bank.js";

const dir = process.argv[2] || "build/test/bank";
const file = (n) => new Uint8Array(readFileSync(resolve(dir, n)));
let checks = 0, fails = 0;
const check = (ok, what) => { checks++; if (!ok) { fails++; console.log(`FAIL ${what}`); } };
const settle = () => new Promise((r) => setTimeout(r, 50));

const k = emuSamples(file("baked.fwsc"), "baked.fwsc");
const pkg = new Sm87Package(file("baked.fwsc"), "baked.fwsc");
check(k.name === "baked.fwsc" && k.samples === 3 && k.kit === pkg.kit().sounds.length && k.bank instanceof ArrayBuffer &&
  Buffer.compare(Buffer.from(k.bank), Buffer.from(pkg.bank())) === 0, "a package's samples: its bank, what it holds");
check(emuHolds(k) === `3 samples, a drum kit of ${k.kit} sounds` && emuHolds({ samples: 1, kit: 0 }) === "1 sample",
  `said: "${emuHolds(k)}"`);
let threw = null;
try { emuSamples(file("orig.fwsc"), "orig.fwsc"); } catch (e) { threw = e; }
check(threw && !(threw instanceof Sm87BankError) && threw.message === "orig.fwsc has no samples in it", "none in it");
threw = null;
try { emuSamples(new Uint8Array(5000), "junk.fwsc"); } catch (e) { threw = e; }
check(threw instanceof Sm87BankError && threw.code === "not-package", "not a package");
check(emuUrl("https://rogerkl.github.io") === "https://rogerkl.github.io/sm-87/emu/" && EMU_ID === "sm87" &&
  CHANNEL === "sm87-emu", "the emulator's address and keys");

// none running: not heard; one switched on: heard, told, gone
const watch = emuWatch();
await settle();
check(!watch.running(), "no emulator: none running");
let told = 0, toldOther = 0;
const stop = emuAnnounce(EMU_ID, () => told++);
const other = emuAnnounce("another", () => toldOther++);
await settle();
check(watch.running(), "switched on: heard");
watch.tell();
await settle();
check(told === 1 && toldOther === 0, `told: it starts again (${told}), another app's emulator not (${toldOther})`);
const late = emuWatch();                  // a page opened after it: asks, and it answers
await settle();
check(late.running(), "a page opened later hears it (who: here)");
const ch = new BroadcastChannel(CHANNEL);
ch.postMessage({ t: "gone", id: EMU_ID, emu: "x" });
await settle();
check(late.running(), "another emulator gone: this one still running");
stop();                                   // (as its page goes)
await settle();
check(!watch.running() && !late.running(), "gone: none running");
other();
[watch, late].forEach((w) => w.close());
ch.close();
console.log(`sm87emu_test: ${checks - fails} of ${checks} checks passed`);
process.exit(fails ? 1 : 0);
