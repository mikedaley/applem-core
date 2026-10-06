# Display, CRT shader and colour decoding

Moved out of CLAUDE.md; this is the detail behind the rules summarised there.

## Theming

Light, dark, and system-follow themes controlled by `ThemeManager` (`src/js/ui/theme-manager.js`). Sets `data-theme` attribute on `<html>` for CSS variable switching. All accent and syntax highlighting colours are derived from the six-stripe Apple rainbow logo palette (Green `#61BB46`, Yellow `#FDB827`, Orange `#F5821F`, Red `#E03A3E`, Purple `#963D97`, Blue `#009DDC`), with brightness adjusted per theme for contrast. Speaker, Mockingboard, and disk drive sound volumes are all wired to a single main volume slider with a unified mute toggle.

Control sytles, sizes and layout must be consistent across the entire app.

**Window surfaces are opaque and carry no `backdrop-filter`.** Use the `--glass-bg`, `--glass-bg-solid` and `--glass-bg-header` tokens for any window, panel, menu or popout background; they are fully opaque in both themes despite the legacy names. Do not reintroduce translucency or blur on these surfaces: they sit over a canvas that repaints 60 times a second, so a backdrop filter forces the compositor to re-blur the full area of every open window on every frame regardless of whether its content changed. Translucency is still correct for two things — dimming scrims behind modals and the window switcher, and accent-tinted inner chips (CPU flags, soft switch badges) layered on an already-opaque window.

## Display / CRT Shader

`public/shaders/crt.glsl` is the whole picture pipeline. Three things in it are load-bearing and must not be undone:

**Animated effects are bounded by the photosensitive-epilepsy limits.** No full-screen luminance modulation may exceed three flashes per second or a 10% relative luminance change (WCAG 2.3.1). `flicker()` is a slow two-sine undulation at 3% amplitude for this reason, and the full-screen TV static that used to play while the machine was off was removed outright — it ran at 50Hz with a 12Hz brightness modulation on top. The constraint is documented in the functions themselves; read those comments before touching them.

**The mask is in physical screen space, the beam is not.** `shadowMask()` derives position from `gl_FragCoord` divided by `u_pixelRatio` — a mask has a fixed pitch in millimetres, so it must neither resize with display density nor move when jitter and horizontal sync displace the picture. Effects that model the *signal* take the distorted UV; effects that model the *glass* do not.

**Phosphor persistence is exponential and per-phosphor.** `burnin.glsl` decays each channel by `exp(-dt/tau)` against real elapsed time, not a per-frame subtraction — the pass is throttled to every fourth frame, so a per-frame decay tied persistence to frame rate. In colour mode green holds longest and blue fades quickest, which tints a moving trail green; in monochrome modes one phosphor means one rate for all channels, held longer.

**Scanlines model a beam spot, not a stripe pattern.** `scanlines()` takes the displayed luminance and widens its Gaussian with it, because a CRT beam grows with current. The framebuffer is 560x384 (280x192 doubled), so a scanline pitch is two texel rows — 192 lines.

The powered-off screen is built by `src/js/display/no-signal-frame.js` as an ordinary 560x384 RGBA framebuffer and uploaded as the source texture, so it passes through the whole CRT chain like emulator video. `WebGLRenderer.updateTexture()` ignores emulator frames while it is displayed.

`WebGLRenderer.draw()` re-derives the drawing buffer size when `devicePixelRatio` changes, because a density change alters no CSS size and so never reaches the `ResizeObserver` that drives `resize()`.

## Composite Video / Colour Decoding

The //e emits no pixels. It emits one bit per 14.31818 MHz dot, four dots to a
cycle of the 3.579545 MHz colour subcarrier, and every colour is manufactured by
the *receiver*. `video.cpp` therefore renders in two stages: the mode emitters
(`emitText40Scanline`, `emitHiResScanline`, …) write a 1-bit dot stream into
`dots_`, and `endScanline()` hands the finished line to one of four decoders in
`ntsc.cpp`. A visible line is 560 dots and the framebuffer is 560 wide, so the
signal maps 1:1 onto pixels and nothing is ever resampled.

Four things here are load-bearing:

**The luma filter must null both 3.58 MHz and 7.16 MHz exactly.** A flat LORES
colour *is* a subcarrier-frequency pattern, so subcarrier leaking into luma makes
greys ripple; leakage at the second harmonic makes a colour's brightness depend
on which subcarrier phase a dot lands on, which shows up as faint banding. A
four-tap boxcar, integrating exactly one colour cycle, annihilates both, and it
is now the whole filter. Do not replace it with a plain low pass, and do not
cascade anything on top of it without a reason: it used to be followed by an
11-tap 4 MHz windowed sinc, and that cascade was where a composite picture's
extra softness came from. The two agree to within a decibel below 3 MHz, so the
sinc bought nothing where the shape of a character lives; what it did was take
13 to 30 dB out of the 4 to 6 MHz band, which carries the edges. The boxcar
alone is also the more faithful model, because a period set's luma path was a
trap at the subcarrier rather than a brick wall at 4 MHz, and nothing above
7.16 MHz exists in the input to leak back in. `hannSinc` is kept, unused and
marked so, because it is the right tool if a future change does need shaping.

**The calibration constants in `ntsc.hpp` were fitted, not chosen.**
`BURST_PHASE`, `CHROMA_GAIN` and `LUMA_GAMMA` come from a least-squares fit
against the physically self-consistent entries of the Apple II palette: black,
white, both greys, and the four single-bit LORES hues, whose phases a real
decoder fixes exactly 90 apart. The multi-bit palette entries were excluded
because they had been hand-tuned and sit 17-24 degrees off. Two independent fits
agreed on the phase to a quarter of a degree.

**The HIRES high bit is a one-dot delay, not a palette swap.** It pushes the
byte's seven pixels half a HIRES pixel right, landing them on the opposite
subcarrier phase; that *is* the mechanism behind orange and blue. The vacated dot
holds the shift register's previous output rather than going blank.

**Double-resolution modes are one dot later than 40-column ones.** The
80-column shift path is clocked a dot behind, so the same pattern comes out a
quarter turn round the colour wheel (`DOUBLE_RES_DELAY` in `video.cpp`). This is
the fact the old `DLGR_COLORS` table encoded as a copy of `LORES_COLORS` with the
nibble rotated left by one. Get it wrong and every DHGR picture is hue-rotated
by 90 degrees — subtle enough to look plausible, so `test_video.cpp` pins it.

**Colour burst is per scanline, but the colour killer is per field.**
`burstForScanline()` models the machine: a //e inhibits burst in text mode,
including the bottom four rows of a mixed screen. `chromaEnabled_` models the
monitor, and it is the flag the decoders actually receive. A real colour killer
integrates burst presence over a time constant far longer than one line, and the
3.58 MHz reference flywheels through gaps, so chroma is switched on or off a
whole field at a time.

Both halves are needed to get the two observable behaviours right. Full text mode
sends no burst at all, the killer engages, and text is crisp white. Mixed mode
sends burst on 160 of 192 lines, so the killer never engages and the four text
rows at the bottom **fringe green and violet along with everything else** — which
is what real hardware does, and what a per-line gate would wrongly suppress. The
same mechanism explains why a II+, which never inhibits burst, fringes its text
in every mode. It also means 80-column text on the Composite preset is genuinely
mushy, exactly as it was on real hardware.

`VideoColorMode` (types.hpp) selects the decoder: MONOCHROME (dots straight to
one phosphor), PIXEL_EXACT and RGB_MONITOR (idealised, see below), COMPOSITE
(full demodulation) and SOLID, which is not a receiver at all — it paints each
cell the colour its value names, over its own dots and no further, so nothing
fringes. Every mode carries that colour out of band in `cellColour_`, set by
the emitter: LORES, DLORES and DHGR from the value a cell holds, HIRES from a
rule applied to the bits (below). The composite decoder is a 512 KB lookup table indexed by a
15-dot window and the subcarrier phase — an exact memoisation of the FIR, not an
approximation, which `test_ntsc.cpp` verifies over all 131072 entries.

**The sharp modes apply no composite effects whatsoever, and that is a
requirement rather than a nicety.** `decodeIdeal()` is not a demodulator and must
never become one: an unlit dot is black, and colour never extends past the pixels
that are actually lit. An earlier version slid a four-dot window over every dot,
which painted colour onto dots that were off and past the edges of white blocks —
a composite artifact in a mode whose whole purpose is not having any.

Because the signal alone cannot say what a pattern means — a flat LORES cell and
a lit HIRES pixel can carry identical dots — each emitter tags its dots with an
`ntsc::IdealKind`. The two kinds name mechanisms rather than modes, because the
split does not fall along mode lines:

- `CELL` — one flat colour across the aligned four-dot group. LORES, DLORES and
  DHGR, whose dots encode an actual colour value.
- `DOT_GATED` — unlit dots are black, lit ones take the artifact colour their run
  implies. Text, whose dots are drawn shapes rather than an encoded colour, and
  which on real hardware picks up artifact colour the same way.
- `DOT_GATED_CELL` — HIRES, which is both at once and which one depends on who is
  looking. Every receiver treats it exactly as `DOT_GATED`; only `SOLID` reads it
  differently. A HIRES picture is a drawn shape to a monitor, but the artist chose
  those dots *for* their colour — a solid violet field is `$55`/`$2A` alternating
  and lights only half the dots, so gating on lit dots paints it as violet
  stripes on black rather than as the violet field that was drawn.

**The HIRES rule for SOLID lives in `Video::emitHiResScanline`, in pixels, not
dots.** A lit pixel beside a lit pixel is white over its own two dots. A lit
pixel on its own is its column's colour — violet or green, blue or orange if its
byte's high bit is set — painted over its whole *pair*, unlit partner included,
which is what makes a `$55`/`$2A` field one colour with no stripes. Everything
else is black. White decided per pixel and colour per pair is what keeps both
true: a pair rule alone leaves a coloured end on every odd-width white stroke,
and a pixel rule alone paints stripes through every field. It has to be the
emitter and not the decoder because only the emitter knows which byte a pixel
came from, and by the time the dots reach `ntsc.cpp` the high bit has become a
half-dot shift and the byte boundaries are gone.

**It took four goes, and each failure looked plausible.** Worth knowing before
touching it:

- *Reading each four-dot group as a palette index* is the CELL rule, and it is
  wrong for a shape: white is a RUN of three or more, but a group reads as white
  only when all four of its own dots are lit, so a three-dot stroke across a
  group boundary came out aqua on one side and brown on the other. A hi-res
  title screen rendered as green and magenta confetti.
- *Filling every coloured group* then doubled every isolated pixel, because a
  lone two-dot pixel at the edge of a letter is also half a group. Hence the
  field test.
- *Counting a run in both the groups it straddles* let one pixel paint eight
  dots, which closed the gaps between letters drawn in colour and ran a word
  into a solid slab. Hence one run, one group.
- *Leaving lone pixels the colour the sharp decoders give them* kept the flecks,
  only thinner: one title screen carries 185 two-dot runs and every one is a
  single-pixel feature of a letter meant to be white.

The cost is deliberate and worth stating: a genuinely intended one-pixel colour
detail, with no colour beside it, comes out white. A mode that cannot tell that
apart from a letter's serif has to choose, and this one is called Solid
**Colour**, not Solid Artifact.

`DOT_GATED` decides between an artifact colour and white by **run length**, not
byte alignment: a run of two lit dots is one isolated pixel (or one text stroke)
and takes a colour, three or more reads as white. Run length is used precisely
because it stays correct across the high bit's half-dot shift without needing to
know where byte cells begin.

Text therefore carries NTSC colour in the sharp modes too, whenever the burst is
live — mixed-mode text fringes green and violet exactly as it does under the
demodulator. What the sharp modes do differently is refuse to let that colour
spread: the background stays pure black and the strokes keep hard edges. Full
text mode kills the burst, so an all-text screen is still crisp white.

Display Settings (`src/js/display/display-settings-window.js`) leads with a **Monitor preset** — Pixel Exact, Composite Color, RGB Monitor, Monochrome Green, Monochrome Amber — with every individual slider behind an Advanced disclosure. Each preset also carries a `colorMode`, which selects the core decoder above. Presets set only the picture, never the user's brightness/contrast/saturation, bezel or screen border; editing a setting a preset owns relabels the selection Custom without changing values.

**Display settings are remembered per machine** (`src/js/display/display-storage.js`, unit-tested): a //e's composite look for games has no business on a IIgs's RGB desktop. Each machine has its own localStorage key; the pre-machine key is read once as the //e's. Each machine's defaults differ in one value, the **Screen Border**: 35% on the 8-bit machines, whose picture fills the frame, and 0 on a IIgs, which draws its own border. Saved display profiles stay global — they are named snapshots any machine may pick.

**Display profiles.** Beyond the built-in presets, the user can save the current
picture as a named profile (`src/js/display/display-profiles.js`, unit-tested in
`tests/js/display/display-profiles.test.js`). Profiles live under their own
localStorage key, so Reset to Defaults does not take them with it, and they are
identified by name — saving over a name replaces it.

Two things differ from a built-in preset, both deliberate. A profile captures
*everything*, brightness, contrast, saturation and bezel included: a built-in
imitates a monitor and has no business resetting someone's calibration, but a
profile is a snapshot of a picture the user liked, so restoring it has to give
that picture back whole. And editing a profile keeps it selected and marks it
modified, rather than dropping to Custom the way a built-in does — losing the
name would leave Save nothing to write back to and force a Save As for every
tweak. Save is enabled only while a selected profile is dirty.

There is deliberately no NTSC Fringing slider. One existed when the shader faked
composite artifacts by tinting detected edges; the core now produces real
fringing from the real signal, so a shader knob for it would only double-count.
