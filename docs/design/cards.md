# Expansion cards and interrupts

Moved out of CLAUDE.md; this is the detail behind the rules summarised there.

## Interrupts

**The IRQ input is a level, and the CPU samples it every instruction.**
`CPU6502::irq()` is an edge, latched until the CPU can take it — that is how a
device interrupting while the I flag is set is not forgotten — but the line
itself is polled through `setIRQStatusCallback()`, a predicate the `Emulator`
builds from the devices that can hold it down: the Mockingboard's VIAs, the
mouse (a card's, or a //c's IOU), and the serial ports. Without the poll, a
handler that returns without clearing its device is never re-entered, and a
device that lets go can still deliver one more interrupt from the latch —
neither of which is what the hardware does.

The predicate is deliberately a handful of null checks against pointers the
`Emulator` already holds, not a walk of the slot array asking every card: the
CPU dispatch loop is the hottest code here and eight virtual calls per
instruction would be paying for devices that do not exist. It is also only
sampled while the I flag is clear, which costs under 1% rather than ~4%. A card
that can hold the line and is not in that list is still heard through its own
edge; what it cannot do is re-interrupt a handler that ignored it.

## Expansion Card Architecture

The MMU supports pluggable expansion cards matching real Apple IIe hardware. Cards implement the `ExpansionCard` interface (`src/core/cards/expansion_card.hpp`).

### Slot Memory Map

| Slot | I/O Space   | ROM Space   | Default Card                |
| ---- | ----------- | ----------- | --------------------------- |
| 1    | $C090-$C09F | $C100-$C1FF | Empty                       |
| 2    | $C0A0-$C0AF | $C200-$C2FF | Empty                       |
| 3    | $C0B0-$C0BF | $C300-$C3FF | 80-column (built-in, fixed) |
| 4    | $C0C0-$C0CF | $C400-$C4FF | Mockingboard                |
| 5    | $C0D0-$C0DF | $C500-$C5FF | Thunderclock                |
| 6    | $C0E0-$C0EF | $C600-$C6FF | Disk II                     |
| 7    | $C0F0-$C0FF | $C700-$C7FF | SmartPort                   |

### Card Interface Methods

```cpp
class ExpansionCard {
    virtual uint8_t readIO(uint8_t offset);      // I/O space ($C0x0-$C0xF)
    virtual void writeIO(uint8_t offset, uint8_t value);
    virtual uint8_t readROM(uint8_t offset);     // ROM space ($Cx00-$CxFF)
    virtual void writeROM(uint8_t offset, uint8_t value);
    virtual void reset();
    virtual void update(int cycles);
    // ... serialization, IRQ callbacks, etc.
};
```

### Available Cards

- `Disk2Card` (`cards/disk2/`) - Wraps Disk2Controller (slot 6)
- `MockingboardCard` (`cards/mockingboard/`) - Dual AY-3-8910 + VIA 6522, stereo output (slot 4)

**The Mockingboard is measured against the datasheets, and four rules in it
are load-bearing.** An AY-3-8910 envelope ramp is 16 steps in `256 × EP`
clocks, so a step is 2 EP ticks of the clock over 8; one step every EP ticks
is the YM2149's 32-step rate, and played every envelope twice as fast. The
PSGs run at the machine's own clock (`AY8910::setClock`, from the profile in
`setMachine`), so a PAL machine's notes are lower, as on the card. The
output is taken from the chip's tick stream through a windowed-sinc low pass
(`FILTER_TAPS`, cut off at 20kHz), not by averaging the ticks in each
sample, which folded ultrasonic tones (period 1 or 2) back into the audible
band. And the VIA drives the AY's BC1, BDIR and RESET as levels: a write or
latch follows the bus while it is held, LATCH straight to WRITE is a write,
and RESET low holds the chip reset. Reset clears every AY register, the mixer
included. The two chips play independently, one per side; nothing
substitutes one for the other when their registers match.
`test_ay8910.cpp`, `test_via6522.cpp` and `test_mockingboard.cpp` pin each.

**A register write lands on the chip at the cycle it was made.** The card
reads the machine's cycle through its cycle callback and runs both chips up
to it before a write reaches a VIA (`MockingboardCard::syncToCycle`); the
chips advance tick by tick between samples (`AY8910::advance`), and a sample
reads the filter wherever they are. The chips used to catch up only at each
48kHz sample, so two chips written eight cycles apart (a song mirroring its
notes to both) came out in step or inverted according to whether a sample
boundary fell between the writes, and on speakers close together the two
sides cancelled. **Mockingboard Phase Lock** in the sound menu
(`MockingboardCard::setPhaseLock`, a host preference, on by default, not in
a save state) plays the left chip on both sides while the two hold the same
registers, so a mirrored song cannot cancel itself. A real card plays its
chips apart; turning the lock off is how to hear exactly that. **Mockingboard
Mono** (`MockingboardCard::setMono`, also on by default and not in a save
state) mixes the two chips, half each, and plays the mix on both sides;
off, PSG 1 is the left and PSG 2 the right, as on the card. Mono is applied
after phase lock, so with the lock off a pair half a cycle apart cancels, as
two real outputs wired together would.
**A card's output is not clipped.** Each chip's three channels are summed
and divided by three, so a chip runs from 0 to 1; the card's DC filter makes
that bipolar, and where a held full level drops away it swings to about
1.08 (the resampling filter's overshoot on top of the step). The 8-bit
machines' `Audio` mixes the card and the speaker at half level each, so
either alone stays inside full scale, but both together can pass it: that
sum used to be clamped flat, and now goes through the same `PeakLimiter` as
the IIgs's mix, which does nothing below its ceiling. On a IIgs the card is
added at half level ahead of that machine's limiter. `test_audio.cpp` drives
a loud card and speaker together and pins that nothing passes the ceiling.
- `MouseCard` (`cards/mouse/`) - Apple Mouse Interface Card via MC6821 PIA command protocol (slot 4)
- `ParallelCard` (`cards/parallel/`) - Centronics parallel port; drives Epson FX-80 and Apple DMP virtual printers (slots 1–2)
- `SmartPortCard` (`cards/smartport/`) - SmartPort hard drive controller, 2 block devices, self-built ROM (user-configurable slot)

**A SmartPort card answers where Apple's SmartPort firmware does.** Its ROM
has `$CnFF = $0A`: the ProDOS entry at `$Cn0A` and the SmartPort entry three
past it at `$Cn0D`, where the IIgs's slot 5 firmware has them (*Apple IIgs
Firmware Reference*). The documented way to find them is `$CnFF`, and ProDOS
reads it, but software hard-codes Apple's addresses: French Touch's DIX,
written for a Liron card or a //c, loads its menu with `JSR $Cn0A`. The card
used to put its entry at `$Cn10`, so that call landed on the boot stub's `STX
$C0n0` and booted the disk again. The fall-through from `$Cn00` branches over
the entries to the boot stub at `$Cn10` with a `BEQ` (the `LDA #$00` before it
has set Z), which a 6502 has and a `BRA` is not. The stub's boot marks the card
booted, so the boot block's first call to the entry is a driver call; it used
to be taken as a boot, which loaded block 0 again and restarted the boot block
— harmless to ProDOS's, which starts over, but not to DIX's, which has banked
the language card's RAM in over the monitor by then.
- `SoftCardZ80` (`cards/softcard/`) - Microsoft Z-80 SoftCard with Z80 CPU emulation (`cards/softcard/z80/`)
- `SSCCard` (`cards/ssc/`) - Super Serial Card with ACIA 6551; drives ImageWriter I and ImageWriter II virtual printers (slots 1–2)
- `SerialPort` (`cards/serial/`) - One of a //c's two built-in ports: the same ACIA 6551, no DIP switches and no ROM (slots 1 and 2, fixed)

**The paper canvas is a window, not the whole job.** A browser caps a canvas
(~32767 px a side, and iOS Safari by total area), and one 8.5x11" page at the
default SS=3 is already ~13.5M backing pixels — about 54MB. `PrinterWindow`
therefore keeps a few pages live (`_liveWindowPages`, asked of the browser via
`canvasFits` and bounded by `MAX_LIVE_BACKING_PX`) and scrolls the paper through
it: `_scrollWindow` writes the departing pages to the page store, shifts the
bitmap up by whole pages and advances `_pagesScrolled`, which `_yToCanvas`
subtracts from every coordinate. Whole pages, because the page-break overlay and
every slice in the snapshot and export paths are page-aligned.

Two consequences are load-bearing. **Ink asks for its row rather than working
it out** (`_reserveRow`): making room can scroll the window, so the canvas y is
only settled after the call — a caller that computed it first drew a page-height
off once a long print started scrolling. And **an export is the job, not the
window**: `_allJobPages()` puts the stored pages before the live ones, which is
what the PDF and the multi-page ZIP use. Page records are numbered from the
start of the job, and the Print Browser counts a job's pages itself rather than
trusting the `pageCount` stamped on a record that was written while the job was
still short.

Before this the height was simply clamped, and every dot past the last page that
fitted was dropped: a four-page print kept one page and silently lost three.

**A GS/OS print is graphics, and the Automatic Line Feed switch nearly ruins
it.** The ImageWriter driver rasterises the page into 8-dot bands and writes
`CR`, `ESC T 16`, `LF` before each — 16/144" is exactly eight dots at the head's
1/72" pitch, so the bands abut. `CItohPrinter` treats `CR`+`LF` as one line
ending when the switch is on (which plain Applesoft text needs), and that
pairing has to survive an escape sequence that prints nothing: the driver's
escape sets the distance for the very `LF` it precedes. With any `ESC` byte
breaking the pairing, every band fed twice and each line of a real GS/OS print
came out sliced in half by a 1/8" white stripe. `_inked()` drops the pairing
whenever a character or a graphics column is laid down, so `CR`, ink, `LF`
still feeds twice. `tests/js/printer/citoh.test.js` pins the band pitch against
a byte stream captured from System 6.0.4 printing through ImageWriter/Printer
v4.2.
- `ThunderclockCard` (`cards/thunderclock/`) - ProDOS-compatible real-time clock (slots 5, 7)
- `NoSlotClock` - DS1215 real-time clock piggybacking on $C300 ROM (not a slot card; toggle in Expansion Slots UI)
