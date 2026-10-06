# Disks: WOZ flux and the Disk Inspector

Moved out of CLAUDE.md; this is the detail behind the rules summarised there.

## WOZ flux tracks

A WOZ 2.1 image can hold some tracks as flux timings (the FLUX chunk: a
second quarter-track map, over TMAP, naming TRKS entries whose bytes are
125ns intervals between transitions, 255 carrying into the next byte).
**They are played back by time, not converted to bits**, because the
difference can be the copy protection: Sirius's Bandits writes parts of
each track at 3.7us a cell and parts at 4.1us, and its loader times its latch
reads to tell them apart. The bits are identical either way; converted to a
bit stream, every one of those tracks failed its checksum and the boot
retried track 1.5 for ever.

`WozDiskImage::fluxToPulses` turns a track into a pulse stream with one bit
per tick of the sequencer's clock (7 master cycles, exactly 45/176 of a flux
tick), and `DiskImage::isTickTimed()`/`readTick()` let `clockLSS` take a pulse
on whichever of its eight ticks it arrives, as the real P6 sequencer does,
instead of one bit at phase 4. Three rules follow from it:

- **The flux bytes are kept and saved as flux.** `exportData` writes a FLUX
  chunk (on a block boundary, which INFO names) and TRKS covers its track
  data. A save state carries the disk through `exportData`, so a state of a
  flux disk that came back as bits would not boot.
- **Writing to a flux track turns it into bits**, because what a drive lays
  down is bits (`convertFluxTrackToBits`, interval rounding at the machine's
  31.29-tick cell).
- **The head keeps its angle** crossing between a flux track (position in
  ticks) and a bit track (in cells): `moveHeadTo` rescales.

`test_woz_disk_image.cpp` pins the timing, the precedence over TMAP, the
save and the write. `test_emulator_disk.cpp` boots Bandits through a save
state when `A2E_BANDITS_WOZ` points at the image, which is not in the
repository, and fails at track 1.5 without the tick-timed sequencer.

## Disk Inspector

**View > Disk Inspector** shows what is recorded on a disk in either drive,
whatever the image format: a platter with every quarter track a ring,
coloured by what is on it and turning under the emulated head while the
motor runs; one track unrolled as a zoomable strip (scroll to zoom down to
single flux pulses, drag to pan); the sectors in the order they pass the
head, each one's decoded bytes, and the raw nibbles. **Timing** recolours by
how long each cell took, which is how a flux track written at more than one
speed shows itself.

**Every format is inspected in one currency, bit cells.**
`DiskImage::inspectQuarterTrack` hands over a quarter track as the drive
would read it: a sector image encodes its track, a WOZ bit track is what it
is, and a flux track is resolved into cells with the time each took.
`core/disk-image/disk_inspection.*` then reads those cells the way the
drive's latch does and names every nibble (sync, address and data fields and
their marks, checksums, well-formed bytes in no standard field, and noise).
It reads two revolutions and keeps the second, so the framing has settled
and a sector across the end of the track reads whole.

**Two buffers, one round trip each**, laid out in `disk_inspection.hpp`:
`_getDiskOverview` is the whole disk in equal arcs per ring for the platter
(about 250KB, analysing each stored track once however many quarter tracks
read it), and `_getDiskTrackDetail` is one quarter track in full.
`src/js/disk-manager/disk-inspector-data.js` parses both (unit-tested);
`disk-inspector-window.js` draws them. The window re-reads only when
`DiskController::getRevision` moves (insert, eject, a write through the
head, or anything taking the writable image), at most twice a second while
a disk is being written; the export goes through the *const* image accessor
for that reason, because the writable one counts as a change.

**The platter is painted once and rotated.** Each pixel of an offscreen
canvas finds its quarter track from its radius and its arc from its angle;
a frame is then one `drawImage`. A quarter track with nothing mapped next to
one that has data is drawn faded, because the head reads a track from the
quarter track either side, and a disk recorded on half tracks otherwise
looks nearly empty. `test_disk_inspection.cpp` pins the analyser and both
buffers.

**Zoomed in, the platter holds still and the head goes round it**, because a
view at 50x turning five times a second shows nothing. The view is painted
fresh for each change of zoom or pan, and once few enough rings are in view
(`MAX_RINGS_IN_FULL`) each is read in full through `_getDiskTrackDetail` and
drawn cell by cell, with values along the ring and flux transitions across
it. **Track details are read one at a time** (`_readTrackDetail`): the core
answers every track into the same buffer, so two reads in flight could each
copy out the other's track.
