# Hexacorde for VCV Rack 2

An oracular step sequencer: I Ching hexagrams on a grid send notes to each other. Each line of a hexagram is a note; a played note sends a pulse toward the next hexagram, which echoes it in turn. Play the board with the mouse, patch the outputs into your voices.

Web version (play in the browser): https://luginf.github.io/hexacorde/ - main project: https://github.com/luginf/hexacorde

## Module

| | |
|---|---|
| **TEMPO** + **RUN** | internal clock in BPM (one step = an eighth note), used when CLOCK is not patched |
| **CLOCK** | one pulse = one step |
| **RESET** | clears the pulses in flight and resets the step counter |
| **PRIME** (poly) | a pulse on channel *i* primes hexagon *i* |
| **ROTATE** (poly) | a pulse on channel *i* turns hexagon *i* by one step |
| **PITCH** (poly) | 1 V/oct, C4 = 0 V; one channel per hexagon (up to 16) |
| **GATE** (poly) | 10 V for 90 % of a step on each note |
| **VEL** (poly) | 0 to 10 V (solid line 7.6 V, broken line 4.4 V) |
| **STEP** | a trigger on every step |

Board: click a hexagon to prime it; click a line to toggle solid / broken; drag a line to rotate; drag the body to move; mouse wheel rotates. **Right-click a hexagon** for its hexagram, scale (23), tonic, octave, rotation, priming every N steps, 7th side (heptagon), custom notes, reset, deactivate, delete. **Right-click the module** for the global tonality, settings, grid size, new hexagram, random draw / scatter, and import / export of setup `.json` files (the same files as the web version). The whole state is saved in the patch.

## Build

Download the Rack SDK 2.x, then `make RACK_DIR=/path/to/Rack-SDK` (C++17), `make install` or `make dist`.

## License

The module is licensed under GPL-3.0-or-later (see `LICENSE`). The engine in `core/` is a copy of the BSD 3-Clause licensed core of the main project (see `core/LICENSE`). This repository is generated from the main project: please send issues and changes there.
