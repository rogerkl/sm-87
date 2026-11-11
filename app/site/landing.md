# SM-87

SM-87 turns the M-VAVE FM-1 into a 1987 studio: an Alesis MMT-8 style sequencer of 100 parts and songs, playing an
Akai S700/X7000 style sampler and an Alesis HR-16 style drum machine. Record from the keys or from MIDI, send samples
over USB from [Axs7](https://rogerkl.github.io/Axs7/), and trade memory dumps with a real MMT-8 or HR-16.

![SM-87 playing a part: PART 00 BT 003 on the LCD, tracks 1 and 2 lit](manual/img/screen.png "SM-87 on the FM-1's screen")

This is version {{VERSION}}. **SM-87 hasn't run on an FM-1 yet**: it works in the emulator, which runs the same code,
so try it there first. You install it at your own risk, and the installer can put M-VAVE's own firmware back.

- [Try it in the browser](emu/). Play it right here, with sound, and send it samples from Axs7.
- [Install](install/) it from Chrome or Edge, with the FM-1 connected by USB.
- The [Manual](manual/) explains the sequencer, the sampler, the drum machine and MIDI.
- [Download {{PKG}}](firmware/{{PKG}}) to use the command-line installer: `python3 tools/fm1_install.py {{PKG}}`.
- Samples and a drum kit of your own, built into the firmware: [Axs7](https://rogerkl.github.io/Axs7/) does it, and
  installs it (its wiki's [SM-87](https://github.com/rogerkl/Axs7/wiki/SM-87) page).
- The [Source](https://github.com/rogerkl/sm-87) is under MPL-2.0. It's built on fm1-base and felucca-bsp, which come
  from Felucca by Leo Kuroshita, Hügelton Instruments.

M-VAVE and FM-1 are trademarks of their owners, and MMT-8, HR-16, S700 and X7000 of theirs. SM-87 is not affiliated
with M-VAVE, Alesis or Akai.
