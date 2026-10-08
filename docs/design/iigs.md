# The Apple IIgs

Moved out of CLAUDE.md; this is the detail behind the rules summarised there.

## The 65816

`CPU65816` (`cpu/65816/`) is a separate class from `CPU6502`, and deliberately:
a 65816 has a 24-bit bus, 16-bit registers whose width changes at runtime, a
direct page and a stack that can sit anywhere in bank zero, separate banks for
code and data, and a second operating mode. Folding that into `CPU6502` would
put a width test on every load, store and arithmetic operation in the hottest
loop of a //e to serve a machine a //e is not. A machine is built from one or
the other.

**Cycle counts, not the cycle pattern.** `CPU6502` models which cycle of an
instruction touches which address, because a //e's video reads the bus during
those cycles. A IIgs's video does not read the 65816's bus at all — it reads the
Mega II's, on the other side of the machine — so this core counts cycles and
does not pretend to place them.

**Three rules in it are worth knowing before changing anything:**

- **Widths belong to the processor, not to the addressing mode.** Every
  operation takes an effective address and reads its own operand at whatever
  width the flags currently say, which is why `opADC` takes a `uint32_t` and
  not a value.
- **Two kinds of address behave differently at the top of a bank.** An
  immediate operand comes from the program bank and a direct page or stack
  operand from bank zero, and neither bank ever increments; an operand reached
  through the data bank does cross into the next one. They are the same number,
  so the addressing mode says which it produced (`operandWrapsInBank_`) and the
  next access consumes the answer.
- **The instructions a 6502 never had ignore emulation mode's stack wrap.**
  PHD, PLD, PEA, PEI, PER, PLB, JSL and RTL walk the stack pointer through all
  sixteen bits and it is forced back into page one at the end, which is why PLD
  with the pointer at `$01FE` really does read its high byte from `$0200`.

**It is verified against 5.1 million recorded states from a real 65816**
(SingleStepTests/65816): every opcode, both modes, 10,000 vectors each,
registers, memory and cycle count. `tests/conformance/test_65816_vectors.cpp`
runs them and skips unless `A2E_65816_VECTORS` points at the files, which are
3GB and not in the repository. Four bugs came out of it that the unit tests did
not find: the indexed page-cross cycle applies when the index is 16 bits wide
*or* crosses a page rather than only crossing; writes and read-modify-writes
never pay it; decimal mode takes V from the value before the top digit's
correction; and the two bank-wrap rules above.

## A IIgs's memory

`IIgsMemory` (`core/iigs/iigs_memory.*`) is the 24-bit map, and **the Mega II
side of it is an `MMU`** — the same class a //e is built from, constructed with
the IIgs profile. Banks `$E0`/`$E1` are its main and auxiliary RAM, `$C000-$CFFF`
in the four banks that see it are its soft switches, and `$D000-$FFFF` is its
language card. That is not a convenience: a IIgs really does contain a //e, and
when the video is written it will read that MMU exactly as `Video` already does.

Three things in it are worth knowing:

- **Shadowing is a copy, not a redirection.** A write to a display region of
  bank `$00` or `$01` lands in fast RAM *and* is copied to `$E0`/`$E1`, because
  the video only ever looks at the Mega II's side. Which regions those are is
  the `$C035` register, and its bits read backwards: a set bit turns a region's
  shadowing **off**. **The copy is a Mega II cycle and the processor waits for
  it**, so a shadowed write costs a slow access whatever the speed register
  says (`shadowWrite` reports whether it copied and `write` charges it, as
  GSSquared's `megaiiWrite` does). Charged as fast, drawing ran up to a tenth
  faster than a real machine beside it.
- **`$C035` bit 6 changes what an address is**, rather than where a write also
  goes: with I/O and language card shadowing inhibited, banks `$00`/`$01` are
  plain RAM from `$C000` up, which is how a program gets a contiguous 128KB.
- **Bank `$00` still obeys the //e's memory switches, and they send it into
  bank `$01`.** A IIgs is a //e whose main RAM is bank `$00` and whose
  auxiliary RAM is bank `$01`, so RAMRD and RAMWRT move `$0200-$BFFF`, ALTZP
  the zero page, stack and language card, and 80STORE with PAGE2 (and HIRES)
  the text and first hi-res pages — overriding RAMRD/RAMWRT there.
  `IIgsMemory::effectiveBank` is the rule, applied before the write lands and
  before it shadows, so a bank `$00` write that belongs in `$01` reaches `$E1`.
  The 80-column firmware depends on it: a line's even columns go to the text
  page with 80STORE and PAGE2 on, and a machine that left them in bank `$00`
  drew every other column blank. Bank `$01` is never redirected. This is the
  same rule GSSquared applies in `calc_aux_read`/`calc_aux_write`.
- **`$C068` (STATEREG) is eight of the //e's soft switches in one byte**, and
  writing it drives those switches through their own addresses so everything
  watching them sees the change the usual way. It has no bit for the language
  card's *write* latch, so `setStateRegister` reads that off the machine and
  preserves it: changing the memory map must not quietly write-protect the
  card, or quietly unprotect it.

## A IIgs that boots

`IIgsMachine` (`core/iigs/iigs_machine.*`) is to a IIgs what `Emulator` is to
the other three: it owns the CPU, the memory and the video, and runs them. The
video is the //e's `Video` class reading the Mega II's MMU, because that is
what a IIgs's //e-mode picture is drawn by.

**The machine has two clocks, and the slow one lives in `IIgsMemory`.** The
65816 runs at 2.8MHz until it reaches the Mega II, and that *access* is
stretched to a 1.023MHz cycle — so the clock ticks inside the memory, as each
slow-side access happens, and `IIgsMachine` adds the rest of the instruction
afterwards at whatever the speed register says. Keeping it there is what lets
it advance *during* an instruction: a disk read loop is a few cycles with one
access in it, and a drive whose clock only moved between instructions sees that
loop in lumps.

**The rest of the instruction is the rest of it.** `takeSlowAccesses()` returns
how many of an instruction's cycles went to the slow side, and `step()`
subtracts them before converting what is left — a cycle spent waiting on the
Mega II is not also a cycle spent running. Charging both halves is easy to do
and invisible until something is timed against it: the boot ROM's read loop
runs out of bank `$00`'s I/O space, so *every* cycle of it is a slow access,
and it came out at thirteen cycles where the disk expects seven.

**A Mega II access from the fast side waits for the slow clock, and fast RAM
is refreshed.** `IIgsMemory::slowAccess` charges an access to the Mega II
the rest of the slow cycle in progress and then a whole one, because the
processor stops at the slow clock's edge; `IIgsMachine::slowCyclesFor`
stretches fast cycles run from RAM by one refresh cycle in every ten (2.8MHz
comes out near 2.5), and code in ROM goes at the full rate. Both are
GSSquared's rules, and the Apple IIgs Diagnostic's speed test is the check: it
counts a nine-cycle loop between two changes of `$C02E` and accepts 25 or 26
at fast speed and 14 or 15 at slow, which is what the machine now counts
(`test_iigs_boot.cpp` runs the same loop). `$C02E`/`$C02F` are the Mega II's
counters as the IIgs exposes them — vertical `$100-$1BF` over the picture and
`$1C0-$1FF` then `$FA-$FF` through blanking, horizontal 0 then `$40-$7F` — from
a beam query the machine installs. `$C019` covers the same blank as a //e's
(from line 192, in Super Hi-Res too) the other way up: bit 7 is high in it on
a IIgs (`caps.vblHighInBlank`). Apple IIgs Technical Note #40 is the source;
the Hardware Reference's soft switch table ("1 = not VBL") is wrong for the
IIgs. Text page 2 is not shadowed: the original logic board, whose 128KB ROM
is the one we run, has no shadowing for it, and `$C035` bit 5 exists only on
the 1MB board (Hardware Reference, chapter 2). `$C046`'s flags say what happened whether
or not it was enabled, and only `$C047` clears them; the diagnostic's handler
switches VBL off before it looks and must still find the flag.

**`$C036`'s bottom four bits are a veto on the fast clock, not a speed
setting.** They are slot motor detect, one each for slots 4 to 7, and a drive
turning in an enabled slot drops the whole machine to 1.023MHz until it stops.
`IIgsMemory::isFastSpeed()` asks a `SlotMotorQuery` the machine installs, so
the memory needs to know nothing about drives. This is what makes a Disk II
readable at all: the controller holds a finished byte for about two bit cells,
and at 2.8MHz the firmware's poll comes round three times per byte and reads
half of them twice.

**Control-Reset and the power switch are two different things on a IIgs, as
on a //e.** `IIgsMachine::warmReset()` is the RESET line: the registers, the
Mega II's switches and the chips that take the line go back to their reset
values and the CPU takes the vector from ROM, but the fast RAM, the Mega II's
RAM and the disk in the drive are exactly as they were — so the firmware finds
its warm-start bytes at `$03F2` and restarts what was running. `reset()` is the
power switch, and it now clears the *fast* RAM too: it used to leave it, and
since the firmware decides between a cold and a warm start by what it finds in
bank `$00`, every "Reboot" was a warm one. `_warmReset` used to call `reset()`
for a IIgs, so Control-Reset was a reboot. `test_iigs_boot.cpp` pins both.

**`$C029` bit 5 shows double hi-res in black and white.** The System 2
Finder and the 80-column desktop programs draw a 560-dot double hi-res
picture with Super Hi-Res *off* and this bit *set*, and the VGC shows the dots
as they are; decoding them into colour instead — which the Mega II's video did,
knowing only bit 7 — fringed every letter red and green. `IIgsMemory` reports
the register through `setNewVideoCallback` and `Video::setDoubleHiResMonochrome`
sends a double hi-res line through the monochrome decoder, white on black; a
monochrome monitor still has the last word. `test_iigs_video.cpp` pins colour
without the bit, grey with it, and green on a green screen either way.

**A IIgs's text is drawn, not transmitted.** `$C022` (TCOLOR) holds the two
colours the VGC substitutes for lit and unlit text dots and the bottom nibble of
`$C034` holds the border — the top nibble of that address is the clock's, which
is why `$06` sets a blue border and starts no transaction. `Video::setTextColours`
is how that reaches the //e's video: a text line is decoded into those two
colours instead of through a receiver, and a machine that never calls it behaves
exactly as before. Monochrome still overrides it, because a monochrome monitor
has one phosphor whatever the machine sent.

**A IIgs's frame is the raster a monitor shows, border included.** The
Mega II's line is 65 cycles: 40 of picture, 12 of blanking, and 13 of border —
6 before the picture and 7 after; its frame is 262 lines: 200 of picture, 22 of
blanking, and 40 of border — 19 above and 21 below. Those are the cycles
GSSquared's scanner flags as border. **What is drawn is the part of that a
monitor's bezel does not hide**: three cycles either side and twelve lines
above and below, which keeps the border's shape and puts it at about the width
it has on the glass — every border cycle drawn made it a fifth of the picture's
width, which no monitor of the period showed. `iigs_spec.hpp` holds both sets
of numbers. At Super Hi-Res's 16 pixels a cycle the frame is therefore
**736x448** (lines doubled) with the 640x400 picture at (48, 24), and
`IIgsVideo` fills the rest with the colour in `$C034`'s bottom nibble — in
Super Hi-Res too, which used to fill the frame with no border at all. **The
//e's 560x384 goes in the same width, stretched to 640** (eight pixels for
every seven dots, linearly), because a text screen and a Super Hi-Res screen
are the same width on the monitor, **and centred in the 200 lines**
(`MEGAII_TOP`), because 192 is eight short of 200 and putting all eight at the
bottom made the bottom border deeper than the top. The profile's `text` rectangle says where the text
screen landed, so the host's text selection maps a pointer onto a cell through
it rather than assuming the text fills the frame, and its `aspect` says the
shape the monitor shows the frame at: a //e's 560x384 at its own ratio, as
always, and the IIgs raster at 4:3. `machineAspect()` drives the screen window
and a `--screen-aspect` CSS variable drives the full-page layout. The shared
framebuffer slot is sized 848x480, which holds it. `test_iigs_video.cpp` pins
the raster and `test_machine_profile.cpp` the profile.

**The picture is drawn as the beam passes, and only a finished frame is
shown.** `IIgsVideo::drawLinesTo` draws each of the frame's 262 lines as the
beam leaves it, from the machine as it is then: the border colour, then that
line of Super Hi-Res (its control byte and palette at that moment) or of the
Mega II's picture, whichever `$C029` says. So a palette or a border changed
part way down the screen shows where it changed. It draws into one buffer and
`finishFrame` swaps it to the one `frame()` returns, and `consumeFrameSamples`
counts frames the beam finished. The picture used to be composed whole when
the host asked for it, every 800 samples: a refill is about twenty cycles
longer than a frame, so the Mega II's half-drawn frame was shown with a split
that crept down the screen, and Super Hi-Res took whatever palette was there
at that instant for every line. `render()` still composes the whole screen
at once, for a paused machine (`forceRenderFrame`) and a restored state.
`test_iigs_video.cpp` pins both: no published picture is two frames, and a
palette changed at line 100 colours lines 100 to 199 only.

**The SCC is a real Z8530 with nothing plugged into it.** `IIgsSCC`
(`core/iigs/iigs_scc.*`) at `$C038-$C03B` is the register file behind the
command/data pair per channel, the transmitter and receiver with their
timing, local loopback and auto echo, the baud rate generator, and the
interrupt logic — because software exercises all of that without a cable.
The Diagnostic's Serial Internal Test writes every register and reads it
back, then arms the zero-count interrupt with the slowest time constant and
measures the interval between two of them: **the zero count comes every
`TC + 2` clocks of 3.6864MHz, not twice that**, because the generator's output
toggles at each zero and the baud rate is half the zero count. A generator
counting the output's period fell outside the window. It then sends bytes
round the local loop at 600 baud, polling RR1's all-sent and RR0's receive
bit. `IIgsMachine::step` advances the chip on the slow clock beside the
Ensoniq, and `IIgsMemory::interruptPending()` includes it, gated on WR9's
MIE. **A loopback cable is fitted between the two ports** —
`IIgsSCC::setLoopbackCable`, on by default because nothing else is ever
plugged in — crossing each port's transmit into the other's receiver and
its DTR into the other's CTS, which is what the External Serial Ports Test
asks for. **The transmitter is clocked from whatever WR11 selects**: the
crystal on RTxC, the generator, or the TRxC pin, then WR4's divider. The
Serial Crystal Test clocks a byte straight from the crystal at x64 and times
its all-sent at 174 microseconds; a transmitter that took the generator's rate
regardless took a fifth of a second. `test_iigs_devices.cpp` pins the
register file, the zero count, the loop, the cable, the clock source and the
interrupts.

**The clock chip is a serial line, and it answers on the read transfer.**
`$C033` is the byte and `$C034` drives it: bit 7 starts a transfer, bit 6 is
its direction (set, the chip supplies the byte; clear, it takes the one in
`$C033`), and bit 5 holds the chip selected across the transfers of one
transaction — the firmware's driver drops it after every one, and
`IIgsClock` goes back to expecting a command when it does. A transaction is a
command byte and then a data byte, each its own transfer, with the 256 bytes of
battery RAM addressed across two command bytes. What matters is *when* the
chip answers a read: on the read-direction transfer, not when it sees the
command. The driver — the same routine in the ROM and in the IIgs Diagnostic —
stores whatever it is holding to `$C033` before every transfer, the read of
the data byte included, so a chip that answered early had its answer
overwritten and then took the junk as its next command. The Diagnostic's Clock
RAM Test read every clock byte back as that junk, retried 256 times, and
dropped into the monitor. The seconds are seeded from the host's clock when
the chip is made and then counted by the machine: `IIgsMemory::tickClocks`
ticks the chip on the same second that raises the VGC's one-second interrupt,
because on the real machine that interrupt *is* the chip's tick. The
Diagnostic writes `$FFFFFFFF` and waits for the roll-over, which a clock
reading the host would never show a machine running faster than real time.
`test_iigs_devices.cpp` pins the protocol, the junk, and the tick.

**Banks `$00` and `$01` are 64K of fast RAM each, language card included.**
Their `$D000-$FFFF` is the bank's own memory in the shape of a //e's card, with
the second `$D000` bank being the 4K hidden under `$C000`; the Mega II's card
belongs to `$E0`/`$E1` alone, and `MMU::readLanguageCardRAM`/`writeLanguageCardRAM`
take main-or-aux as an argument for it. `IIgsMemory::fastLanguageCardAddress` is
the rule for the fast side. Three earlier models of this each broke GS/OS in a
way that looked like something else — see `wiki/Apple-IIgs.md`.

**Vectors are pulled from ROM whatever the map shows** — the FPI answering the
65816's VPB line. `CPU65816::setVectorReadCallback` is the hook; nothing on a
IIgs writes a vector into RAM, and GS/OS copies its kernel over `$D000-$FFFF`
with interrupts enabled.

**Interrupts.** `IIgsMemory::interruptPending()` is the OR of the ADB
(`$C027`, full/enable pairs), the VGC (`$C023`: the scan line is enable bit 1
with flag bit 5, the one-second tick enable bit 2 with flag bit 6; both
acknowledged through `$C032`, bit 5 low for the scan line and bit 6 low for
the second), and the Mega II (`$C041`/`$C046`/`$C047`, VBL and quarter-second);
the CPU samples it every instruction. The scan-line interrupt is asked for by
bit 6 of a Super Hi-Res line's control byte and raised by
`IIgsMachine::raiseScanLineInterrupts` as the beam finishes that line.
QuickDraw II draws the mouse pointer from it, through the handler it installs
at `$E1:0028` — the vector the ROM's `AND #$22 / LSR / LSR` dispatch reaches —
so with the two VGC pairs swapped the pointer was redrawn once a second, on the
tick that arrived through QuickDraw's vector instead. The ROM's manager asks the SCC *first*
and the Ensoniq's `$E0` *last*, so `$C038-$C03B` must answer RR3 with nothing
pending until something is, and register `$E0` reads active-low "none" —
either one wrong is *Unclaimed Sound Interrupt*. `$C071-$C07F` map the ROM's vector firmware into the I/O page.

**The 256 bytes of battery RAM are the host's to keep**, because a real
machine's battery keeps them and the Control Panel's settings only mean
anything if they survive. `src/js/machine/iigs-battery-ram.js` restores them
before the machine runs and writes them back when the core says they changed
(`_batteryRamChanged()`, one boolean, rather than comparing 256 bytes).
**They go out and come back exactly as the firmware wrote them, checksum
included**: the firmware validates that checksum before trusting the contents
and its algorithm has not been worked out here, but it never needs to be as
long as nothing alters the bytes. Alter one and the firmware writes its own
defaults over the lot, which is what a machine with a dead battery does on
every start and is what this machine did before. GSSquared keeps its battery
RAM in a file the same way and also does not compute the checksum.
`test_iigs_boot.cpp` proves the firmware then leaves them alone: not one byte
written on the second start.

**The ADB controller has to take exactly the bytes each command carries, and
has to answer a bus transaction in a frame.** The firmware writes a command and
then its arguments to `$C026`, so a command whose argument count is wrong leaves
its own bytes to be read as commands: read-memory takes *two* bytes because its
address is sixteen bits, a Listen takes two, and the undocumented `$12`/`$13`
take two. A command above `$1F` addresses the bus rather than the controller —
high nibble the command, low nibble the device, `$8n-$Bn` Listen registers 0 to
3 and `$Cn-$Fn` Talk, with `$70-$73` the controller's own "stop polling that
device". A Talk is answered with a header byte with bit 7 set whose bottom three
bits are one *less* than the count that follows, because the firmware's read
loop counts down to one after an INY; a device with nothing to say still sends
the header. Register 3 is what the firmware enumerates the bus with, and answers
with the device's address and a handler byte. Get any of it wrong and the
firmware sits in a read loop until its own counter expires, reports the
transaction incomplete, and unwinds through a tool error path whose `RTL` lands
in the middle of an instruction in the Tool Locator — so the boot ends in the
monitor, with the message hidden behind whatever Super Hi-Res was showing. That
is what stalled a System 6.0.4 install disk at its splash screen.
`test_iigs_devices.cpp` pins the counts, the frame and the empty answer.

**`$C025` says which modifier keys are down**, and it used to read zero
whatever was held. The Event Manager reads it on every event, so a machine
answering zero has no shift-click and no command-key menu shortcut.
`IIgsMachine::reportModifiers` fills it in from the browser's own flags plus
the Apple keys the `Keyboard` already tracks for a //e's pushbuttons, and the
latch in bit 5 comes up on any change and clears on a read, which is how a
program tells "nothing held" from "pressed and released between two polls".

**The Control Panel's hotkey still does not work, and the reason is now
known rather than guessed.** Control-Open-Apple-Escape reaches the machine
correctly — the modifiers read `$A2` and the key latches as `$9B` — but
nothing running looks at it. Measured under ProDOS with a program polling for
a key: `$C000` read 111,899 times while the hotkey was held, `$C025` read
zero times. The ADB microcontroller is not the answer either: `$C026`'s
sequence-detect bits are Control-Command-Reset and Control-Command-Delete and
there is no bit for Escape (Table 6-3 of the Hardware Reference). That leaves
the firmware's interrupt-driven Desk Manager, which never starts here — the
ADB status register reads `$00` after boot, so the keyboard interrupt it would
need was never enabled. Finding what enables it is where the next attempt
should start.

**How much fast RAM a IIgs has is a user choice**, from 256K to 8M, in the
Machine menu and remembered in localStorage. `_setIIgsMemoryKB` rebuilds the
machine, as switching machines does, and `main.js` applies the remembered size
*before* the machine is built rather than after. `clampFastRamSize` rounds to
whole 64K banks; banks above what is fitted must not answer, because the
firmware sizes memory by writing to one and reading it back. System 6.0.4 boots to the Finder
with a working mouse; the built-in SmartPort in slot 5 serves it hard drive images
through GS/OS's extended calls.

**A IIgs's seven slots each hold two things, and `$C02D` says which answers.**
Chapter 8 of the Hardware Reference: every slot is a real socket *and* has a
built-in device assigned to it, and "only one device can be selected at a time
for each slot". The register moves **both the ROM and the I/O** for slots 1, 2,
5, 6 and 7; for slot 4 it moves the ROM only, because "I/O space for slots 3
and 4 is always enabled"; and slot 3 is not in the register at all — bit 3 is
reserved and its ROM follows the //e's own SLOTC3ROM. `IIgsMemory` holds the
user's cards in `slotCards_`, separate from the Mega II's slots where the
machine's own parts live, and `slotIOIsCard`/`slotRomIsCard` are those rules.
A slot switched to a card that is not there reads the floating bus, and the
machine's own device must *not* answer in its place.

**The Control Panel's setting is held against the firmware, because we are the
Control Panel.** On a real machine that choice lives in battery RAM and the
firmware copies it into `$C02D` on every start. We cannot write that battery
RAM — the checksum algorithm is not known here and the firmware would rewrite
its defaults — so `IIgsMemory::overrideSlot` remembers the bits the user
actually chose and a firmware write to `$C02D` is *merged* rather than obeyed
for those. A slot nobody has touched is left entirely to the firmware, which is
what keeps a machine with no cards behaving exactly as before. Without this a
card fitted before boot was ignored from the first reset onwards.

**`$C800-$CFFF` is one window seven cards share**, and a card claims it by
having its own `$Cn00` read; `$CFFF` hands it back, as INTC8ROM does on a //e.
Without it a card with more firmware than 256 bytes — a Super Serial Card, a
Thunderclock, a parallel card — has nowhere to put the rest of it.

**A card in a socket runs on the slot bus's clock, not the 65816's.**
`IIgsMachine::step` hands every fitted card the slow-clock time the
instruction covered, the same span the Ensoniq and the SCC advance by,
because a slot's phi2 is the Mega II's 1.023MHz at any speed. Handed the
processor's cycles, a Mockingboard's VIA timers ran its music about two and
a half times too fast at 2.8MHz, and it made samples faster than the mixer
took them, so the backlog grew for as long as the machine ran.
`test_iigs_boot.cpp` times a card's timer at full speed.

**A card's samples are added after the `$C03C` amplifier, not through it.** A
Mockingboard in a socket has its own output on the real machine, so scaling it
by the volume nibble would fade a card's music along with the ROM's bell — the
same reason the Ensoniq is kept off that nibble.

**A slot given to "Your Card" with nothing in it reads the bus.** `$C02D`
says which slots the internal firmware answers for; a slot switched away from
it with no card fitted answers `$FF`, as an empty slot on any Apple II answers
with the bus, rather than showing the firmware the setting was meant to hide
(`IIgsMemory::slotIsExternalAndEmpty`). ProDOS 8 2.4.1 finds the AppleTalk
firmware's `ATLK` signature in slot 7 and calls into it, which ends in a BRK
at `$C711` on a ROM 01 — a ProDOS 2.4.1 bug, fixed in 2.4.2 ("not compatible
with the AppleTalk Workstation card"), and 2.4.3 boots here — and the known
way round it is to set slot 7 to Your Card, which only works if the firmware
then goes away.

**The IIgs's SmartPort answers where the machine's own firmware does.** The
real slot 5 firmware has `$C5FF = $0A`: its ProDOS entry at `$C50A` and its
SmartPort entry at `$C50D`, and software written for a IIgs hard-codes those
rather than reading `$C5FF`. Every `SmartPortCard` lays its ROM out that way
(see `docs/design/cards.md`), and with `setStandsInForFirmware(true)` its
`$C5FE` status byte is the firmware's `$BF`
whatever is fitted — four volumes, removable, interrupting. ProDOS 8 1.x needs
the drive 2 that byte implies: its device-table builder pushes a byte per
device that is not the boot device and pops one per other device in the boot
slot, which only balances with two drives there. A card laid out like a card,
reporting the one image it held, sent every demo disk booting ProDOS 8 1.x
into a BRK — first at `$C711`'s neighbour when a `JSR $C50D` found an RTS, then
after the ProDOS splash from the unbalanced stack. `SmartPortCard::
setTransferCallback` reports every block transfer, for a trace or a debugger.

**Slot 5 is the IIgs's SmartPort, and it is part of the machine** — no card to
fit, no Control Panel setting. `IIgsMemory::setInternalCardSlot` names the slot
that answers at `$Cn00` whatever `$C02D` says, because a part the machine has is
on the internal side of that switch. The machine's own slot 5 firmware is real
but polls the IWM for a Sony 3.5" drive, so it cannot serve a block image; with
nothing inserted the SmartPort has no ROM and that firmware shows through.
**Which of the two answers at `$C5xx` changes only at reset**
(`SmartPortCard::setROMFollowsReset`). At "Check startup device!" the firmware
runs its slot 5 code over and over, and an image inserted then used to swap the
SmartPort's ROM in under a CPU part way through the old code: the machine
landed in the monitor at whatever byte its next instruction fell on. An image
inserted while the machine runs is readable at once and its ROM takes over at
the next Ctrl+Reset or power on (the host says so); one inserted before the CPU
has run since reset latches straight away, which is how a machine started with
an image boots from it; ejecting the last image leaves the ROM, answering "no
device", until reset. `test_iigs_boot.cpp` inserts deep in `$C5xx` and pins both.
`SmartPortCard::setExecutingAt` is how a trap card is told the CPU is executing
its entry point rather than reading it — a 6502 has already advanced the program
counter by then and a 65816 has not, and the card must not guess.

**A IIgs has a game port like every other Apple II, and the host drives it
through the same three calls.** The paddle timers are the Mega II's, so
`IIgsMachine::setPaddleValue` hands the value to the MMU inside it, and
`setButton` holds one of the three pushbutton lines down — ORed with the Apple
keys in the button callback, because `$C061`/`$C062` are one line each rather
than two. The bindings for all three used to answer only `g_emulator`, which
returns early while a IIgs is running, so a joystick, a gamepad and the
Joystick window's cursor keys moved nothing at all on that machine.

**The Sirius Joyport fits that connector too**, and the multiplexing works
here as it does on a //e: the annunciators choose the stick and the axis pair,
and the Joyport answers `$C061-$C063` *instead of* the Apple keys, active low.
**Its reset guard has to be a hundred times longer than a //e's.** Both lines
idle high, which is a held Open and Closed Apple to firmware deciding how to
start, and a IIgs asks twice — measured at about 229,000 and 396,000 cycles of
the Mega II's clock, after its power-on diagnostics, and never again — where a
//e's reset routine asks within a few milliseconds. With the //e's 50,000-cycle
window a machine with a Joyport fitted went into the self test and drew nothing
at all; `IIgsMachine::JOYPORT_RESET_GUARD_CYCLES` is a second's worth, which
covers both looks and is still far shorter than the time anything takes to boot
off a disk and ask about a stick. `test_iigs_boot.cpp` pins the table and the
boot.

**A mouse report's two top bits are two buttons.** `$C024` gives X then Y,
seven bits of movement each; the X byte's bit 7 is button 1, which the mouse
here does not have, and the Y byte's is button 0. The same button in both
was two presses to the firmware, and the Finder opened a folder on a single
click.

**The volume nibble in `$C03C` reaches the speaker and not the Ensoniq.** One
amplifier really does carry both on the machine, and modelling that sounded
wrong: sound software drops the nibble to about 5 and puts it back to 15
around every burst of DOC access, in flips lasting well under ten
milliseconds, so scaling the synthesiser by it wobbles a steady note at
whatever rate the software happens to be transferring at, and leaves the
average level low as well. The host's volume control is the amplifier for the
Ensoniq instead. GSSquared does the same, for the same reason, and names two
more: a stereo card taps the DOC's channels ahead of the volume control, and
at least one game sets the nibble to zero while playing through one. The
speaker keeps the nibble, because the ROM's bell fades by walking it down.

**Where the nibble is applied it is a taper, not a ratio.**
`amplifierGain()` in `iigs_spec.hpp` is a cube root, and that came out of a
measurement: `nibble / 15` put the machine 9.5dB below a //e for the same
speaker click at the setting its own firmware boots with, 0.150 peak against
0.450. There is no turning it up from inside either, because the Control Panel
hotkey is not implemented and the firmware rewrites battery RAM's volume byte
(`$1E`) whenever its checksum does not match. The taper keeps what the nibble
is for — silence at zero, full output at fifteen, every step ordered — and
puts the default within 3dB of the other machines.

**The nibble also reads back as it was written**, which it did not:
`readControl()` forced it to 15, so the Control Panel's volume setting and the
toolbox's `SetSoundVolume`, which all change it by reading the register and
writing it back, were working from a machine that claimed to be at full
volume. `test_iigs_devices.cpp` pins the taper, the readback and the
Ensoniq's independence from the nibble; `test_iigs_boot.cpp` pins the
speaker's level at the firmware's own volume.

**A IIgs has a speaker as well as an Ensoniq.** `$C030` is a Mega II address, so
`IIgsMachine` owns an `Audio` toggled on the slow clock and adds the Ensoniq's
samples on top. Without it the machine is silent through every beep and click.
The volume nibble in `$C03C` is the amplifier's and scales both: the ROM's bell
fades out by turning it down, and the firmware sets it to 5 from battery RAM.
For the speaker the gain follows the nibble's writes at the slow-clock times
they happened (`IIgsMemory::setVolumeCallback`, applied per sample in
`IIgsMachine::generateStereoAudioSamples` through a twenty-millisecond slew),
after the coupling stage: one gain per buffer, taken from the nibble at the
buffer's end, made the fade a staircase and brought the speaker's decaying
tail back at full level when the ROM put the volume back — a note after the
bell. `test_iigs_boot.cpp` rings it and checks the envelope.

**The mix is held under full scale, not clipped.** The Ensoniq can reach
`ENSONIQ_LEVEL` (4.25) times full scale with every slot of its scan at full
scale, and the speaker and a Mockingboard add to that. The device clipped
whatever went past, a buzz on every loud peak. `PeakLimiter`
(`audio/peak_limiter.hpp`) runs over the whole mix at the end of
`IIgsMachine::generateStereoAudioSamples`: it does nothing below its ceiling
(0.98), and when a peak would go past, it drops the gain of both channels at
once to put that sample on the ceiling, then lets it back up over about a
quarter of a second. `test_peak_limiter.cpp` pins it.

**The Ensoniq runs on the machine's clock and it interrupts.** `IIgsSound` is
the chip as GSSquared and MAME model it — resolution-shifted table addressing,
a zero byte halting every mode, the table's end wrapping free-run and halting
the rest, swap mode handing over to the partner, sync mode restarting the
oscillator below, one scan per `8 × (oscillators + 2)` ticks of 7.16MHz.
`IIgsMachine::step` feeds `advance()` the slow clock and the chip produces a
frame per scan into a ring that `generateSamples()` resamples to the host at
the chip's rate over the host's, nudged by up to half a percent to hold the
backlog near four milliseconds beyond the buffer being made. The machine has
already produced that buffer's frames when the backlog is measured, so the
target counts them: aiming at four milliseconds alone leaned fast on every
buffer and ran the ring dry before its end, holding the last few samples
flat sixty times a second, which crackled (`test_iigs_devices.cpp`, "never
run the chip's output dry"). **The output is the average of the scan's
slots, whatever channel each oscillator is assigned to.** The chip has one
analogue output and the oscillators take turns on it: each gets one cycle of
the scan, its byte through "two cascaded eight bit Digital to Analog
Converters", volume in the upper and waveform in the lower, the last two
cycles refresh the RAM, and "the results are integrated over time" (Apple,
*Ensoniq DOC ERS*, Rev. 1.0, June 1986; "the 32 oscillators are time-domain
multiplexed", *Apple IIGS Hardware Reference*, chapter 6, whose stereo example
demultiplexes that one output on the channel strobe "to low-pass filter"). A
stock machine filters the pin, so it hears each oscillator as one slot in
`enabled + 2`: the chip can never be louder than one slot at full scale, and
a voice is louder the fewer oscillators share the scan. It used to sum the
oscillators at a fixed eighth of full scale each, which had sixteen of them
at twice full scale and no document behind the eighth. The amplifier after
the chip is not documented — the Hardware Reference gives only the
connector's ±5V — so `IIgsSound::ENSONIQ_LEVEL` is its gain, 34/8, which
keeps a voice with all 32 oscillators enabled as loud as the old eighth.
`test_iigs_devices.cpp` pins both: every slot at full scale is the same level
with 4, 16 or 32 enabled, and one voice is 34/4 times louder with 2 enabled
than with 32. Only a stereo card in a slot uses the strobes to pull the
channels apart, and there is no such card here. Splitting by the channel field
instead put a game's bass in one speaker and its melody in the other — Spy
Hunter played one or the other rather than both. **The uppermost enabled
oscillator is heard three times over**, its own slot and the two refresh
cycles after it, which fills the scan's `enabled + 2` slots. Neither Apple
document says what the pin carries during refresh, so this is undocumented. An
oscillator with its interrupt bit set raises one when it halts; `$E0` names it
active low and clears it on the read; `IIgsMemory::interruptPending()` includes
the chip. Interrupts waiting are reported oldest first — "pushed onto a
first-in, first-out buffer, and handled in that order" (*Apple IIGS Hardware
Reference*, the Oscillator Control register) — where the lowest-numbered used
to go first. One that finishes with its bit off is kept, and turning the bit
on delivers it: "if the IE bit is changed to a one then the interrupt will be
sent to the OIR" (*Ensoniq DOC ERS*). A write that keys the oscillator on
again drops what the last run kept, since a player keys a voice on with its
interrupt bit set in the same write; neither document says, so that is a
choice. Sync mode pairs a lower even oscillator with the odd one above it,
which restarts with it ("the odd-mate oscillator will synchronize and begin
its wavetable simultaneously"); it used to restart the odd one below.
`test_iigs_devices.cpp` pins all three. The sound tools play every sample through swapped pairs refilled
from those interrupts, so a chip that only ran when the host asked for a
buffer, and never interrupted, played the first buffer of anything and stopped.

**A IIgs's printer is on the back of the machine, and the port is channel A.**
The two sockets are the two halves of one Z8530, and which half is which was
measured rather than reasoned about: slot 1's firmware programs `$C039`/`$C03B`
and slot 2's `$C038`/`$C03A`, so the printer port is channel **A** — the
opposite of what both the address order and the port numbering suggest.
`IIgsMachine::setSerialTxCallback` hands the host a byte with the port it left
by (1 or 2) and `serialReceive` puts one into the modem port, which is what the
//c's pair of calls mean on a machine with two ports. The host's own printer
does not have to know any of it: `serial1`/`serial2` in slots 1 and 2 of the
IIgs profile are the same names a //c uses, so the printer manager finds an
ImageWriter reachable without being told about a third machine.

Two things in that path print nothing at all when they are wrong, and both are
pinned by `test_iigs_boot.cpp`. **An unplugged port answers as a device that is
present and ready** — CTS *and* DCD — because what is on the end of it is an
emulated printer, and the firmware polls both before every character; a port
that answered honestly sat in that loop for ever. And **the loopback cable
between the two ports is not fitted by default**: it is a test rig that only
the Apple IIgs Diagnostic's External Serial Ports Test asks for, and with it on
a byte the printer driver sends goes round to the other socket instead of out
of the machine. It is a tick box in the Serial Port window, deliberately not
remembered across sessions.

**ENABLE is not the motor, and `DiskController::isDriveEnabled()` is the
difference.** A drive keeps turning for about a second after the CPU switches
it off; `isMotorOn()` says so, and that is right for reading. But the IWM's
mode register is writable exactly while the *line* is low, and the sequencer
must not write flux when it is. The IIgs firmware exercises both in one
instruction — it switches the drive off and writes the mode register at
`$C0EF`, which is also Q7 — so a machine that asks about the mechanism instead
of the wire spins for a second and erases track zero while it does it.

**A IIgs's IWM has a 3.5" port, and an 800K disk goes in a real drive on
it.** Bit 6 of `$C031` points the chip at that port and bit 7 is the port's
SEL line (`IWM::setDiskRegister`; `$C031` is held in `IIgsMemory` only so it
reads back). With the port selected the four phase lines are not a stepper:
CA0-CA2 and SEL are a 3.5" drive's sixteen-way selector and LSTRB its strobe.
`SonyDrive` (`cards/iwm/sony_drive.*`) answers the status bits on SENSE and
performs the controls on the strobe — step direction, step, spindle on and
off, eject, clear disk-switched — from Neil Parker's table of the firmware's
SEL35/STAT35/CONT35. ENABLE only selects a drive; the spindle is its own
control and stops half a second after the drive is deselected. The data path
is the chip's own, not the P6 sequencer: a bit every 2us (two slow cycles),
in latch mode a byte held until it is read and then cleared, and in
asynchronous mode a write buffer the IWM empties itself, with the handshake
register's ready bit and its underrun, which is how the firmware knows its
last byte reached the disk. The 5.25" sequencer stands still meanwhile
(`DiskController::fiveInchSelected`), and a 5.25" drive's light and `$C036`'s
slot 6 motor detect ask `isFiveInchMotorOn`, so a 3.5" read runs at 2.8MHz.

**The disk is held as a 3.5" WOZ, whatever it came in as.** A WOZ is kept;
an 800K or 400K block image (or a 2MG holding one) is encoded track by track
by `disk-image/gcr35.*` — five zones of 12 down to 8 sectors, 2:1
interleave, 524-byte sectors (12 tag bytes) in the three-way checksummed
6-and-2 CiderPress2 documents, **699 data nibbles and 4 of checksum, 703 in
all** — and decoded back on save, a sector that no longer reads keeping its
old block, so what is saved is the format that went in. A disk the machine
ejects (GS/OS does) is kept by the drive until the host has put it in Recent
(`has35Ejected`/`export35Ejected`/`clear35Ejected`). The mechanism is the
IWM's card state; the disks follow at the end of a IIgs state (version 3).
`test_disk35.cpp` pins the encoding, the table and the IWM's registers;
`test_iigs_boot.cpp` boots ProDOS from a 3.5" disk through the machine's own
slot 5 firmware and writes and reads a block back through it. System 6.0.4's
installer and System 2.0 boot from one. The hosts have a 3.5" Drives window on
a IIgs only (`disk35-manager.js`, `native/src/drives/disk35_drives.*`), beside the
5.25" Drives window, and an 800K image dropped or inserted on a IIgs goes there
(`media-kind.js: isDisk35`). Each shows the disk turning under its head as the
5.25" window does: the browser's `DiskSurfaceRenderer` takes a geometry
(`THREE_AND_A_HALF`: 80 tracks, 12 sectors), and the native card paints the
side under the head from `inspect::buildOverview35`, which spreads the 80
tracks over the platter's 160 rings and reads their fields the 3.5" way
(`analyzeTrack(..., Recording::ThreeAndAHalf)`); read as 5.25" fields, every
3.5" sector is a failed checksum. The native 5.25" window keeps the ImGui id
"Disk Drives" (`DiskDrives::WINDOW_NAME`), so saved layouts still find it.
**The two native windows are built from the same parts**: one card
(`native/src/drives/drive_ui.hpp`: the turning thumbnail, the label, and a track
bar the head slides along, 35 tracks or 80 with the zones marked) and one
inspector (`native/src/drives/disk_inspector.*`), which knows nothing about drives:
each window hands it an `InspectedDisk` every frame and a `RingReader` that
reads a ring in full, a quarter track on a 5.25" disk, half a track on the
side under the head of a 3.5" one.

**Two devices had to exist before the machine would draw anything**, which is
earlier than the plan expected: the firmware's power-on diagnostics sync and
interrogate the **ADB** controller and test the **Ensoniq's** RAM before the
splash screen. A IIgs whose `$C027` never answers stops with `Fatal system
error-> 0911`. Both are real devices in their own files now, with the keyboard,
the mouse and the synthesiser still to come.

**A ROM image's banks can be either way round**, and `loadROM` asks rather than
assumes: it looks for the emulation reset vector, which every IIgs ROM has at
`$FF:FFFC`. Get it wrong and the machine resets to `$00:0000`.
