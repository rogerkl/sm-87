# The manuals

SM-87 follows the manuals of the machines it recreates. They are Alesis's and Akai's, under their copyright, so they
aren't in this repository. Each is listed here with what SM-87 takes from it and where a copy can be found (other
sites' copies, found 2026-11-11). The code's comments name them by the file names in the first column. Put your own
copies in `documentation/manuals/` to keep them beside the code: git leaves that folder out.

| Manual | What SM-87 takes from it | Where |
|---|---|---|
| Alesis MMT-8 Owner's Manual (`MMT-8_Manual.pdf`) | The sequencer: parts, tracks, songs, recording, and its functions (pages 4-16) | [ManualsLib](https://www.manualslib.com/manual/207506/Alesis-Mmt-8.html) |
| Alesis HR-16/HR-16B and MMT-8 User Manual, 72 pages (`ALESIS-HR16-HR16B-MMT8-User-Manual.pdf`) | The drum machine: pads, patterns, quantize, swing, step edit | [manualsdir](https://www.manualsdir.com/manuals/724207/alesis-hr-16b-mmt-8.html) |
| Alesis HR-16/HR-16B Service Manual 1.00, section 11.0 MIDI Implementation | The HR-16's SysEx dump ([hr16-sysex.md](hr16-sysex.md)) | [manualsdir](https://www.manualsdir.com/manuals/46570/alesis-hr-16.html?page=29) |
| Alesis MMT-8 MIDI System Exclusive Format (`MMT-8 SYSEX.odt`) | The MMT-8's memory dump (`app/mmt8/mmtdump.h`) | [A copy as a GitHub gist](https://gist.github.com/redconfetti/e10078eaf437b59d5c876bb1cd75deed) |
| Akai S700 Operator's Manual, 41 pages (`akai-s700-manual.pdf`) | The sampler: its parameters, programs and pages | [ManualsLib](https://www.manualslib.com/manual/3558284/Akai-S700.html) |
| Akai, MIDI system exclusive data format for S700, 1986 (`akai-s700-sysex-spec-1986.pdf`) | The sample dump, the sample parameters (SPRM) and the overall settings (OVS) | Posted to the Synth-diy mailing list in 1998. All SM-87 uses of it is in [akai-s700-x7000](akai-s700-x7000/README.md) |
| Alesis MidiVerb Instruction Manual (`MidiVerb_Manual.pdf`) | Nothing yet: for an effects section later | [manuals.online](https://manuals.online/alesis/music-pedal/midiverb/user-manual/25k3b) |

[mmt8-tape-dump-decoder.txt](mmt8-tape-dump-decoder.txt) points to mmt8utils, which found what the MMT-8's dump holds
beyond its document (the BCD beats + 1).
