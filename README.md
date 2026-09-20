# Pixel Tales

A turn-based RPG for the **Flipper Zero** — Paper Mario-style timed action commands and defensive
parries, on a 128×64 1-bit screen with six buttons.

> **Status: design phase.** No code yet. See [`docs/DESIGN.md`](docs/DESIGN.md).

## What it is

Timing is the whole defensive game: every incoming attack can be **blocked** (halved, status
nullified) or **superguarded** (frame-perfect, zero damage, counters melee) with the OK button.
Damage doesn't apply instantly — HP **rolls down** over real time, so a lethal hit still leaves you a
window to heal, kill the last enemy, or run and survive.

Builds come from scarcity: three stats (**HP / SP / BP**), one choice per level, and stackable cards
that cost Build Points. Difficulty is itself an equippable card.

## Design reference

Heavily informed by [Block Tales](https://www.roblox.com/games/16483433878/) (Spaceman Moonbase) —
its mechanics are digested in [`docs/RESEARCH.md`](docs/RESEARCH.md). Pixel Tales uses **entirely
original setting, characters and assets**; it shares systems design, not content.

## Building

Targets **official Flipper firmware (OFW)**, latest Release, via [`ufbt`](https://pypi.org/project/ufbt/).
An OFW-built `.fap` also runs on Momentum and other forks.

```sh
pip install ufbt
ufbt update          # fetch SDK + ARM toolchain
ufbt                 # build the .fap
ufbt launch          # build, install and run on a connected Flipper
```

Core combat logic lives in `src/core/` as pure C99 with **no Flipper headers**, so it compiles and is
unit-tested on the host:

```sh
make -C test         # run host tests + headless balance simulator
```

## Layout

```
src/core/    pure C99 game logic — host-testable
src/app/     Furi / Canvas / input / storage
assets/      1-bit sprites (XBM), app icon
test/        host unit tests + battle simulator
docs/        design bible and research notes
```
