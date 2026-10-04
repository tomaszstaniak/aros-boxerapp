# BOXTEST - DOS test fixture

A small 16-bit real-mode `.COM` program that automated tests run inside the
emulator (DOSBox 0.74 core in the Boxer port). Every mode writes a
deterministic plain-ASCII log with CRLF line endings into the current
directory, so tests compare files and never read the screen. Lines are also
echoed to the console for a human observer.

Licence: original code of the aros-boxerapp project, under the project's
licence. It contains no third-party code.

## Build

```sh
./build.sh            # needs nasm; NASM=/path/to/nasm to override
```

Output: `<project>/build/boxtest/BOXTEST.COM` (generated, not versioned). The
script prints the nasm version, size and SHA-256. Recorded build:

- NASM 3.02 (compiled on Jun 29 2026), `nasm -f bin`
- 2026-10-02 stage 2c (GFX12 added): `BOXTEST.COM`, 3839 bytes,
  SHA-256 `4e32e8dd5d709f77cc071f8ca406736a19609283594543ab1186d9f10237075a`
- 2026-10-02 (TIME, GFX added): `BOXTEST.COM`, 3743 bytes,
  SHA-256 `5cf792d7a0af10a4632cd80d6ff5d75d2721bd505802835a85284c06f3e5c0a9`
- before: 3165 bytes,
  SHA-256 `7a4c541bcb1e0a4cf83806207416eb18d4264b406bd36fa04252c76e3a032782`

Two consecutive builds produced the same hash. 8086 instruction set
(`cpu 8086`).

## Usage

```
BOXTEST KEYS | MOUSE | ADLIB | SB | SAVE | ALL | TIME | GFX | GFX12
```

The first argument is matched case-insensitively. No argument or an unknown
one prints usage and exits with errorlevel 1. No log is written then.

| Mode  | Interactive | Ends on | Log file(s) |
|-------|-------------|---------|-------------|
| KEYS  | yes | ESC, or 60 s with no key | `BOXKEYS.LOG` |
| MOUSE | yes | right button, ESC, or 60 s idle | `BOXMOUSE.LOG` |
| ADLIB | no  | 35 s of tones, then the SB check | `BOXADLIB.LOG`, `BOXSB.LOG` |
| SB    | no  | about 0.5 s (2 s timeout) | `BOXSB.LOG` |
| SAVE  | no  | immediately | `BOXSAVE.LOG`, `SAVE.DAT` |
| ALL   | no  | SB then SAVE | `BOXSB.LOG`, `BOXSAVE.LOG`, `SAVE.DAT` |
| TIME  | yes | ESC, or 280 lines (about 280 s emulated) | `BOXTIME.LOG` |
| GFX   | yes | any key, or 60 s | `BOXGFX.LOG` |
| GFX12 | yes | any key, or 60 s | `BOXGFX12.LOG` |

ALL is the non-interactive subset that finishes within a few seconds. It
leaves out ADLIB because ADLIB runs for 35 s by design.

## Errorlevels

When several conditions apply in one run, the highest value is returned.

| Code | Meaning |
|------|---------|
| 0 | success |
| 1 | usage: no argument or unknown mode |
| 2 | file I/O: a log, `SAVE.TMP` write, or the rename failed |
| 3 | hardware: no mouse driver, no AdLib, DSP reset/version failed, or no DMA IRQ |
| 4 | `SAVE.DAT` corrupt (left unchanged) |
| 5 | KEYS/MOUSE: 60 s with no input (`END TIMEOUT`) |
| 6 | KEYS/MOUSE: log buffer (8000 bytes) nearly full (`END FULL`) |

In ADLIB mode the SB part can raise the code to 3 even when the AdLib
was found; the logs show which part failed.

## Log formats

Every log starts with `BOXTEST 1.0 <MODE>`. Hex numbers are upper case and
fixed width: 2 digits for a byte, 4 for a word, 8 for a dword. A single space
separates fields. Every line ends with CR LF, including the last.

### BOXKEYS.LOG

```
BOXTEST 1.0 KEYS
K <scan:2> <ascii:2>        one line per key, read with INT 16h AH=10h
END ESC | END TIMEOUT | END FULL
```

The ESC key itself is logged (`K 01 1B`) before `END ESC`. AH=10h is the
enhanced read, so grey arrows report ASCII `E0`. Sample, after typing `a`
and then ESC (`expected/BOXKEYS.a-esc.LOG`):

```
BOXTEST 1.0 KEYS
K 1E 61
K 01 1B
END ESC
```

### BOXMOUSE.LOG

```
BOXTEST 1.0 MOUSE
DRIVER PRESENT BUTTONS <bx:4>   INT 33h AX=0 result; FFFF means two buttons
M <x:4> <y:4> <buttons:2>       initial state, then every change
END RIGHT | END ESC | END TIMEOUT | END FULL
```

Without a driver: `DRIVER ABSENT` and errorlevel 3. Coordinates are the
driver's virtual screen (0..639 x 0..199 in text mode on DOSBox). Bit 0 is
the left button, bit 1 the right. Changes are found by polling INT 33h
AX=3, so a click shorter than one poll can be missed; tests should hold a
button for at least one emulated frame. Exact coordinates depend on the
injected motion, so tests should compare the structure and the
button-transition lines.

### BOXADLIB.LOG

```
BOXTEST 1.0 ADLIB
DETECT PRESENT | DETECT ABSENT
START TICK <ticks:8>      BIOS 0040:006C at the first note
END TICK <ticks:8>
ELAPSED TICKS <n:4>       expected 027D (637 = 35 s); 027E if a tick lands
                          between the loop exit and the read
NOTES <n:4>               expected 0024 (36 note starts)
```

Detection is the standard timer test: reset both timers, start timer 1
at FFh, read status at 388h before (top 3 bits 000) and after 80 us
(top 3 bits 110). When present, channel 0 plays C4 D4 E4 F4 G4 A4 B4 C5
repeatedly, one note per 18 ticks (about 1 s), for 637 ticks. Only the
`DETECT ABSENT` line is logged when absent. The START/END tick values
differ every run: compare with `expected/BOXADLIB.masked.LOG`, where
`*` matches any hex digit. A run that crosses midnight (when the BIOS
counter resets) is not supported.

### BOXSB.LOG

```
BOXTEST 1.0 SB
CONFIG A=<base:4> I=<irq:2> D=<dma:2> SOURCE=ENV|DEFAULT
DSP RESET OK | DSP RESET FAIL
DSP VERSION <major:2><minor:2> | DSP VERSION FAIL     (only after RESET OK)
PLAYBACK IRQ FIRED | PLAYBACK IRQ TIMEOUT | PLAYBACK UNSUPPORTED | PLAYBACK SKIPPED
```

Configuration comes from `BLASTER` (`A` hex, `I` and `D` decimal), and
the defaults A=220 I=7 D=1 apply otherwise. IRQ and DMA are printed in hex.
Playback: a 500 Hz square wave, 4000 bytes at 8000 Hz, sent by 8-bit
single-cycle DMA (DSP command 14h). `FIRED` means the DMA-complete IRQ
arrived within 36 ticks. `UNSUPPORTED` means IRQ outside 3..7 (only the
master PIC is handled) or DMA above 3. `SKIPPED` follows a failed reset.
`TIMEOUT` and the two FAIL results give errorlevel 3.

DOSBox 0.74 with its default `sbtype=sb16` sets `BLASTER=A220 I7 D1 H5 T6`
and reports DSP 4.05. The expected log for that case is
`expected/BOXSB.dosbox074-sb16.LOG`:

```
BOXTEST 1.0 SB
CONFIG A=0220 I=07 D=01 SOURCE=ENV
DSP RESET OK
DSP VERSION 0405
PLAYBACK IRQ FIRED
```

### BOXSAVE.LOG and SAVE.DAT

`SAVE.DAT` is 14 bytes:

| Offset | Size | Content |
|--------|------|---------|
| 0  | 8 | ASCII `BOXSAVE1` |
| 8  | 4 | counter, little-endian |
| 12 | 2 | checksum: 16-bit sum of bytes 0..11, little-endian |

A file of any other length, or one with a wrong magic or checksum, is
corrupt.

```
BOXTEST 1.0 SAVE
SOURCE NONE | SOURCE SAVE.DAT | SOURCE SAVE.TMP
OLD NONE | OLD <n:8> | OLD CORRUPT
NEW <n:8>                                   (not written when corrupt)
RESULT OK | RESULT UNCHANGED | RESULT WRITE FAIL | RESULT RENAME FAIL
```

Procedure:

1. Load `SAVE.DAT`. If it is corrupt, log `OLD CORRUPT`, `RESULT UNCHANGED`,
   exit 4 and touch nothing.
2. If it cannot be opened, load `SAVE.TMP` (see step 5); if that is not
   valid either, start from 0 (`SOURCE NONE`, `OLD NONE`).
3. Write counter+1 to `SAVE.TMP` (create/truncate, write, close, check
   the byte count).
4. Delete `SAVE.DAT`. DOS rename (INT 21h AH=56h) fails when the target
   exists, so there is no atomic rename-over in DOS. A delete is
   needed first.
5. Rename `SAVE.TMP` to `SAVE.DAT`. If the program stops between steps 4
   and 5, only a valid `SAVE.TMP` is left. The next run takes the counter
   from it (`SOURCE SAVE.TMP`), so no increment is lost.

Persistence check across sessions: run N leaves counter N. Starting from no
file, the first run logs `expected/BOXSAVE.first.LOG` and leaves
`expected/SAVE.DAT.after-first` (byte for byte). The second run logs
`expected/BOXSAVE.second.LOG`:

```
BOXTEST 1.0 SAVE
SOURCE NONE
OLD NONE
NEW 00000001
RESULT OK
```
```
BOXTEST 1.0 SAVE
SOURCE SAVE.DAT
OLD 00000001
NEW 00000002
RESULT OK
```

### BOXTIME.LOG (added 2026-10-02)

```
BOXTEST 1.0 TIME
T <ticks:8> <hhmmss:6> <loops:8>   one line per 18 BIOS ticks
END ESC | END TIMEOUT | END FULL
```

`ticks` is BIOS 0040:006C at the end of the window; `hhmmss` the CMOS
clock read with INT 1Ah AH=02h (BCD, so it reads as decimal digits; in
DOSBox it is the host's time); `loops` how often a fixed polling loop ran
during the 18 ticks. Emulated ticks follow executed cycles, so `loops`
scales with the CPU speed setting; a host-side pause shows as an RTC jump
with only 18 ticks between lines. Errorlevel 5 on END TIMEOUT.

### BOXGFX.LOG (added 2026-10-02)

```
BOXTEST 1.0 GFX
MODE <before:2>        INT 10h AH=0Fh before switching (03 = 80x25 text)
MODE <graphics:2>      after INT 10h AX=0013h (13 = 320x200x256)
WAIT KEY | WAIT TIMEOUT
MODE <after:2>         after INT 10h AX=0003h
END
```

In mode 13h the program draws 16 colour bars (20 px each), a white border
and a white-outlined 120x100 box, which a 4:3 display shows as a square;
nothing is printed while in 13h. Errorlevel 5 on WAIT TIMEOUT.

### BOXGFX12.LOG (added 2026-10-02, stage 2c)

Same format as BOXGFX.LOG, with mode `12` (640x480x16) in place of `13`.
Only a white top and bottom row are drawn. Reason: in DOSBox 0.74 mode 13h
is doubled to the same 640x400 frame as 80x25 text, so GFX never changes the
frame size; 12h gives a 640x480 frame, so GFX12 makes the front end
reallocate its frame buffer on both switches (the freed-buffer regression).

## Verification status

The expected files were written from this specification. No DOS emulator
was available when the fixture was written (2026-10-01: no `dosbox`,
`dosbox-staging` or `dosbox-x` on the build Mac), so they are **not**
captured output, and **no mode has run yet**. Checks done: assembly
with NASM 3.02, a repeat build with the same hash, and a disassembly
review with `ndisasm`. The first DOSBox run should confirm each expected
file or replace it, with evidence recorded.
