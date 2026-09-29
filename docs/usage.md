# exTracker — Usage Guide

## Overview

exTracker is a Linux step-sequencer with a pattern-grid interface inspired by classic trackers.
Notes, volumes, and effects are entered directly in the grid using keyboard shortcuts.
Audio is rendered in real time through the built-in sample engine and/or JUCE audio plugins.

Run the GUI build:

```sh
./build-make-gui/extracker
```

Or the headless CLI build (for scripting and tests):

```sh
./build-make/extracker
```

---

## Pattern Grid — Cell Layout

Each cell in the pattern grid represents one step on one channel.  
Columns from left to right:

| Column | Width | Content |
|--------|-------|---------|
| Note | 3 chars | `C-4`, `D#3`, `---` (empty), `OFF` (note-off) |
| Sample | 3 hex | Sample slot number `000`–`0FF`, `---` if none |
| Velocity | 3 chars | `a7F` (velocity = 0x7F), `...` when at default (100) |
| Effect | 3 chars | Tracker effect: command nibble + 2-digit value, e.g. `F80` |

---

## Navigation

| Key | Action |
|-----|--------|
| Arrow keys | Move selection |
| Tab / Shift+Tab | Next / previous channel |
| Enter | Advance cursor by step |
| Home / End | First / last row |
| Page Up / Page Down | Jump 16 rows |
| Click | Select cell |
| Alt+Drag | Audition-scrub rows without writing notes |

---

## Note Entry

The keyboard is mapped as a two-octave piano layout.  
The **upper row** plays in the higher octave (oct+1); the **lower row** plays in the current octave.

### Semitone rows

| Keys | Notes |
|------|-------|
| `1 2 3 4 5 6 7 8 9` | Upper row semitones (oct+1, semitones 0–8) |
| `A S D F G H J K L` | Lower row semitones (oct, semitones 0–8) |

### Whole-tone rows
# exTracker — Usage Guide

## Overview

exTracker is a Linux step-sequencer with a pattern-grid interface inspired by classic trackers.
Notes, volumes, and effects are entered directly in the grid using keyboard shortcuts.
Audio is rendered in real time through the built-in sample engine and/or JUCE audio plugins.

Run the GUI build:

```sh
./build-make-gui/extracker
```

Or the headless CLI build (for scripting and tests):

```sh
./build-make/extracker
```

---

## Pattern Grid — Cell Layout

Each cell in the pattern grid represents one step on one channel.  
Columns from left to right:

| Column | Width | Content |
|--------|-------|---------|
| Note | 3 chars | `C-4`, `D#3`, `---` (empty), `OFF` (note-off) |
| Sample | 3 hex | Sample slot number `000`–`0FF`, `---` if none |
| Velocity | 3 chars | `a7F` (velocity = 0x7F), `...` when at default (100) |
| Effect | 3 chars | Tracker effect: command nibble + 2-digit value, e.g. `F80` |

---

## Navigation

| Key | Action |
|-----|--------|
| Arrow keys | Move selection |
| Tab / Shift+Tab | Next / previous channel |
| Enter | Advance cursor by step |
| Home / End | First / last row |
| Page Up / Page Down | Jump 16 rows |
| Click | Select cell |
| Alt+Drag | Audition-scrub rows without writing notes |

---

## Note Entry

The keyboard is mapped as a two-octave piano layout.  
The **upper row** plays in the higher octave (oct+1); the **lower row** plays in the current octave.

### Semitone rows

| Keys | Notes |
|------|-------|
| `1 2 3 4 5 6 7 8 9` | Upper row semitones (oct+1, semitones 0–8) |
| `A S D F G H J K L` | Lower
| Keys | Notes |
|------|-------|
| `Q W E R T Y U I O P` | Upper row whole tones (oct+1, white-key offsets) |
| `Z X C V B N M , . /` | Lower row whole tones (oct, white-key offsets) |

### Other note commands

| Key | Action |
|-----|--------|
| `+` / `-` | Octave up / down |
| `[` / `]` | Step size down / up |
| Shift+O | Write note-off (fadeout) — uses current Gate ticks |
| Del / Backspace | Clear selected step |
| Right-click | Clear step |

---

## Input Modes

Three direct-entry modes accelerate entering effects, velocities, and instruments without touching sliders.  
The active mode is shown in the column header: `[FX]`, `[VOL]`, or `[INS]`.

---

### FX Direct Entry — `` ` `` or F2

Toggle: `` ` `` (backtick)  
Header badge: `[FX]`

Type **3 hex characters**: 1 command nibble + 2 value digits.  
The row advances automatically after the 3rd digit.

Examples:

| Input | Effect written |
|-------|---------------|
| `F80` | cmd=`0F`, val=`80` → 128 BPM |
| `C64` | cmd=`0C`, val=`64` → set volume 100 |
| `410` | cmd=`04`, val=`10` → vibrato speed=1, depth=0 |

| Key | Action |
|-----|--------|
| Enter | Commit partial buffer (zero-pads remaining digits) |
| Backspace | Remove last typed digit; if buffer empty, clears effect |
| Esc | Exit FX mode |

---

### Volume Direct Entry — `'` or F3

Toggle: `'` (single quote)  
Header badge: `[VOL]`

Type **2 hex characters**: velocity byte (clamped to 1–127).

- On note rows: sets note velocity.
- On empty rows: writes a `Cxx` volume effect on that channel (applies to the sustaining note).

Auto-advances by step after the 2nd digit.

| Key | Action |
|-----|--------|
| Enter | Commit partial buffer (zero-pads remaining digit) |
| Backspace | Remove last typed digit |
| Esc | Exit volume mode |

---

### Instrument Direct Entry — `;` or F4

Toggle: `;` or F4  
Header badge: `[INS]`

Type **2 hex characters**: instrument `00`–`FF`. The note then plays that instrument.  
The selected cell shows an amber overlay while typing.  
Auto-advances by step after the 2nd digit.

| Key | Action |
|-----|--------|
| Enter | Commit partial buffer (zero-pads to 2 digits) |
| Backspace | Remove last typed digit |
| Esc | Exit instrument mode |

---

## Block Editing

| Key | Action |
|-----|--------|
| Shift+Arrows | Extend selection |
| Click+Drag | Mark block |
| Ctrl+C | Copy block |
| Ctrl+X | Cut block |
| Ctrl+V | Paste block (overwrite) |
| Ctrl+Shift+V | Mix-paste: only fill empty cells |
| Ctrl+Up / Down | Transpose ±1 semitone |
| Ctrl+Shift+Up / Down | Transpose ±1 octave |
| Alt+Up / Down | Selected step velocity ±1 |
| Alt+Left / Right | Selected step gate ±1 |
| Alt+Shift+Arrows | Larger velocity / gate nudges |

Panel buttons (right panel) provide the same Copy / Cut / Paste / Transpose±.  
**Apply FX to Block** writes the current FX Cmd/Val to every step in the selection.

### Paste Modes and Columns

**Paste...** (next to Paste) opens the paste-special menu:

| Mode | What it does |
|------|--------------|
| Overwrite | Block replaces the target; empty clipboard cells clear it (Ctrl+V) |
| Merge | Non-empty clipboard cells replace the target; empty ones leave it alone |
| Mix | Only fills target cells that are empty (Ctrl+Shift+V) |
| Insert | Pushes the rows below down by the block height in the pasted channels, then pastes |
| Flood | Overwrite, repeated down to the end of the pattern |

The same menu has **Note / Instrument / Volume / Effect** column toggles for the next paste
(from the menu or Ctrl+V); they reset to all columns after every paste. For example, untick
everything except Effect, then paste to copy only an effect lane. Instrument and Volume only
land on cells that end up with a note.

The CLI equivalent is
`pattern paste <row> [chOffset] [step N] [mode overwrite|merge|mix|insert] [flood] [cols nivf]`,
where `cols` takes any of `n`ote, `i`nstrument, `v`olume and `f`x. The CLI default mode is
`merge`; `flood` cannot be combined with `insert`.

### Clipboard History

The last 9 copies (and cuts) are kept, newest first. The drop-down under the Copy / Cut /
Paste row lists them with a short preview, for example `4x2  rows 0-3 ch 1-2  3 notes 1 fx C-4 D#4 ...`.
Pick one to make it the block that Paste, Ctrl+V and Paste... use; **Clear** forgets them all.
Copying the same notes again moves that entry back to the top instead of adding a duplicate.
The history lasts until you quit and is not saved with the song.

CLI: `pattern clip` lists the history (`*` marks the one paste uses), `pattern clip use <n>`
selects slot `n`, and `pattern clip clear` empties it.

### Pattern Macros

Available in the right panel:

- **Fill Hats** — fills every other step with a hi-hat pattern
- **Accent 4th** — boosts velocity on every 4th step
- **Invert Velocities** — flips velocity values within the selection

---

## Step Editor (right panel)

The Step Editor shows detailed controls for the selected cell.

| Control | Description |
|---------|-------------|
| Velocity | Note velocity 1–127 (slider or MIDI Learn) |
| Gate | Note length in ticks; 0 = sustain until next note-off |
| FX Cmd | Effect command (slider or hex box) |
| FX Val | Effect value (slider or hex box) |

### Pattern Search

Shortcut: Ctrl/Cmd+F4 (next), Ctrl/Cmd+Shift+F4 (previous)

Search field modes: **Note**, **Instrument**, or **FX Value**.  
Set the value in the search box and use next/prev to step through matches.

### A/B Compare

Capture the current pattern state as snapshot A, then toggle between A and the live edits to compare versions.

### MIDI Learn

Map external MIDI CC messages directly to Velocity, Gate, FX Cmd, or FX Val:

1. Click the **Learn** button next to the target parameter.
2. Move a CC on your MIDI controller.
3. The mapping is live immediately.

Click **Clear** to remove all MIDI CC mappings.

### Song / Module Message

Ctrl+M — focuses the Song/Module Message editor in the panel.

---

## Undo History

The right panel shows a visual timeline of pattern snapshots.  
Click any snapshot in the history list to restore that state.

---

## Transport

| Control | Description |
|---------|-------------|
| Play / Stop | Toolbar buttons or **Space** (grid focused) |
| Loop | Toggle pattern looping |
| Tempo slider | BPM (40–240) |
| Swing slider | Per-pattern groove offset (50–75%) |
| Ticks/Beat | Rows per beat (controls rhythmic resolution) |
| Ticks/Row | Transport tick length per row |

### Play Modes

- **Pattern** — loop or single-play the current pattern
- **Song** — play patterns in song-order sequence

---

## Song Order

Patterns are arranged in a song order list (right panel, Song Order section).

| Control | Action |
|---------|--------|
| Entry selector | Pick which song position to edit |
| Pattern selector | Choose which pattern plays at that position |
| Add / Remove | Insert or delete a song position |
| Up / Down | Reorder positions |

### Song Arranger

| Control | Action |
|---------|--------|
| Bars slider | Number of bars to operate on |
| Duplicate Section | Copy the selected section and append it |
| Insert Bars | Insert empty bars at the current position |
| Ripple Left / Right | Shift all entries left or right in the song order |

---

## Instruments and Samples

**A note plays its instrument and nothing else.** There are 256 instrument slots (`I00`–`IFF`);
each holds either a plugin (sine, square, SF2, SFZ, LV2, VST3, ...) or a *sample instrument* that
plays a sample from the sample bank. Pattern cells show the note's 2-digit instrument number.

- New notes get the **selected instrument**: the **Ins** box on the toolbar, which is the same
  selection as the slot list on the **Instruments** tab.
- Loading a WAV on the **Samples** tab also creates its sample instrument (the selected
  instrument slot if it is empty, otherwise the first free one). **Make Instrument** does the
  same for a sample that has none; the tab shows which instrument plays the selected sample.
- Several instruments can play one sample; edits on the Samples tab (trim, normalize, loop, ...)
  reach all of them. Unloading a sample silences its instruments until a sample is loaded into
  that slot again; they keep their own settings (gain, root, loop). The same happens when a
  song's sample file is missing: load the WAV into that sample slot and the instrument is back.
- Assigning a plugin or instrument file to a slot that is in use asks before replacing it.
- Songs from older versions, where a note could pick a sample through its sample column or by
  sharing its number with a sample slot, are converted when loaded; the status line says so.

The side panel has four tabs: **Song** (song order, arranger, message, channels, channel filter,
reverb), **Instruments**, **Samples** and **Edit** (step editor, search, clipboard, MIDI learn,
macros, undo, keyboard settings).

### Set Instrument

**Set Instrument** (Edit tab) gives every note in the marked block the selected instrument, or
every note in the selected channel when no block is marked. Channels have no instrument of their
own; older songs' per-channel instruments are ignored when loaded.

### Channel Mute, Solo and Names

Each channel row has **Mute** and **Solo** toggles. While any channel is soloed, only soloed
channels play (solo wins over mute); clearing every solo restores the mute state.
Double-click a channel label to rename it; clear the text to go back to `CH <n>`.
The pattern header shows `S` for soloed, `⊘` for muted, grey for channels silenced by another
channel's solo, and the name after the channel number.

The CLI has the same controls:

| Command | Action |
|---------|--------|
| `ch` / `ch list` | Show name, volume, mute and solo for every channel |
| `ch vol [<ch>] [<0-200>]` | Show or set channel volume (100 = unity) |
| `ch mute <ch> [on\|off]` | Toggle or set mute |
| `ch solo <ch> [on\|off]` | Toggle or set solo |
| `ch solo off` | Clear solo on all channels |
| `ch name <ch> [<name>\|-]` | Show, set or clear (`-`) a channel name |
| `ch insert <ch>` | Insert an empty channel at `<ch>`, shifting the rest right |
| `ch delete <ch>` | Delete `<ch>`, shifting the rest left |
| `ch move <from> <to>` | Move a channel to another position |
| `ch dup <ch>` | Duplicate `<ch>` into the next channel |
| `ch undo` / `ch redo` | Undo / redo the last insert, delete, move or duplicate |

Instrument, mute, volume and names are saved with the song; solo is not.

### Channel Filter

The **Filter** section of the right panel (type, cutoff, resonance) sets a filter on each ticked
channel. It is kept per channel: it survives play/stop and pattern edits, and it moves with its
channel on insert, delete, move and duplicate (channel undo restores it too). Pattern effects
`18`/`19`/`1A` can override it during playback; changing the panel again takes it back. The
filter is saved with the song.

### Channel Insert, Delete, Move and Duplicate

Right-click a channel number in the pattern header for **Mute**, **Solo**, **Insert empty
channel here**, **Duplicate channel**, **Delete channel**, **Move left / right**, and
**Undo / Redo channel edit**. The CLI has the same edits (see the table above).

- Edits apply to **every pattern** and carry the channel's name, instrument, mute, solo and
  volume along with its notes.
- The channel count never changes: insert and duplicate drop the last channel, so they refuse
  while it holds notes or effects in any pattern (clear it or add a channel first); delete adds
  an empty channel at the end.
- Channel undo keeps 16 steps. It refuses once any pattern has been edited since, so it can
  never throw away later note edits. After a channel edit, `pattern undo` and the Undo
  History panel start fresh, because their older snapshots use the previous channel order.

### Sample Bank

The sample bank (Samples tab) holds sample slots S000–S100. The slot list shows which instrument
plays each sample, e.g. `S003 - kick (I03)`.

| Control | Action |
|---------|--------|
| Sample selector | Choose the sample slot |
| Load WAV | Load a WAV into the slot and create its sample instrument |
| Play / Stop | Preview the sample itself |
| Make Instrument | Create an instrument that plays this sample (if none does) |
| Play from MIDI Keys | Play the sample's instrument from the MIDI keyboard |
| Rename | Set a display name for this slot |
| Clear | Remove the sample; its instruments fall silent until the slot is loaded again |

### Waveform Editor

- Drag the **green handles** to set trim start/end range.
- **Apply Trim** crops to the selection (source retained in memory).
- **Normalize** — peak-normalizes the selected region.
- **Fade In / Fade Out** — apply linear fade to the selected region.
- **Reload Source** — restores the original loaded sample from memory.
- **Ctrl/Cmd+K** — crop shortcut while the waveform has focus.

---

## Effects Reference

Effects are written in the Effect column as a 3-hex sequence: command nibble + 2-digit value.  
Effect memory: if the value byte is `00`, the sequencer reuses the last non-zero value for that command on that channel (standard tracker behavior). Commands `5xx` and `6xx` share memory with `Axx`.

| Command | Name | Behavior |
|---------|------|----------|
| `0xx` | Arpeggio | Hi nibble = +x semitones up, lo nibble = +y semitones up. Cycles through base → base+x → base+y per tick. |
| `1xx` | Slide Up | Pitch slides up xx units per tick. |
| `2xx` | Slide Down | Pitch slides down xx units per tick. |
| `3xx` | Tone Portamento | Glide pitch toward the next note at speed xx. |
| `4xx` | Vibrato | Hi nibble = speed, lo nibble = depth. LFO on pitch. |
| `5xx` | Portamento + Vol Slide | Tone portamento active, with simultaneous volume slide (hi nibble = vol up, lo = vol down). |
| `6xx` | Vibrato + Vol Slide | Vibrato active (reuses `4xx` params), with simultaneous volume slide. |
| `9xx` | Retrigger | Retrigger the note every xx ticks within the row. |
| `Axx` | Volume Slide | Hi nibble = volume up per tick, lo nibble = volume down per tick. |
| `Bxx` | Jump | Jump to pattern row xx immediately. |
| `Cxx` | Set Volume | Set note velocity to xx (1–127). On empty rows: applies to the sustaining note on that channel without retriggering. |
| `Dxx` | Pattern Break | End current pattern early and jump to row xx of the next pattern. |
| `E1x` | Fine Slide Up | Fine pitch slide up by x units (1 unit per tick). |
| `E2x` | Fine Slide Down | Fine pitch slide down by x units. |
| `E9x` | Retrigger (sub) | Retrigger every x ticks (E-command sub-mode). |
| `ECx` | Note Cut | Cut (silence) the note at tick x within the row. |
| `EDx` | Note Delay | Delay note trigger by x ticks within the row. |
| `Fxx` | Set Tempo | If xx < 32: sets Ticks/Row (TPR). If xx ≥ 32: sets BPM directly. Example: `F80` = 128 BPM. |

---

## Save / Load Format

Song files are plain text with the extension `.xts` (or `.xtp` for pattern-only exports).

### File Header

```
EXTRACKER_SONG_V1 <rows> <channels> <patternCount> <songLength> <currentPattern> <songOrderPosition>
```

### Pattern Data

Each pattern is saved as a block:

```
PATTERN <index>
<row> <channel> <hasNote> <note> <instrument> <sample> <gateTicks> <velocity> <retrigger> <effectCmd> <effectVal>
... (one line per cell)
```

### Tail Tokens

After all pattern data, the following tokens appear:

| Token | Format | Description |
|-------|--------|-------------|
| `SONG_ORDER` | space-separated pattern indices | Song playback sequence |
| `PATTERN_SWING` | space-separated integers (50–75) | Swing value per pattern |
| `TRANSPORT` | `<bpm> <tpb> <tpr>` | BPM, ticks-per-beat, ticks-per-row |
| `MIDI_MAP` | 16 integers | MIDI channel → internal channel map |
| `MIDI_EDITOR_CC_MAP` | integers | Step editor MIDI CC assignments |
| `SAMPLE_ENTRY` | `<slot> <name> <path>` | One line per loaded sample slot |
| `CHANNEL_INSTRUMENTS` | one slot per channel | Older files only; ignored when loading |
| `CHANNEL_MUTED` | one `0`/`1` per channel | Channel mute state |
| `CHANNEL_VOLUME` | one integer (0–200) per channel | Channel volume in percent |
| `CHANNEL_NAMES` | one quoted string per channel | Channel names (`""` = default) |
| `REVERB` | `<room> <damping> <wet> <width>`, each 0–255 | Global reverb (Reverb panel / `reverb set`) |
| `CHANNEL_FILTERS` | `<type> <cutoff> <resonance>` per channel | Channel filter: type 0=off 1=LP 2=HP 3=BP 4=notch, cutoff and resonance 0–255 |

### Startup Templates

On first run (or by preference) a template can be applied automatically:

- **Blank** — empty grid
- **House** — basic four-on-the-floor kick/hat pattern
- **Electro** — electro-style synth pattern

The template preference is stored in the user preferences file and applied before the first pattern is shown.
