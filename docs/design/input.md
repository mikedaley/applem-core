# Input, paste and CPU speed

Moved out of CLAUDE.md; this is the detail behind the rules summarised there.

## Paste / Typed Text

Pasted text (Ctrl+V, the BASIC and assembler windows, and the agent's
`typeKeyboard`) goes into a **type-ahead buffer inside the core**, not a queue
in JavaScript. `Emulator::pasteText()` translates the text with the one
`charToAppleKey` in `keyboard.cpp` and pushes it onto `pasteBuffer_`;
`loadNextPasteKey()` moves a character into the keyboard latch, and
`clearKeyboardStrobe()` schedules the next one once the program has read the
strobe. The machine therefore pulls characters at its own pace — a character
can never be overwritten before it is read — subject to the minimum gaps
below.

The host therefore does not meter the paste at all. `input-handler.js` writes
the text across in whole runs — a paste of any size costs a fixed handful of
RPCs, where the old path spent one `_charToAppleKey`, one `_isKeyboardReady`
and one `_runCycles` **per character** and drove the CPU in 500-cycle bursts
from the main thread, competing with the audio-paced worker for the same
emulation. What is left in JS is bookkeeping: the 8x speed boost, dropped and
restored around pauses, and a `_pastePending()` poll to notice the end and fire
the completion callback. `{token}` sequences (`{ctrl-c}`, `{left}`, `{chr:4}`)
are still resolved host-side and pushed as single codes with `_pasteKey`.

Two details are load-bearing. `loadNextPasteKey()` refuses to touch the latch
while the strobe is set, which is what makes the buffer lossless. And it
refuses to refill the latch *immediately*: a character becomes available
`PASTE_KEY_GAP_CYCLES` (~15ms of emulated time) after the previous one was
taken, and `PASTE_LINE_GAP_CYCLES` (~150ms) after a carriage return.

The gap is not cosmetic. Clearing the strobe twice is a keyboard **flush** —
`POKE -16368,0`, `STA $C010`, the ROM and DOS doing it before settling down to
wait for input — and it is everywhere in Apple II software. A person typing
leaves nothing pending for a flush to eat; a buffer that refilled the latch the
instant the strobe cleared handed each flush a fresh character to throw away.
The symptom is a paste that arrives with characters missing, in some programs
and not others: `10 GET A$: POKE -16368,0: PRINT A$;: GOTO 20` received
`ACEGIKMOQSUWY02468` from `ABCDEFGH…9`, exactly every second character.
`test_emulator.cpp` pins that case. The carriage-return gap is longer because
the machine has a line to digest afterwards — Applesoft tokenises it, DOS and
BASIC.SYSTEM run their command parsers — with flushes at the end of that work.

A read of `$C000` that finds the keyboard empty looks like a better signal than
a timer — the program asking for input — and it was tried and measured to fail:
Applesoft polls `$C000` between every statement to check for Ctrl-C, so it
releases the next character long before the program reaches its flush. Do not
reintroduce it. Because the gaps are emulated time, the 8x paste speed boost
shortens the wall-clock wait proportionally; a 2400-character program pastes in
around seven seconds.

The buffer is host state like the speed multiplier — it is not serialized into save states,
and `reset()` discards it. Mobile input goes through the same buffer, because
an on-screen keyboard can deliver several characters in one `input` event and
writing the latch per character overwrote keys the machine had not read yet.

## AKD (any key down)

`$C010` bit 7 says a key is *physically held*, as opposed to `$C000` bit 7,
which says a key code is waiting to be read. `Emulator::updateAnyKeyDown()`
**derives** it from the only two things that can hold a key down — a host key
currently held (`Keyboard::isAnyKeyDown()`) and a pasted character still
sitting unread in the latch (`pasteHoldsKey_`) — rather than any path setting
`keyDown_` directly. That is deliberate: every earlier version asserted AKD
somewhere with no matching release, so the line went high on the first
keystroke of the session and stayed there, and key-repeat or game input code
polling it saw a key held forever.

`Keyboard` tracks held keys by *browser keycode*, with a running count, so a
key-up always clears the key it names whatever the modifier state was when it
was pressed, and auto-repeat's stream of key-downs cannot double-count.
Shift, Control, Caps Lock and the Apple buttons deliberately do not assert AKD
— they are separate lines on real hardware, not keys in the matrix. Losing
window focus releases everything, held keys included, because a key held
across a blur never delivers its key-up.

## The Game Port

The game I/O connector takes one device, and which one is a user choice:
`GamePortDevice` in `src/core/input/joyport.hpp` — the Apple resistive
joystick, or Sirius Software's **Joyport**.

The Joyport put two Atari CX40-style digital sticks on the connector. Each has
five switches and the connector has three pushbutton inputs, so it multiplexes:
AN0 selects the stick, AN1 selects the axis pair, and PB0-PB2 report fire, the
first of the pair, and the second.

    AN0   AN1   PB0 ($C061)   PB1 ($C062)   PB2 ($C063)
    off   off   fire 1        left 1        right 1
    off   on    fire 1        up 1          down 1
    on    off   fire 2        left 2        right 2
    on    on    fire 2        up 2          down 2

**The switches are active low, which is why this is a device choice rather than
an addition.** A line reads *high* while nothing is pressed — the opposite of a
pushbutton — so a Joyport cannot share PB0/PB1 with the Open and Closed Apple
keys. `Emulator::getButtonState()` therefore consults the Joyport *instead of*
`buttonState_` when it is selected, and switching devices releases whatever the
old one was holding. It is not a slot card and does not want to be: it hangs
off the 16-pin connector, so the emulator owns it and the pushbutton read path
consults it.

**The Joyport lets go of PB0/PB1 across a reset, and it has to.** The //e's
reset routine reads `$C061` and `$C062` to see whether an Apple key is held —
Open Apple asks for a cold boot, Closed Apple runs the self test. A Joyport
idles both lines *high*, which is exactly what a held key looks like, so a //e
with one fitted ran the self test on every reset and never reached a prompt.
That is faithful: pins 2 and 3 of the game connector really are the Apple keys
on a //e, which is why the Joyport belongs to the II and II+ era. It is also
useless. `joyportResetGuardCycle_` therefore releases those two lines for
`JOYPORT_RESET_GUARD_CYCLES` (~50ms) after a reset — long enough to cover the
ROM's check, far too short for a game to have asked about the stick. PB2 is
never released, because no Apple key is wired to it, and a *closed* switch
still reads low inside the window, so a fire button held through a reset is not
lost.

The device is a host preference like the speed multiplier — `reset()` clears
the sticks but keeps the device, and neither is written into a save state. The
core starts every machine on an Apple joystick, so `main.js` pushes the
remembered choice back in after startup and again in `onMachineChanged()`.

Host-side, `src/js/input/game-port.js` owns the selection, its storage and the
mapping from a browser gamepad to five switches (unit-tested in
`tests/js/input/game-port.test.js`); `JoystickWindow` shows one panel per
device and `GamepadHandler` now tracks *every* connected pad rather than the
first, because the Joyport takes two. A single pad drives both sticks — a
one-player game that happens to read stick 2 then still plays, which is worth
more than a dead second stick. An opposing pair is dropped rather than sent:
a real gate cannot close left and right at once, and a program that saw both
would take whichever it tested first.

`test_joyport.cpp` pins the table above and `test_emulator.cpp` pins it through
the machine's own read path.

## CPU Speed

**View > CPU Speed** picks 1x/2x/4x/8x of the 1.023 MHz clock. The mechanism
was already in the core: `generateStereoAudioSamples()` runs
`samples * CYCLES_PER_SAMPLE * speedMultiplier_` cycles for a fixed number of
samples, so audio keeps pacing the machine and the picture keeps arriving at
60fps — the emulator just covers more emulated time per frame, and the speaker
rises in pitch along with it, like an accelerator card.

**Both sound sources measure their rate in CPU cycles, so both must be told the
speed.** `Emulator::applySpeedToAudio()` pushes it to each on every change and
on card insertion. `Audio::generateStereoSamples()` clamps a buffer whose cycle
span looks implausible, and that "expected" span scales with the multiplier —
left at 1x it discarded all but the last eighth of an 8x buffer and replayed the
remainder at real-time pitch, which is heard as sound that cuts out or refuses
to speed up. `MockingboardCard` emits one frame per `cyclesPerOutputSample_`,
also scaled — otherwise it produced eight frames for every one the mixer
consumed and the backlog grew without bound, heard as normal-pitch music
falling further and further behind. `test_audio.cpp` and `test_mockingboard.cpp`
pin both.

`src/js/ui/emulation-speed.js` owns the selection (localStorage, unit-tested in
`tests/js/ui/emulation-speed.test.js`); `UIController.setupSpeedSelector()`
wires the menu and `ScreenWindow.setSpeedState()` shows a title-bar chip above
1x.

Two things are deliberate. `Emulator::reset()` does **not** clear
`speedMultiplier_` — it is a host preference (or a paste boost in flight), not
machine state, so a reboot must not drop the user back to 1 MHz;
`test_emulator.cpp` pins this. And the selector writes through
`InputHandler.setBaseSpeed()`, which records the new baseline instead of
touching WASM while a paste boost is live — otherwise `restorePasteSpeed()`
would put the old speed back when the paste ended. The multiplier is not part
of a save state.

## Keyboard Shortcuts

| Shortcut         | Action                   |
| ---------------- | ------------------------ |
| F1               | Open/close Help window   |
| Ctrl+Escape      | Exit full page mode      |
| Ctrl+V           | Paste text into emulator |
| Ctrl+`           | Open window switcher     |
| Option+Tab       | Cycle to next window     |
| Option+Shift+Tab | Cycle to previous window |
| F5               | Run / Continue execution |
| F10              | Step Over                |
| F11              | Step Into                |
| Shift+F11        | Step Out                 |

**Which host key is Open Apple is the machine's choice.** On the 8-bit
machines the two Option keys are the Apple keys — left Open, right Closed — and
⌘ is left to the browser. A IIgs's keyboard is a Mac's: ⌘ *is* its Open Apple
and Option its Closed Apple, and GS/OS drives its menus with ⌘-letter, so on
that machine the emulator takes ⌘ while it has the keyboard. **View > ⌘ as
Open Apple** is the switch, remembered per machine
(`src/js/input/apple-keys.js`, default on for the IIgs only, unit-tested). With
it on, `InputHandler.translateAppleKeys()` sends ⌘ to the core as the left Alt
and either Option as the right, so the core's Apple-key tracking needs no
second mapping; every ⌘ combination is `preventDefault`ed (a browser still
keeps ⌘W, ⌘Q and the like for itself, which is why this is a choice); and
because macOS delivers no key-up for a key let go while ⌘ is held, the keys
pressed under ⌘ are released when ⌘ is, or AKD would stay high.

**The rest of the mapping is the same on every machine, and the differences
are the machine's.** `keyboard.cpp` maps Backspace to left arrow (`$08`,
which is how Applesoft deletes), forward Delete to `$7F` (the key marked
DELETE), the numeric keypad to what the number row types, and Control with
`@ [ \ ] ^ _` to `$00` and `$1B-$1F` as the encoder does, with the 2, 6 and
- keys reading as `@`, `^` and `_` under Control whether or not Shift is held. The core only ever
hears the Apple keys as the two Alt keys and ignores the Meta keys: on the
8-bit machines the host blocks ⌘ and the Windows key, and on a IIgs it sends
⌘ as the left Alt, so nothing left to the browser can press an Apple key. A
II+ has no lower case (`caps.hasLowercase`, applied through
`Keyboard::setUppercaseOnly`) and no Apple keys, so the Alt keys are its two
pushbuttons. A //c has the shift-key modification built in, so
`MouseIOU::setShiftKey` pulls `$C063` low for Shift as well as the mouse
button; the Enhanced //e does not have it, so there `$C063` stays the game
port's third button. Ctrl+Pause/Break is Ctrl+Reset on keyboards that have
the key. `test_keyboard.cpp`, `test_mouse_iou.cpp` and `test_emulator.cpp`
pin all of it.

The Joystick window has a **Cursor Keys** toggle that also drives the joystick from the arrow keys (full deflection 0/255 per axis). The arrows keep reaching the emulator's keyboard as normal, so ProDOS selectors, catalog menus and BASIC line editing still work while the toggle is on. When enabled, a "CURSOR KEYS" chip appears in the Monitor title bar. The same toggle is in the View menu (`btn-cursor-keys-joystick`), which is how it is reached in the layouts that have no Monitor title bar; menu item, header switch and state restores are kept in sync through `JoystickWindow.onCursorKeysChanged`. The setting persists via localStorage.
