// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Roger Klaveness
// SM-87's own part of the emulator's page (fm1-base tools/build.py --web: built as app_own.js, after the parts of
// fm1.toml emu). "Send .syx" in the page's bar sends a SysEx file into the FM-1 as from its USB port: no MIDI ports
// and no virtual cable on the way, so none of a long dump is lost there. A file may hold any number of messages (an
// MMT-8's or HR-16's memory dump, a sampler's sample dumps and their parameters, as a SysEx librarian saves them); they
// go one at a time, with a pause after each as long as SM-87 needs to take it in (a sample dump into its flash). What SM-87 made of them shows
// on its screen; how far the file is, beside the button.
import { toDevice } from "./parts/midi.js";

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
}
