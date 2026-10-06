# State serialization

Moved out of CLAUDE.md; this is the detail behind the rules summarised there.

## State Serialization

Binary format with a versioned header that every machine shares (magic,
version, machine id — see Machine Profiles). `src/core/emulator/state_stream.hpp`
is the `StateWriter`/`StateReader` pair every machine writes through, so the
rules — little-endian, a blob is its length then its bytes, a read past the end
fails once rather than crashing — are in one place; `drive_state.hpp` is the
two floppies, image and head position, shared by both machines.

**Apple II family** (`emulator_state.cpp`): CPU, 128KB RAM, both language
cards, the soft switches (packed and restored by `MMU::packSwitchesForState` /
`restoreSwitchesFromState`, which writes the switches' own addresses so
everything watching them sees the change), the keyboard latch and buttons,
then **every slot by card id with that card's own state** — so a state refits
the cards it was saved with, through `setSlotCard`, and an SSC, a parallel
card, a SoftCard, a //c's built-in ports and its IWM's mode register all come
back — then the disks, the No-Slot Clock, and a //c's IOU mouse with the steps
it had banked. A card's state is sized in 32 bits because a SmartPort card's
state is its hard drive images.

**IIgs** (`iigs_state.cpp`): the 65816 — mode first, then the flags, then the
registers, because `setEmulation` and `setP` each force the widths the mode
requires — then `IIgsMemory::serialize`: the fast RAM (refused on restore if a
different amount is fitted), the Mega II's RAM, language card and switches,
every register of the memory controller, the slow clock, and the devices,
each with its own `serialize`/`deserialize` — ADB (queues included), the clock
chip (battery RAM, seconds, a transaction in flight), the SCC's two channels,
the Ensoniq (RAM, oscillators, registers; not its output ring, which is the
host's backlog). Then the machine's own counters, the IWM, the floppies and
the SmartPort with its images. `test_iigs_state.cpp` round-trips each part.

**A card's state is written straight into the buffer, and the buffer is
reserved for it.** A SmartPort card's state is its hard drive images, so a
machine with two 32MB volumes writes a state of about 72MB. Serializing each
card into a temporary and copying that in held two further copies of the
payload at once, and the buffer's own growth doubled it again — about 190MB of
heap to write 72MB. That went past `MAXIMUM_MEMORY` and **aborted the module**,
which is worse than it sounds: an aborted module rejects everything asked of it
afterwards, so the symptom was every control in the app going dead rather than
one save failing. `StateWriter::blobFrom` writes the card's bytes in place and
patches the length to what `serialize` actually returned, both `exportState`s
reserve the card sizes up front, and the ceiling is 512MB. The host also
reports a failed save now: every notification in `handleSave` came after the
await, so a rejected save said nothing at all.

Autosave plus 5 manual save slots, stored in browser IndexedDB, each record
naming the machine that wrote it. **The autosave is per machine**
(`autosave:<key>`; the record from before there was more than one machine is
the //e's), because a state restores only into the machine that wrote it and
one shared autosave would come back to nothing for every machine but the last.
The Save States window labels a slot saved on another machine and, on Load,
asks before switching to it. Window option state (toggles, view modes, mute
states) is persisted separately via localStorage.
