# EmuTOS ROM

`etos512se.img` is EmuTOS 1.4 (512 KB, Swedish keyboard layout), a free,
open-source, GPLv2-licensed reimplementation of Atari TOS. It's redistributed
here so the game can be tested in Hatari without hunting down a ROM first —
unlike real Atari TOS, EmuTOS is free software and fine to ship.

Project home: https://emutos.sourceforge.io/

```sh
hatari --tos roms/etos512se.img --auto build/QUANOID.PRG
# or boot a release floppy image from releases/:
hatari --tos roms/etos512se.img --disk-a releases/qnoid003.st
```

See `EMUTOS-LICENSE.txt` for the full GPLv2 text.
