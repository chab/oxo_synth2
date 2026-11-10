# s1ref — Synth1 reference harness

Black-box host for the original `Synth1.component` (Daichi, AU v1.13 beta11,
x86_64 only → runs under Rosetta). Only public AudioToolbox API: published
parameters, the ClassInfo state chunk, MIDI in, audio out. No disassembly.

Build: `make -C tools/s1ref` → `build/s1ref/s1ref`.
Python helpers need the repo venv: `python3 -m venv .venv && .venv/bin/pip install numpy scipy`.

## Commands
- `s1ref params out.json` — published parameter info + quantization probe
  (→ `reference/meta/params.json`).
- `s1ref nativemap` — AU normalized step → native integer
  (→ `reference/meta/nativemap.txt`).
- `s1ref fromnative [ver=N] id=v ...` — write native values through the chunk,
  print what the AU reports.
- `s1ref classinfo`, `s1ref get` — raw state dumps.
- `s1ref batch jobs.txt|-` — render jobs (`S1REF_TIME=1`: each job's
  render speed on stderr, see AGENTS.md "VS THE ORIGINAL"):
  ```
  job /abs/out.wav
  sr 44100 | len 3.0 | tempo 120 | preroll 0.25
  n <id> <native>          # native value via the state chunk (preferred)
  p <id> <normalized>      # AU parameter
  w <word> <int32>         # raw chunk word (state outside the params)
  pt <s> <id> <normalized> # AU parameter at a time (block split there)
  note <on s> <off s> <key> <vel>
  cc <s> <num> <val>
  bend <s> <0..16383>
  end
  ```

## Facts (measured, do not rediscover)
- State chunk = ClassInfo `data`: "Synth1 VST Chunk Data", int32 LE words;
  word 140 = version (113); parameter k = pair `(1, native)` at words
  142 + 2k. Native values == `.sy1` values (ver 112/113).
- AU parameter k (normalized) ↔ native: step i of n published steps sits
  at (i + 0.5) / n, except a few enums with gaps (see nativemap.txt):
  play mode 38, chorus type 64, lfo type 42/47 (native 5 is the 3rd entry),
  lfo dest 41/46 start at 0.2.
- Word 140 = 100 / 105 / 106 / 107 / 108 makes the chunk loader apply the
  original's legacy patch conversion (same as its .sy1 loader, where a file
  without a ver line counts as 100): `measure_legacy.py` →
  reference/meta/legacy.json, `check_legacy.py`; arp beat (33) / delay time
  (35) are not converted (see AGENTS.md "LEGACY .sy1 CONVERSION").
- A fresh instance loads program 001 of `bankfolder0` (prefs
  `jp.daichi.synth1`): the way to probe the original's .sy1 loader
  (`measure_legacy_ranges.py` backs the prefs up and restores them).
- **Process-wide state**: a 2nd instance in the same process starts with a
  different oscillator phase. Only the first instance per process is
  reproducible, so `s1.render_batch` spawns one process per job (parallel).
  With that, renders are bit-exact repeatable.

## Corpus
`tools/s1ref/corpus.py [group…]` → `reference/renders/<group>/…wav` +
`manifest_<group>.jsonl` (exact job per file). Groups: presets, sweeps,
pitch, filter, velocity (~4.3k files, ~3.4 GB float WAV, ~1 min).

The presets group is anonymized (`patch0001`…, meta = version / legacy /
clamped only): `corpus.py presets` re-renders the jobs of its existing
manifest, it does not read patch files. Download / unpack: top README
"Reference data". `pack_reference.sh` builds that archive
(`dist/synth2-reference.zip`: the manifests, every file they list,
`reference/derived`).
