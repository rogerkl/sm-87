// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Roger Klaveness
// SM-87's own part of the emulator's page (fm1-base tools/build.py --web: built as app_own.js, after the parts of
// fm1.toml emu). Two buttons in the page's bar:
//  - "Send .syx" sends a SysEx file into the FM-1 as from its USB port: no MIDI ports and no virtual cable on the way,
//    so none of a long dump is lost there. A file may hold any number of messages (an MMT-8's or HR-16's memory dump,
//    a sampler's sample dumps and their parameters, as a SysEx librarian saves them); they go one at a time, with a
//    pause after each as long as SM-87 needs to take it in (a sample dump into its flash). What SM-87 made of them
//    shows on its screen; how far the file is, beside the button.
//  - "Load .fwsc" takes a firmware package's samples (and drum kit), as Axs7 or tools/mkrom.py put them in, and the
//    FM-1 starts again with them in place of the emulator's own, as an FM-1 would once the package is installed: the
//    slots that held the samples built in before get the new ones, a sample sent in stays where it is. They are kept
//    in this browser until "Built-in samples" (a factory reset keeps them, as it keeps an FM-1's firmware). The
//    worklet's side puts them in before the FM-1 boots (app_worklet.js; s700/s7mem.c).
import { toDevice } from "./parts/midi.js";
import * as data from "./parts/data.js";
import { Sm87Package } from "./sm87bank.js";

/** the SysEx messages in a file's bytes, F0 .. F7 each; bytes between them left out, and a message cut short (no F7,
 *  or another status byte inside) */
export function syxFrames(bytes) {
  const frames = [];
  let start = -1;
  for (let i = 0; i < bytes.length; i++) {
    const b = bytes[i];
    if (b === 0xf0) start = i;
    else if (b === 0xf7) {
      if (start >= 0) frames.push(bytes.subarray(start, i + 1));
      start = -1;
    } else if (b & 0x80) start = -1;
  }
  return frames;
}

/** the pause after a message of n bytes: 1.8 s after a sample dump (66,632 bytes), about ten times what SM-87 needs
 *  to take one in and write it to the flash before the next (test/emu_test.mjs sends four in a row) */
export const pauseMs = (n) => 100 + Math.ceil(n / 40);

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const count = (n, one) => `${n.toLocaleString("en")} ${one}${n === 1 ? "" : "s"}`;

async function send(file, button, said, status) {
  let bytes;
  try {
    bytes = new Uint8Array(await file.arrayBuffer());
  } catch (e) {
    status(`${file.name} could not be read: ${(e && e.message) || e}`);
    return;
  }
  const frames = syxFrames(bytes);
  if (!frames.length) {
    status(`${file.name} has no SysEx message in it (F0 .. F7).`);
    return;
  }
  button.disabled = true;
  try {
    for (let i = 0; i < frames.length; i++) {
      said.textContent = frames.length === 1 ? `Sending ${file.name}…`
        : `Sending ${file.name}: ${i + 1} of ${frames.length}…`;
      toDevice(frames[i]);
      await sleep(pauseMs(frames[i].length));
    }
    const total = frames.reduce((a, f) => a + f.length, 0);
    said.textContent = `Sent ${file.name}: ${count(frames.length, "message")}, ${count(total, "byte")}.`;
  } finally {
    button.disabled = false;
  }
}

// ---------------------------------------------------------------- a firmware package's samples
const DB = "fm1-emu", STORE = "state";    // the data part's database (parts/data.js), under a key of SM-87's own

function db() {
  return new Promise((resolve, reject) => {
    const r = indexedDB.open(DB, 1);
    r.onupgradeneeded = () => r.result.createObjectStore(STORE);
    r.onsuccess = () => resolve(r.result);
    r.onerror = () => reject(r.error);
  });
}
async function op(mode, fn) {
  const d = await db();
  return new Promise((resolve, reject) => {
    const t = d.transaction(STORE, mode), q = fn(t.objectStore(STORE));
    t.oncomplete = () => { d.close(); resolve(q && q.result); };
    t.onerror = t.onabort = () => { d.close(); reject(t.error); };
  });
}
const bankKey = (id) => `${id}:bank`;
/** the package's samples kept: { name, bank (ArrayBuffer), samples, kit (its sounds) }, or undefined */
const kept = (id) => op("readonly", (s) => s.get(bankKey(id)));
const keep = (id, k) => op("readwrite", (s) => s.put(k, bankKey(id)));
const forget = (id) => op("readwrite", (s) => s.delete(bankKey(id)));

/** a package's samples as they are kept: Sm87BankError (its message says why), or Error for one without samples */
export function fromPackage(bytes, name) {
  const pkg = new Sm87Package(bytes, name), bank = pkg.bank();
  if (!bank) throw new Error(`${name} has no samples in it`);
  const c = pkg.contents();
  return { name, bank: bank.buffer, samples: c.samples.length, kit: c.kit ? c.kit.sounds.length : 0 };
}

const holds = (k) => `${count(k.samples, "sample")}${k.kit ? `, a drum kit of ${count(k.kit, "sound")}` : ""}`;

/** the emulator's data area kept as it is now (a sample sent in just before), then the page again: the FM-1 starts
 * with what is kept (after a while without an answer, as it is) */
async function startAgain(id) {
  try {
    const now = await Promise.race([data.current(), new Promise((r) => setTimeout(() => r(null), 1500))]);
    if (now && now.data) await data.keep(id, now.data);
  } catch { /* (as it was last kept) */ }
  data.stop();
  location.reload();
}

let pkgSaid = null, pkgKept = null;
function pkgShow(taken) {
  if (!pkgSaid) return;
  const k = pkgKept;
  pkgSaid.button.hidden = !k;
  pkgSaid.text.dataset.taken = String(taken);   // (the firmware's word: 1 taken, -1 not, 0 none given or not yet)
  pkgSaid.text.textContent = !k ? "" : taken === -1 ? `${k.name}'s samples don't check: the built-in ones play.`
    : `Samples: ${k.name} (${holds(k)}).`;
}

// the worklet's word, once the FM-1 is on: whether it took the package's samples (app_worklet.js)
export function message(m) {
  if (m && m.sm87bank) pkgShow(m.sm87bank.taken);
}

function attachPackage(ctx, bar) {
  const status = ctx.status || (() => {});
  if (!ctx.beforeLoad || typeof indexedDB === "undefined")
    return;                               // (an fm1-base without the hook: patches/fm1-base-emu-app-part.patch)
  ctx.beforeLoad(async (msg) => {
    try {
      const k = await kept(ctx.id);
      pkgKept = k && k.bank instanceof ArrayBuffer ? k : null;
    } catch (e) {
      status(`The samples loaded before could not be read: ${(e && e.message) || e}`);
    }
    pkgShow(0);
    return pkgKept ? { ...msg, parts: { ...msg.parts, app: pkgKept.bank } } : msg;
  });
  const box = document.createElement("span");
  const load = document.createElement("button");
  const input = document.createElement("input");
  const builtIn = document.createElement("button");
  const said = document.createElement("span");
  load.type = builtIn.type = "button";
  load.textContent = "Load .fwsc";
  load.title = "Play a firmware package's samples and drum kit, as Axs7 or tools/mkrom.py put them in: the FM-1 " +
    "starts again with them";
  builtIn.textContent = "Built-in samples";
  builtIn.title = "Forget the package's samples: the FM-1 starts again with its own";
  builtIn.hidden = true;
  input.type = "file";
  input.accept = ".fwsc,.FWSC";
  input.hidden = true;
  said.setAttribute("role", "status");
  box.className = "fwsc";
  box.append(load, input, " ", builtIn, " ", said);
  bar.insertBefore(box, bar.querySelector(".cpu"));
  pkgSaid = { button: builtIn, text: said };
  load.addEventListener("click", () => input.click());
  input.addEventListener("change", async () => {
    const file = input.files && input.files[0];
    input.value = "";                     // (the same file again is a change too)
    if (!file) return;
    let k;
    try {
      k = fromPackage(new Uint8Array(await file.arrayBuffer()), file.name);
    } catch (e) {
      status(`${(e && e.message) || e}: nothing changed.`);
      return;
    }
    load.disabled = builtIn.disabled = true;
    said.textContent = `Starting again with ${file.name} (${holds(k)})…`;
    try {
      await keep(ctx.id, k);
    } catch (e) {
      load.disabled = builtIn.disabled = false;
      status(`${file.name}'s samples could not be kept in this browser: ${(e && e.message) || e}`);
      pkgShow(0);
      return;
    }
    await startAgain(ctx.id);
  });
  builtIn.addEventListener("click", async () => {
    load.disabled = builtIn.disabled = true;
    said.textContent = "Starting again with the built-in samples…";
    try { await forget(ctx.id); } catch { /* (none kept) */ }
    await startAgain(ctx.id);
  });
}

export function attach(ctx) {
  const bar = document.querySelector(".bar");
  if (!bar) return;
  const box = document.createElement("span");
  const button = document.createElement("button");
  const input = document.createElement("input");
  const said = document.createElement("span");
  button.type = "button";
  button.textContent = "Send .syx";
  button.title = "Send a SysEx file into the FM-1, as over USB: an MMT-8 or HR-16 dump, samples";
  input.type = "file";
  input.accept = ".syx,.SYX";
  input.hidden = true;
  said.setAttribute("role", "status");
  box.className = "syx";
  box.append(button, input, " ", said);
  bar.insertBefore(box, bar.querySelector(".cpu"));
  button.addEventListener("click", () => input.click());
  input.addEventListener("change", () => {
    const file = input.files && input.files[0];
    input.value = "";                     // (the same file again is a change too)
    if (file) send(file, button, said, ctx.status || (() => {}));
  });
  attachPackage(ctx, bar);                // (after it in the bar)
}
