# Flipper Tales

A turn-based RPG for the **Flipper Zero** — Paper Mario-style timed action commands and defensive
parries, on a 128×64 1-bit screen with six buttons.

You play a wiped device recovering its modules. Your combat abilities *are* the modules: **Sub-GHz,
NFC, RFID, Infrared, iButton, BadUSB, GPIO, U2F, BLE**.

> **The modules are in-game items with in-game effects.** This app does not use the Flipper's real
> radios, NFC, IR or USB hardware, and never transmits anything. It reads and writes nothing but its
> own save file.

> **Status: the loop runs.** Walk a four-room prologue, meet foes, fight them,
> and come back out. Not yet built: foe movement, items, the pause menu and
> saving. See [`docs/DESIGN.md`](docs/DESIGN.md).

## What it is

Three stats, each meaning something: **Charge** (survival), **RAM** (running modules), **Flash**
(how many modules you can install). One choice per level, and modules stack — every build is a
visible sacrifice.

Timing is the whole defensive game. Every incoming attack can be **jammed** (halved, payload
nullified) or, frame-perfect, **captured** — zero damage, *and the attack is written into your
Signal Library for you to replay*. A perfect parry doesn't just save you, it permanently expands
your moveset.

Damage doesn't apply instantly. Charge **rolls down** over real time, so a lethal hit still leaves
you a window to heal, finish the last enemy, or run and survive.

## Design reference

Heavily informed by [Block Tales](https://www.roblox.com/games/16483433878/) (Spaceman Moonbase) —
its mechanics are digested in [`docs/RESEARCH.md`](docs/RESEARCH.md). Flipper Tales uses **entirely
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

Because no hardware APIs are in scope, core game logic lives in `src/core/` as pure C99 with **no
Flipper headers** — it compiles and is unit-tested on the host:

```sh
make -C test test    # unit tests (also enforces the no-floating-point rule)
make -C test sim     # headless balance simulation
```

## Layout

```
src/core/    pure C99 game logic — host-testable, no Flipper headers
src/app/     Furi / Canvas / input — rendering and glue only
assets/      app icon (1-bit PNG)
test/        host unit tests + battle simulator
docs/        design bible and research notes
```

## Controls

**Overworld**

| Button | Action |
|---|---|
| D-pad | walk |
| OK | strike a foe you are facing, use a terminal, or take a door |
| Back | quit |

**Battle**

| Button | Action |
|---|---|
| ← / → | choose an action |
| ↑ / ↓ | choose a target |
| OK | confirm; then time the action command, and time your guard |
| Back | quit |

Tap OK in the last **150 ms** before an attack lands to **jam** it (half damage,
payload nullified). Tap in the last **50 ms** to **capture** it — zero damage,
and the attack is stored in your Signal Library.
