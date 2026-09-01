# exTracker

A Linux-first, pattern-based music tracker with real-time audio engine. Ships a CLI (`extracker`) and a JUCE-based GUI (`extracker_gui`).

---

## Build & Run

```bash
# Configure
cmake -S . -B build-make -G "Unix Makefiles"

# Build CLI only (fast, no JUCE download)
cmake --build build-make --target extracker

# Build GUI (downloads JUCE 7.0.10 on first run — slow once)
cmake --build build-make --target extracker_gui

# Run CLI
cd build-make && ./extracker

# Run GUI (auto-connects Keystation 88 MK3 if present)
./launch-gui.sh

# Run tests
ctest --test-dir build-make --output-on-failure
```

Audio backends are auto-detected: **PipeWire → JACK → ALSA → Null**.

---

## GUI Overview

Three toolbar rows across the top:

| Toolbar | Contents |
|---------|----------|
| 1 | Play · Stop · Record · Rec > · Overdub · Punch · Loop · Pattern/Song mode · Status |
| 2 | Tempo · Swing · Ticks/Beat · Ticks/Row · Pattern label · Pattern selector · Insert Before/After · Remove |
| 3 | New Song · Save Song · Load Song · Expand/Shrink rows/channels · Insert/Remove row · Help · Grid density · Dark mode · Keys |

The **right panel** (scrollable) switches between tabs: Instruments, Samples, Sample Editor, Plugins, Song Order, Module, Search/Compare.

**New Song** resets tempo, clears all instrument assignments, and applies the selected startup template (Blank, House, or Electro).

The GUI **auto-loads** the last used song on startup.

---

## Instruments Panel

- **Slot selector** — choose instrument slot I0–I15.
- **Plugin selector + Assign Plugin To Slot** — assign a scanned VST3, LV2, SF2, or SFZ plugin.
- **Load Instrument File** — load `.xpm`, `.s3i`, `.xi`, `.iff/.8svx`, or `.sf2` (opens key-picker for melodic SF2 mode).
- **Open Plugin Editor** — opens native VST3 editor window (VST3 only; LV2 plugins show parameter sliders in this panel instead).
- **Parameters section** (scroll down) — labeled sliders for LV2 control ports (e.g. Cutoff, Resonance, ADSR).
- **Gain / Attack / Release** — envelope for all instrument types.
- **Root / Pan / Loop** — sample-specific settings (builtin.sample only).
- **Keystation >** (Samples panel) — assigns a loaded WAV sample slot to MIDI keyboard playback.

### Instrument types

| Type | How to load | Notes |
|------|-------------|-------|
| `builtin.sine` / `builtin.square` | Assigned by default to I0/I1 | Simple oscillators |
| VST3 | Scan Plugins → Assign Plugin To Slot | Full audio + native editor |
| LV2 | Auto-scanned from `~/.lv2` and `/usr/lib/lv2` | Audio + parameter sliders |
| SF2 (standard) | Scan Plugins → Assign Plugin To Slot | Full GM soundfont via FluidSynth |
| SF2 Melodic | Load Instrument File → `.sf2` → key-picker | Drum key played chromatically across keyboard |
| SFZ | `plugin assign <i> sfz:/path/file.sfz` | SFZ scripted sampler via sfizz |
| S3I (OPL2 FM) | Load Instrument File → `.s3i` | Software FM synthesis, 9-voice poly |
| XPM / F9 | Load Instrument File → `.xpm` | Multi-zone sampler, WAV loop sustain |
| WAV sample | Samples panel → Load WAV → Keystation > | Pitch-shifted per MIDI key |

### SF2 Melodic mode

Each key in a drum soundbank is a different sound. SF2 melodic mode locks to one of those keys and plays it chromatically across the full keyboard using pitch bend.

**CLI:**
```
# Find which keys have sounds in a drum SF2
plugin zones /path/to/DrumKit.sf2
→ 61 key(s) with samples in DrumKit.sf2:
  key 36  (C2)  →  plugin assign 0 sf2:/path/to/DrumKit.sf2:melodic:36
  key 38  (D2)  →  plugin assign 0 sf2:/path/to/DrumKit.sf2:melodic:38
  ...

# Assign (auto-detects and registers factory on-demand)
plugin assign 0 sf2:/path/to/DrumKit.sf2:melodic:36
```

**GUI:** Load Instrument File → pick `.sf2` → a dialog shows all available drum keys → select one → click "Load Melodic".

The melodic plugin ID is saved in the song file and restores correctly on load.

---

## Recording

### Toolbar buttons

| Button | Action |
|--------|--------|
| **Record** | Toggle record on/off |
| **Rec >** | Jump transport to record cursor row, start playing, enable record |
| **Overdub** | Toggle overdub (write over existing notes vs. skip filled rows) |
| **Punch: Off** | Toggle punch-in/out (only records between punch in/out rows) |

### CLI record commands

```
record on / off           — enable/disable recording
record play               — jump to cursor row, start playback, enable record
record channel <ch>       — set record channel (0-based)
record cursor <row>       — set cursor to absolute row
record cursor +N / -N     — move cursor relative
record cursor start/end   — jump to first/last row
record jump <N>           — advance cursor by N rows after each note (default 1)
record quantize on/off    — quantize to transport row when playing
record overdub on/off     — overdub mode
record punch <in> <out>   — set punch range (rows)
record punch off          — disable punch
record punch status       — show current punch range
record undo / redo        — undo/redo last recorded note
```

### Recording modes

| Mode | How | Behavior |
|------|-----|----------|
| **Step record** | Transport stopped, Record on, play notes on keyboard | Each note goes to cursor row, cursor advances by jump amount |
| **Live / quantized** | Record on, press Play (or Rec >) | Notes quantized to current transport row as pattern plays |
| **Overdub** | Overdub: On + any mode | Writes over existing notes instead of finding next empty row |
| **Punch-in/out** | Punch range set, transport playing | Only records while transport is between punch in and punch out rows |

**Tip:** Press `record cursor 0` (CLI) before starting to place notes from row 0.

---

## Pattern Grid

```
note set <row> <ch> <midi> <instr> [vel] [fx] [fxval]
note clear <row> <ch>
note vel <row> <ch> <vel>
note gate <row> <ch> <ticks>
note fx <row> <ch> <fx> <fxval>
```

All note commands support `dry [preview [verbose]]` to preview without writing.

### Pattern views

```
pattern print [from] [to]           — print pattern as text table
pattern display [from] [to]         — compact colored display
pattern watch [interval_ms]         — live-updating display (Ctrl+C to stop)
pattern play [from] [to] [step N]   — play a row range in isolation
```

### Pattern bulk operations

All bulk ops accept `dry [preview [verbose]]` first, then `[from] [to] [ch] [step N] [chance P]` range/stride/probability filters.

```
pattern transpose <semitones>       — shift all notes by semitone offset
pattern velocity <percent>          — scale velocities (100 = unchanged)
pattern gate <percent>              — scale gate lengths
pattern effect <fx> <fxval>         — fill effect command/value across rows
pattern scale-duration <percent>    — scale gate+spacing together
pattern invert-notes [centerNote]   — mirror notes around center pitch
pattern filter-notes <min> <max> [minVel] [maxVel] [delete]  — keep/remove by note or velocity range
pattern humanize <velRange> <gateRangePct> <seed>            — add random variation around current values
pattern randomize <probabilityPct> <seed>                    — randomize velocity/effect per step by probability
pattern copy <from> <to> [chFrom] [chTo] [step N]
pattern paste <destRow> [chOffset] [step N]
pattern undo / redo
```

### Pattern management

```
pattern ls                    — list all patterns
pattern dup [index]           — duplicate current pattern (or by index)
pattern sw <index>            — switch to pattern by index
pattern in <before|after>     — insert new pattern before/after current
pattern del                   — delete current pattern
pattern insert-swing <on|off|status>  — inherit swing setting from previous pattern
pattern template <blank|house|electro>  — fill with a starter groove
```

### Song order

```
song ls                       — list song order
song status                   — show current position and play mode
song pos                      — show current song position
song set <entry> <pattern>    — change pattern at song entry
song insert <entry> <pattern> — insert entry at position
song append <pattern>         — append pattern to end of song
song remove <entry>           — remove entry from song order
song move <entry> <up|down>   — reorder entries
song goto <entry>             — jump to song position
song first / last             — jump to first/last entry
song next / prev [wrap]       — advance/retreat one entry
song play <pattern|song|status>  — set or query playback mode
```

---

## Effects Reference

Effect memory is active: a zero value repeats the last non-zero value for that effect channel.

### Standard effects (column `FX`)

| Code | Name | Value |
|------|------|-------|
| `00` | Arpeggio | `XY` = semitone offsets (X up, Y up from root) |
| `01` | Pitch slide up | amount per tick |
| `02` | Pitch slide down | amount per tick |
| `03` | Tone portamento | slide to note at this speed |
| `04` | Vibrato | `XY` = speed (X) / depth (Y) |
| `05` | Portamento + volume slide | portamento continues; `XY` = vol slide |
| `06` | Vibrato + volume slide | vibrato continues; `XY` = vol slide |
| `07` | Tremolo | `XY` = rate (X) / depth (Y) |
| `08` | Set pan | 0 = left, 80 = center, FF = right |
| `09` | Retrigger | retrigger every N ticks |
| `0A` | Volume slide | hi nibble = up, lo nibble = down |
| `0B` | Pattern jump | jump to row N in current pattern |
| `0C` | Set velocity | 0–127 |
| `0D` | Pattern break | break to row N of next pattern |
| `0F` | Speed / Tempo | `< 32` = ticks per row, `≥ 32` = BPM |
| `14` | Fade-out note-off | note-off fades out instead of hard cut |
| `17` | Set ticks per beat | sets transport ticks-per-beat |
| `E6` | Pattern loop | `E60` = mark loop start; `E6N` = loop N times |
| `EF` | Carry | carry effect memory to next row |

### Extended effects (`0E` — value = `XY` where X = sub-command, Y = value)

| Code | Name | Notes |
|------|------|-------|
| `0E0` | Filter toggle | `0E00` = off, `0E01` = on (legacy per-channel) |
| `0E1` | Fine slide up | pitch up by Y semitone-cents |
| `0E2` | Fine slide down | pitch down by Y semitone-cents |
| `0E3` | Glissando | `0E30` = off, `0E31` = on (snap portamento to semitones) |
| `0E4` | Vibrato waveform | 0 = sine, 1 = ramp, 2 = square |
| `0E5` | Fine tune | semitone offset ±7 (8–F = negative) |
| `0E6` | Pattern loop | `0E60` = set start, `0E6N` = loop N times |
| `0E7` | Tremolo waveform | 0 = sine, 1 = ramp, 2 = square |
| `0E8` | Fine pan | 0–F mapped to L–R (16 steps) |
| `0E9` | Retrigger | retrigger every Y ticks |
| `0EA` | Fine volume up | add Y to velocity |
| `0EB` | Fine volume down | subtract Y from velocity |
| `0EC` | Note cut | silence note at tick Y |
| `0ED` | Note delay | delay note trigger by Y ticks |
| `0EE` | Pattern delay | hold current row for Y extra rows |
| `0EF` | Funk repeat | retrigger on every following row |

### Per-step filter / instrument-effects codes

These are written directly as the `fx` value in a note step and control the instrument's live effects chain:

| Code | Effect | Value |
|------|--------|-------|
| `18` | Filter type | 0 = off, 1 = LP, 2 = HP, 3 = BP, 4 = Notch |
| `19` | Filter cutoff | 0–255 (normalized 0.0–1.0) |
| `1A` | Filter resonance | 0–255 |
| `1B` | Delay time | 0–255 → 0–1000 ms |
| `1C` | Delay feedback | 0–255 → 0–0.98 |
| `1D` | Delay wet | 0–255 → 0.0–1.0 |
| `1E` | Distortion type | 0 = off, 1 = soft, 2 = hard, 3 = fuzz |
| `1F` | Distortion drive | 0–255 |
| `20` | Chorus rate | 0–255 → 0.05–5 Hz |
| `21` | Chorus depth | 0–255 |
| `22` | Chorus wet | 0–255 |
| `23` | Surround depth | 0 = front, 255 = rear |

---

## Instrument Effects CLI

Per-instrument effect chain (delay, distortion, chorus) and global reverb bus.

```
effects delay <instr> <time_ms> <feedback 0-255> <wet 0-255>
effects dist <instr> <off|soft|hard|fuzz> <drive 0-255>
effects chorus <instr> <rate 0-255> <depth 0-255> <wet 0-255>
effects depth <instr> <0-255>        — surround depth (0=front, 255=rear)
effects clear <instr>                 — clear all effects for instrument
effects reset [<instr>|all]           — reset effects, filter and pitch
effects get <instr>                   — show current settings
effects list                          — list all instruments with active effects

filter set <instr> <off|lp|hp|bp|notch> <cutoff 0..1> <resonance 0..1>
filter clear <instr>
filter get <instr>
filter list

reverb set <room 0-255> <damp 0-255> <wet 0-255> [width 0-255]
reverb send <instr> <send 0-255>     — per-instrument reverb send level
reverb get
reverb clear
```

---

## Instrument Edit CLI

```
instrument edit info <instr>          — detailed state dump
instrument edit list                  — list editable parameter names
instrument edit set <instr> <param> <value>
instrument edit get <instr> <param>
instrument edit gain <instr> <0.0–2.0>
instrument edit attack <instr> <ms>
instrument edit release <instr> <ms>
instrument edit root <instr> <0–127>
instrument edit pan <instr> <L|C|R|0x##|0–255>
instrument edit loop <instr> <off|on|bidi|sustain> [start] [end]
```

---

## Samples Panel

```
sample load <slot> <name> <file.wav>     — load WAV into slot (0x000–0x0FF hex)
sample ls                                 — list loaded slots
sample info <slot>                        — show slot metadata
sample assign <slot> <instr>             — assign sample slot to instrument
```

### Sample Editor (CLI)

```
sample edit info <slot>
sample edit trim <slot> [dry] --from <val> --to <val> [--unit s|f]
sample edit normalize <slot> [dry] [--from <val>] [--to <val>] [--unit s|f]
sample edit fade <slot> <in|out> [dry] [--from <val>] [--to <val>] [--unit s|f]
sample edit reverse <slot> [dry] [--from <val>] [--to <val>] [--unit s|f]
sample edit resample <slot> [dry] (--rate <hz> | --factor <x>)
sample edit bitdepth <slot> [dry] <bits> [--from <val>] [--to <val>] [--unit s|f]
sample edit loop <slot> <on|off|bidi|sustain> [--start <val>] [--end <val>] [--unit s|f]
sample edit loop-crossfade <slot> [dry] [--len <val>] [--unit s|f]
sample edit volume <slot> <0.0–2.0>
sample edit pan <slot> <L|C|R|0–255>
sample edit transpose <slot> <semitones>
sample edit restore <slot> [dry]
```

`--unit s` = seconds, `--unit f` = frames. `dry` previews without writing.

`loop-crossfade` blends the loop seam (set a loop point first) for seamless loops.

---

## Plugin CLI

```
plugin scan                           — scan for LV2, VST3, SF2, SFZ plugins
plugin list                           — list discovered plugins
plugin load <id>                      — load plugin by ID
plugin assign <i> <id>               — assign plugin to instrument slot
plugin assign <i> sf2:<path>:melodic:<key>  — SF2 drum key as chromatic instrument
plugin zones <sf2-path>              — list which MIDI keys have samples in an SF2 drum bank
plugin set <i> <param> <value>       — set LV2 / VST3 parameter
plugin get <i> <param>               — get parameter value
plugin params <i>                    — list all parameters for instrument slot
plugin info <id>                     — show port layout for a plugin
plugin status                        — show instrument → plugin assignments
plugin preset save <i> <file>        — save plugin state preset
plugin preset load <i> <file>        — load plugin state preset
plugin effect list                   — show global effect chain slots (0–7)
plugin effect assign <s> <id>        — assign LV2 effect to chain slot
plugin effect remove <s>             — remove effect from chain slot
plugin effect set <s> <param> <val>  — set effect parameter
plugin effect get <s> <param>        — get effect parameter
```

---

## Module Message

A free-text note attached to the song file (composer notes, BPM info, etc.).

```
message set <text>    — set module message
message get           — print module message
```

In the GUI: type in the **Module message** text area in the Module tab.

---

## MIDI

```
midi instrument <N>           — route MIDI keyboard to instrument slot N
midi thru on/off              — pass MIDI notes through to audio engine
midi channel <ch>             — set MIDI receive channel (0 = omni)
midi learn on/off             — MIDI learn mode for channel mapping
midi transport sync on/off    — sync transport to MIDI clock
midi connect <client:port>    — connect MIDI source (wraps aconnect)
midi ls                       — list MIDI ports
midi quick                    — show MIDI status
```

### Play WAV samples from keyboard

```
sample load 5 kick /path/to/kick.wav
sample assign 5 3              — assign sample slot 5 to instrument slot 3
midi instrument 3              — route keyboard to instrument 3
midi thru on
```

Or in the GUI: load WAV in Samples panel → select slot → click **Keystation >**.

---

## Core / Transport CLI

```
p / play                      — start playback
s / stop                      — stop playback
st / status                   — show transport + pattern status
w / save <file>               — save song to .xtp file
r / load <file>               — load song from .xtp file
reset                         — reset pattern and state
tempo <bpm>                   — set tempo
tpb <N>                       — ticks per beat
tpr <N>                       — ticks per row
loop <from> <to>              — set loop range
loop off                      — disable loop
```

Multiple commands can be chained with `;`:
```
record cursor 0 ; record on ; p
```

---

## Save / Load

Song files use extension `.xtp` (`EXTRACKER_SONG_V1` format). The GUI remembers the last used file and reloads it automatically on next startup.

```
w mysong.xtp     — save
r mysong.xtp     — load
```

Instrument assignments (including SF2 melodic IDs) are stored in the file and restored on load.

---

## Keyboard Shortcuts (GUI)

| Key | Action |
|-----|--------|
| Space | Play / Stop |
| F5 | Play from beginning |
| Ctrl+S | Save song |
| Ctrl+O | Load song |
| Arrow keys | Move cursor in grid |
| Enter | Edit selected step |
| Delete | Clear selected step |
| Ctrl+Z | Undo |
| Ctrl+Y | Redo |

---

## CLI Aliases

Most commands have short aliases: `p` (play), `s` (stop), `st` (status), `w` (save), `r` (load), `pattern dup/sw/ls/del/in`, `song ls/st/se/si/ap/rm/mv/g/f/l/n/b/pl`, etc.
