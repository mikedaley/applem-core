# Machine profiles

Moved out of CLAUDE.md; this is the detail behind the rules summarised there.

## Machine Profiles

The emulator models one machine at a time, and which machine it is comes from a
**profile**: `src/core/machine/machine_profile.hpp` holds a `MachineProfile`
per machine and a registry of them. There are four: `APPLE_IIE_PROFILE`,
`APPLE_II_PLUS_PROFILE`, `APPLE_IIC_PROFILE` and `APPLE_IIGS_PROFILE`.

**A profile also says which family it belongs to, and that is what selects the
parts.** `MachineFamily::AppleII` is the three 8-bit machines: one design, built
from `MMU`, `Video`, `Audio` and `CPU6502`, differing only by the numbers in
their profiles. `MachineFamily::AppleIIgs` is a different computer — a 65816 on
a 24-bit bus, a memory controller that shadows banks, a second display system,
an Ensoniq — and it is built from its own classes in `core/iigs/`. The family is
chosen once, at construction, and is also what the compile-time validation asks
before applying a rule that only holds for one design: a IIgs is not measured
against the //e-sized arrays it does not use, or against "a visible column
clocks out 14 dots" when its picture is 640 dots wide. See `wiki/Apple-IIgs.md`
for the plan; `Emulator::isMachineRunnable` returns false for the whole family
until its parts exist, whatever ROMs are in the build.

**The profile is data, not polymorphism.** The parts of a machine that differ
between a //e, a II+ and a IIgs are overwhelmingly numbers — a clock rate, a
scanline count, how much RAM answers, which CPU is fitted, whether the video
generator inhibits colour burst in text mode. Those live in a struct that the
subsystems read. They are deliberately not virtual methods: `MMU::read`, the
video emitters and the CPU dispatch loop are the hottest code in the emulator,
and an indirect call on a per-cycle or per-dot path would cost real speed to
serve a machine count of one. Anything a future machine cannot express as data
— a 65816's 24-bit bus, the IIgs shadowing map, Super Hi-Res — wants its own
subsystem class chosen once at construction, not a branch taken sixty million
times a second. The rule is: **a number or a flag goes in the profile; a
different mechanism goes in a different class that the profile names.**

The profile is threaded by construction, not by lookup. `Emulator(MachineId)`
selects it and hands it to `MMU` and `Audio`; `Video` takes it *from the MMU*
rather than as a second argument, because the video scanner and the floating
bus are the same counters read two ways and a pair that disagreed would be a
bug with no way to express it. Cards receive it through
`ExpansionCard::setMachine()`, called by `MMU::insertCard`. Most cards ignore
it — a Disk II does not care what is at the other end of the bus — but the
mouse card raises its interrupt at the start of vertical blank, and where
vertical blank falls belongs to the machine.

**The constants in `types.hpp` did not go away, and that is deliberate.**
`MAIN_RAM_SIZE`, `FRAMEBUFFER_SIZE` and the rest size `std::array` members at
compile time, which a runtime profile lookup cannot do. They remain as the
//e's values, and `machine_profile.hpp` `static_assert`s every one of them
against the profile, so the two descriptions cannot drift: change one and the
build fails. Further assertions pin the relationships rather than the numbers —
a scanline is its blanking plus one cycle per visible column, a visible column
clocks out 14 dots, the framebuffer is every visible line doubled.

**Every profile is validated at compile time.** `profileIsSelfConsistent()`
checks that a profile describes a machine that could exist — a scanline is its
blanking plus one cycle per visible column, a column clocks out 14 dots, the
framebuffer is every visible line doubled, a machine with no auxiliary bank
does not claim auxiliary RAM, double hi-res does not exist without 80 columns,
the ROM reaches the top of the address space, and nothing is fitted to a slot
the machine does not have. `profileFitsCompiledStorage()` checks it against the
arrays the build actually allocates, which are sized for the //e and are
therefore the ceiling for every machine. `allProfilesValid()` runs both over
the registry in a `static_assert`, so a broken profile does not compile.

**Save states carry the machine id, and every machine writes the same header.**
Twelve bytes — magic, format version, machine id — begin a state whichever
machine wrote it. Everything after that point is laid out to the saving
machine's shape, so a state restored into a different machine would be read as
garbage rather than fail; the id is what lets `importState` refuse it. The
Apple II family's layout is `STATE_VERSION` 9 in `emulator_state.cpp`; a IIgs's
is its own (`iigs_state.cpp`, version 1), because the two share nothing after
the header and have no reason to move together. The host reads the header
itself (`src/js/state/state-header.js`) and, asked to load a state saved on
another machine, switches to that machine first rather than let the core
refuse — a save is a save of a whole machine, and loading one is asking for it
back. The autosave is kept per machine for the same reason (see State
Serialization).

**The host asks rather than assumes.** `src/js/machine/machine-profile.js`
fetches the whole profile as one JSON string through `_getMachineProfileJSON`
(one round trip — the Worker services RPCs on the thread that runs the
emulation) and `main.js` does it immediately after the WASM module is up,
before anything sizes itself to the picture. The WebGL renderer, the
text-selection overlay, the screenshot path, the save-state preview, the
printer's screen dump and the agent's `captureScreenshot` all read
`machineDisplay()` instead of the 560x384 they each used to hardcode. A fetch
failure is not fatal: the module falls back to the //e, which is a correct
description of the only machine that exists.

The one place that still fixes a size is the shared framebuffer slot
(`FB_WIDTH`/`FB_HEIGHT` in `worker/shared-buffers.js`). A `SharedArrayBuffer`
cannot be resized once handed to the Worker and the AudioWorklet, so the slot
is allocated up front and must hold any machine's frame. `setupSharedBuffers()`
checks the fit and falls back to the `postMessage` transport rather than let a
frame write past the end of the slot.

### The Apple II Plus

The second profile, and the one that proves the seam carries. Its video timing
is the same circuit, so every number in `MachineTiming` is identical to the
//e's and the differences fall entirely in what the machine *has*. Four of them
matter, because each exercises a different part of the mechanism:

- **An NMOS 6502 rather than a 65C02.** The CPU core already modelled both
  variants; the profile is what selects one.
- **No auxiliary bank.** `$C000-$C00F` are the //e's memory and display
  management switches — 80STORE, RAMRD/RAMWRT, INTCXROM, ALTZP, SLOTC3ROM,
  80COL, ALTCHARSET — and on a II+ that range manages no memory at all.
  `writeSoftSwitch` ignores the whole group when the machine has no auxiliary
  bank, and **that single guard is what makes every 80-column and
  double-resolution path unreachable**: `Video` selects those modes from the
  80COL switch, which can now never be set. No second guard in the video code
  is needed or wanted.
- **12KB of ROM at `$D000` rather than 16KB at `$C000`,** since nothing on a
  II+ motherboard answers at `$C100-$CFFF`. `MMU::loadROM` places a machine's
  image at the offset its `romBaseAddress` implies within the `$C000-$FFFF`
  window, so the read path — which indexes `address - ROM_WINDOW_BASE` — needs
  no knowledge of where a given machine's ROM begins.
- **It never inhibits colour burst.** A //e kills the burst on text lines and
  so shows crisp white text; a II+ sends a reference on every line and its text
  fringes green and violet in every mode. `Video::burstForScanline()` reads
  `caps.inhibitsBurstInText`, so this follows from the profile alone.

**Its character generator stores glyphs the other way round.** A //e's 8KB ROM
puts bit 0 at the left of a glyph row and leaves the blank scanline at the end
of each eight-byte cell; a II+'s 2KB ROM puts bit 6 at the left and the blank
scanline first. Neither is more correct — it is how the part was wired to the
video shift register — but the renderer reads one layout, so `MMU::loadROM`
rewrites the image into it (`normaliseCharROM`, driven by `MachineCharRom` in
the profile). This is done once at load rather than per dot, because it is a
property of the ROM image and the dot loop is the hottest code in the video
path. Get it wrong and every character on screen is drawn mirrored, which is
exactly what the II+ did before this existed.

**It has only one character set, and asking for a second blanks the screen.**
The UK set is the other half of the //e's 8KB ROM (342-0273), and the profile
says where each set begins (`MachineCharRom::usSetOffset`/`ukSetOffset`): the
UK set is the *lower* half and the US set the upper, so a US machine reads
from 0x1000. Reading the lower half as the US set made the //e's switch work
backwards and put a pound sign for every # on a IIgs, which reads the same
part with no switch. A II+ has nothing past its one set, so every glyph would
read back blank and the display show nothing but the cursor, which survives
because it is the inverse of a blank. `caps.hasUkCharSet` gates the switch,
which only the //e has, and the host hides the toggle elsewhere.
`test_machine_profile` pins the # and the pound sign on every machine.

**The II+ ROMs are optional and are not in the repository.** A II+ motherboard
carries six 2KB ROMs in sockets D0 to F8 covering `$D000-$FFFF`: five of
Applesoft and the Autostart monitor at `$F800`. `scripts/generate_roms.sh`
concatenates them in address order, or accepts a single pre-combined
`apple2plus.rom`, and emits empty arrays when they are absent. **A machine can
therefore be fully described and still be unable to start.** `Emulator::init()`
records this in `hasSystemROM()` rather than silently running a //e's ROM or
none at all, `Emulator::isMachineRunnable()` answers the same question about a
machine that is not running, and the host's `listMachineProfiles()` puts a
`runnable` flag on every entry so a chooser does not offer a machine that will
never reach a prompt.

**Switching machines rebuilds the emulator.** There is no way to convert a
running machine into a different one — the RAM, the cards and the save state
are all shaped to the machine that made them — so `_setMachine` destroys the
global emulator and constructs the new one. Inserted media and host state do
not survive, exactly as they would not across a page reload, and the caller is
responsible for putting them back.

**Slot 0 exists on a II+.** `slots_` is indexed by slot number with room for
eight, and `MMU::insertCard` asks the profile rather than assuming 1-7. What
goes in slot 0 on a real II+ is the 16K language card, which is how a 48K
machine becomes the 64K one nearly all II+ software expects; the profile fits
one as a *fixed* card, because the bank switching at `$C080-$C08F` is the same
hardware the //e carries on its motherboard, is implemented by the MMU, and is
not something the user could pull out.

The Expansion Slots window follows all of this. `SlotConfigurationWindow`
builds its slot list from the profile — which slots exist, and which carry a
card the user cannot change — rather than from a fixed //e table, and
`setMachine()` rebuilds it after a switch. A //e shows slots 1-7 with the
80-column card locked into slot 3; a II+ shows 0-7 with the language card
locked into slot 0 and slot 3 free for anything. What each free slot *offers*
stays host presentation (`SLOT_UI`), since that is convention rather than
machine fact.

**What a machine ships with is in its profile, not in the constructor.**
`Emulator`'s constructor used to fit a Mockingboard in slot 4 and a Disk II in
slot 6 whatever the machine was, and `getSlotCardName` reported an 80-column
card in slot 3 whatever the machine was. Both put hardware in a II+ that it
never had, and both leaked into the saved slot layout. Defaults now come from
`slots[].defaultCard` and a fixed slot reports `slots[].fixedCard`. A card the
machine does not ship is *parked* in `diskStorage_`/`mbStorage_` rather than
dropped, because `disk_` and `mockingboard_` still point at it and
`setSlotCard()` fits it later from exactly those members.

**Refitting a slot with the card it holds changes nothing**, and startup
restores hard drive images only after the saved layout is applied
(`main.js`, after `slotConfigWindow.create()`). Both matter: a refit builds a
new, empty card, and a SmartPort's images are in the card, so a //e used to
come back from every reload with its drive empty. The ordering covers a layout
that moves the SmartPort, which the no-op alone would not.
`test_emulator_disk.cpp` pins the refit.

**Slot layouts are remembered per machine.** `src/js/machine/slot-storage.js`
keys them by machine (`a2e-slot-config:apple2e`), because the machines do not
agree about what a slot is: one shared layout put a II+'s slot 3 card into a
//e's built-in 80-column slot, and followed a //e's SmartPort onto a machine
whose defaults are a bare Disk II. A machine with nothing saved falls back to
its profile's defaults rather than to a shared constant, and an emptied machine
stays empty — "never configured" and "deliberately stripped" are different
states. The single pre-machine key is read once as the //e's starting point,
copied under the //e's own key, and then left alone; an orphan costs nothing,
and losing somebody's layout to a mistake in that copy would cost more.

### The Apple //c

A //e folded into a slab: the same 65C02, the same 128K, the same IOU and MMU,
so every number in timing, memory and display is the //e's and almost every
capability is too. What differs is the back of the machine.

**It has no expansion sockets, but it decodes all seven slot addresses.** The
firmware and everything written for a //e depend on those addresses, so each
one answers to a part soldered to the board: two 6551 serial ports in slots 1
and 2, the 80-column firmware in slot 3, the mouse in slot 4, and the disk port
in slot 6. Every slot is therefore a *fixed* slot — the part of `MachineSlot` a
//e exercises only in slot 3 — and `caps.hasExpansionSlots` is false, which is
what stops the slot window offering a card to a machine that has nowhere to
take one.

**That capability is also a memory rule.** With no socket there is nowhere for
a card's ROM to live, so the firmware for all of it is inside the 16KB system
ROM and `$C100-$CFFF` reads the internal ROM whatever INTCXROM and SLOTC3ROM
say: those switches choose between the internal ROM and a slot that does not
exist. `MMU::read` and `MMU::peek` take that branch first, before the switches.
Without it the region reads zeroes, the reset lands on a `BRK`, and the vector
sends it to another one — a //c wedged at `$C803` before drawing anything.

**Two smaller differences are modelled.** 4KB of character generator rather
than 8KB, so there is no second set to ask for; and a disk that is not a Disk
II — the drive hangs off an IWM at `$C0E0`, so slot 6 names `"iwm"` rather than
the card a //e fits there.

**Its disk is an IWM, and the sequencer under it is the card's.** The chip
decodes the same sixteen addresses at `$C0E0-$C0EF` and means the same things
by them, so what is below it — the drives, the stepper, the motor, the LSS — is
`DiskController`, shared with `Disk2Card`; `IWM` adds the register file a read
sees in front of it (data, status, handshake, and a mode register writable only
with the motor off) and has no ROM, because a //c's disk firmware is in the
system ROM rather than in slot 6's 256 bytes. Which class gets built is the
profile's `slots[6].fixedCard`, and `Emulator::getDisk()` hands out the base,
so nothing the host asks about a drive had to change. A //c boots DOS 3.3 from
the drive in its case.

**Its serial ports are the SSC's ACIA with no card around it.** Slots 1 and 2
each hold a `SerialPort` composing an `ACIA6551` at the slot's offsets 8-B —
`$C098-$C09B` and `$C0A8-$C0AB`, the same four addresses an SSC answers — so
`PR#1` and `IN#2` work off the machine's own firmware. There is deliberately no
base class shared with `SSCCard`: what the two have in common *is* the ACIA and
they already share it by composing it, the way the hardware does. What is left
over is a card's DIP switches and 2KB ROM against a port's nothing, and a base
class holding four forwarding methods would describe a part that does not
exist. This is the other half of the IWM's rule — share a mechanism, not a
resemblance.

The host's serial calls (`setSerialTxCallback`, `serialReceive`) serve every
machine that has a serial line, a IIgs included, because the question is about
the line rather than about what provides it: transmit goes to every port there
is, and a byte arriving from outside goes to port 2, the modem port, since a
printer does not talk back.

**Its mouse is the IOU, and is the one part that is not a card at all.** A //e's
mouse is an MC6821 in a slot with a ROM and a command protocol; a //c's is two
quadrature lines into the IOU, so `MouseIOU` (`input/mouse_iou.cpp`) is owned by
the `Emulator` and hooked into `MMU::readSoftSwitch`/`writeSoftSwitch` by a
pointer that is null on every other machine — which is what keeps a //e's
`$C015`, `$C063` and `$C066` exactly as they were. The profile names "mouse" in
slot 4 only because that is where a //c's mouse *firmware* lives.

Two things about it are load-bearing:

- **Travel is interrupts, not a delta.** The IOU counts nothing. Each unit of
  movement toggles X0, the edge raises an IRQ, and the firmware's handler reads
  X1 for the direction and adds one to a position in slot 4's screen holes. So
  host movement is banked and released one step at a time, and never onto a
  flag the handler has not cleared — releasing them faster loses the ones in
  between, which looks like a mouse that moves part of the way and sticks.
- **`$C015` and `$C017` report; only `$C048` clears.** Table 9-2 of the //c
  Technical Reference calls them RstXInt and RstYInt and says a read resets
  them, and the machine's own handler proves otherwise: it reads `$C015` and
  ORs `$C017` to see whether either fired, BITs each again to see which, then
  writes `$C048` when it is done. A read that cleared would send every X
  movement down the Y path. The firmware is the authority, not the table.

### NTSC and PAL

**The 8-bit machines come in both standards, and the switch retimes the
running machine rather than rebuilding it.** A PAL Apple II is the same design
on a 14.25045MHz crystal: 312 lines a frame at 50Hz, the same 192 drawn, a
clock of the crystal over 14 (about 1.0179MHz). Software timed against the
beam is written for one standard; French Touch's DigiDream, made for a PAL
//e, flips double hi-res on and off at lines it counts to from the vertical
blank, and on a 262-line frame those land somewhere different every frame.
`palVariant()` derives each PAL profile from its NTSC twin and changes the
timing alone (`VideoStandard` lives in `MachineTiming`), so everything
already reading the profile follows: the frame length, `$C019`, the floating
bus (whose vertical counter starts at `$0C8` rather than `$0FA`, which
`getVideoScannerAddress` derives from the line count), the speaker's and the
Mockingboard's cycles per sample. `Emulator::setVideoStandard` swaps the
profile in `MMU`, `Video`, `Audio` and every card (`retime`) and starts the
frame in progress where the cycle count says it is, so memory, cards and
disks stay as they were; software that measured the frame at startup wants a
reboot, and the hosts say so. The standard is a host preference held by
`MachineHost` and remembered per machine (`a2e-video-standard:<key>` in
`src/js/machine/video-standard.js`, `PAL.<key>` in the native settings), not
written into a save state. The IIgs has no PAL variant yet: its timing is
also in `iigs_spec.hpp`. `test_machine_profile` pins the numbers, the
vertical blank and the scanner; `test_machine_host` the switch, the frame
rate and the IIgs staying NTSC.

### Choosing a machine

**The header badge names the machine and is how it is changed.** It used to be
the right-hand half of `apple-logo.png`, so the header announced "//e" whatever
was running; the logo is now `applem-logo.png` (the wordmark alone) and the
badge is a live control. `MachineMenu` (`src/js/machine/machine-menu.js`) keeps
it in step and hangs an ordinary `.header-menu-container` dropdown off it, so
it inherits the app's open/close, click-outside and Escape handling rather than
inventing its own. Each entry draws the machine, names it, summarises its CPU,
memory and columns, ticks the one in use and marks any whose ROMs are missing.

The badge wears `profile.logotype`, not `shortName`. Apple's own marks are not
always what you would write in a sentence: a II Plus is badged `][`, and
rendering "II+" in the badge's heavy oblique face produces "//+", which is not
a designation Apple ever used. `shortName` stays for prose.

The window title follows the machine too, so a browser's window switcher shows
which machine a tab is running.

Switching is destructive and the menu says so before doing it: the core
rebuilds the emulator, so inserted media and anything in memory are lost, just
as they would be on a reload. What is *not* lost is the user's preferences —
`AppleIIeEmulator.onMachineChanged()` pushes the display settings, volume,
character set and clock speed back into the new core, because those were the
user's choices rather than machine state. The chosen machine is remembered in
localStorage under `a2e-machine` and restored at startup, before the renderer
and windows are built, so they are made for the right machine rather than
rebuilt for it a moment later. A remembered machine the build cannot run is
ignored rather than honoured.

### Menus follow the machine

`src/js/ui/machine-availability.js` says which menu items the running machine
can use, from its profile and the cards fitted, and
`UIController.applyMachineMenus()` hides the rest — at startup, after a switch,
and whenever the Expansion Slots window applies a change. Hidden rather than
disabled: a greyed "Expansion Slots" on a //c invites the question of how to
enable it, and the answer is a different computer. What goes: Expansion Slots
on a //c (no sockets; a IIgs keeps it, with each slot's built-in-or-card
switch); CPU Speed on a IIgs (the
multiplier is `Emulator`'s); SmartPort Drives, Serial Port and Printer unless
something provides them (a IIgs's slot 5 and its two sockets, a //c's ports, or
a card); the Mockingboard and Mouse Card debug windows unless the card is
fitted — a //c's "mouse" is the IOU, which has no PIA to show. A separator left
with nothing after it goes too. `tests/js/ui/machine-availability.test.js` pins
the table.

### Adding a machine

A new `MachineId` and profile entry, a subsystem class for anything that is a
different mechanism rather than a different number, and its ROMs. Nothing in
the host needs to know.
