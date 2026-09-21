# Flipper Tales — Design Bible

A turn-based RPG for the **Flipper Zero**, in the lineage of Paper Mario 64 / EarthBound, with
[Block Tales](RESEARCH.md) as the immediate mechanical reference.

You play a wiped device recovering its modules. Your combat abilities *are* the modules — Sub-GHz,
NFC, RFID, Infrared, iButton, BadUSB, GPIO, U2F, BLE.

> **These are in-game items with in-game effects.** The app does not use the Flipper's real radios,
> NFC, IR or USB hardware, and never transmits anything. See §2.1.

Status: **design locked for Milestone 1**. Nothing here is implemented yet.

---

## 1. Pillars

1. **Timing is the whole defence.** You are never safe because of a stat. You are safe because you
   pressed OK at the right moment. One button carries the entire defensive game.
2. **A perfect parry teaches you something.** Capturing a signal permanently expands your moveset.
   Skill converts into progression, not just survival.
3. **Damage is a scene, not a number.** Rolling Charge means a lethal hit gives you a window to act.
4. **Builds come from scarcity.** Three stats, one choice per level, stackable modules.
5. **Everything is readable at 128×64 in 1-bit.** If it can't be understood in black and white on a
   screen the size of a postage stamp, it doesn't ship.

## 2. Platform constraints

| | |
|---|---|
| Display | 128×64, **1-bit** (no colour, no greyscale) |
| Input | D-pad (4) + OK + Back — **6 buttons total** |
| CPU | STM32WB55, Cortex-M4 @ 64 MHz |
| RAM | ~256 KB SRAM; realistically **60–100 KB app heap**. The `.fap` is loaded into RAM. |
| Storage | SD card — saves and streamed content |
| Audio | Single-channel beeper |
| Toolchain | C, built with [`ufbt`](https://pypi.org/project/ufbt/) |

**Firmware target: Official (OFW), latest Release.** An OFW-built FAP runs on Momentum; a
Momentum-built FAP does not run on OFW. The game needs nothing outside Canvas, input, storage and
timers, so OFW is a strict superset of audience — and the official Apps Catalog
[requires](https://raw.githubusercontent.com/flipperdevices/flipper-application-catalog/main/documentation/Contributing.md)
compatibility with the latest Release/RC firmware.

### 2.1 The hardware is fiction

The module names are **theme only**. Flipper Tales:

- does **not** link against `furi_hal_subghz`, the NFC stack, `infrared_worker`, `lfrfid_worker`,
  USB HID, or GPIO;
- does **not** transmit, receive, read, emulate or replay any real signal;
- requests no radio permissions and performs no I/O beyond SD-card saves.

This is a deliberate scope decision with three payoffs: it sidesteps region-locked Sub-GHz TX rules
and any resemblance to an attack tool (catalog general requirement #4 forbids bypassing the device's
intentional limits), it keeps the RAM and flash budget small, and it leaves **100% of game logic
host-testable** (§6).

### 2.2 Hard rules falling out of the hardware

- **No floating point.** All maths is integer. The Signal meter is stored in centi-units
  (100 = one bar).
- **Static tables live in flash** (`const`), never RAM. Battle state stays under ~2 KB.
- **~4 lines × ~20 characters** of text at a time. Writing must be EarthBound-terse. This shapes the
  script more than any other constraint.
- **Content streams from SD** (dialogue, maps, enemy tables) so it can grow without a rebuild.

## 3. Deviations from Block Tales

Deliberate, and each has a reason.

| Change | Why |
|---|---|
| **Original IP throughout** — our own world, modules instead of licensed swords | Catalog requirement #2 is "no infringement on rights or trademarks". Block Tales itself, built on Roblox IP, is ineligible for the official catalog. |
| **Superguard captures the signal** (§4.5) | New. Block Tales' superguard only avoids damage; ours converts execution into permanent progression. |
| **Solo only.** No party scaling, no shared-meter party tax, and **no solo double-turn rule** | The double-turn exists only to compensate for having no party. With solo as the only mode we balance 1v1 honestly instead of bolting on a handicap. |
| **Integer Signal meter** (centi-units) | No FPU budget. |
| ~28 modules/cards (from 85), ~24 items (from 158) | RAM, and a 128×64 menu shows ~5 rows. |
| **Ratings become a universal damage multiplier** | In Block Tales, ratings are per-move feedback labels. A single rating→multiplier table is far cheaper and more legible here. |
| **Attack class encoded by banner border, not colour** | We have no colour. See §5. |
| Cards that *exploit* being mid-roll (a "gambler" archetype) | Block Tales only uses rolling HP defensively. There's untapped build space there — and it pairs with the low-Charge Signal bonus (§4.7). |

**Kept wholesale:** three stats with one choice per level · stackable cards · the three-tier guard ·
rolling HP and mortal damage · the published priority table · enemy attributes as puzzle-locks ·
difficulty-as-an-equippable-card.

## 4. Combat specification

### 4.1 Stats

The three stats are renamed to mean something:

| Stat | Name | Role |
|---|---|---|
| HP | **Charge** | Survival. Damage drains the battery. |
| SP | **RAM** | Running a module costs memory. |
| BP | **Flash** | How many modules you can keep installed. |

- Start: **10 Charge**, 5 RAM, 3 Flash.
- Levelling costs a flat **100 XP**, carries the remainder, and fully restores Charge/RAM.
- One choice per level: `+5 Charge` / `+5 RAM` / `+3 Flash`.
- Caps: Charge 100, RAM 100, Flash 30. Level cap starts at 4, +4 per chapter.
- Underlevelled enemies award 0 XP; a single battle awards at most 100 XP.

### 4.2 Damage resolution (all integer)

```
raw        = base_power + atk_up - atk_down
raw        = raw * rating_pct / 100          // action command, see 4.3
effective  = raw - max(0, target_def - pierce)

if effective <= 0            -> DEFLECT     (0 damage, no payload applied)

switch (guard_result):
  NONE     -> dmg = effective
  JAM      -> dmg = effective / 2            // floor; payload nullified
  CAPTURE  -> dmg = 0                        // + signal captured, see 4.5

final = max(0, dmg)
```

`NFC` pierces 50% of target `SHIELDED`, rounded up.

### 4.3 Action commands → ratings

A single table, applied as `raw * pct / 100`:

| Rating | Multiplier |
|---|---|
| MISS | 100% (0% if `Ante Up` equipped) |
| NICE | 110% |
| GOOD | 125% |
| GREAT | 150% |
| AMAZING | 175% |
| EXCELLENT | 200% |

### 4.4 Guard windows

Measured backwards from the impact frame. Tunable constants:

| | Normal | Hard Mode |
|---|---|---|
| Jam window | 150 ms | 75 ms |
| Capture window | innermost 50 ms | innermost 25 ms |

The app polls at 100 Hz (10 ms), which resolves the 50 ms capture window to
about five samples. These are the numbers most likely to need changing
once the game is played on hardware — they are constants in `ft_types.h` for
exactly that reason.

Both bars open with a **500 ms ready beat** (`FT_READY_MS`). The track and its
target zones are drawn, but the cursor is held at the start line and three pips
count the player in. Presses during the beat are ignored rather than penalised —
mashing is still self-punishing, because the first press after the release lands
at the very start of the sweep, nowhere near the target.

Action commands use the same mechanism, graded by distance from the perfect
moment: ≤30 ms EXCELLENT, ≤60 ms AMAZING, ≤100 ms GREAT, ≤160 ms GOOD, else a
miss. Early and late are punished identically.

### 4.5 Attack classes and Capture

Every enemy attack has a class, which determines what you can do about it:

| Class | Banner (§5) | Jam | Capture |
|---|---|---|---|
| Normal | plain 1px box | ✅ | ✅ |
| `GUARDED` | hatched box | ✅ | ❌ |
| `UNDODGEABLE` | inverted box | ❌ | ❌ |

**Capture** is the centrepiece mechanic. A frame-perfect guard against a Normal-class attack takes
zero damage *and* writes that attack into the player's **Signal Library**:

- The library is a **4-slot ring buffer**; capturing when full overwrites the oldest.
- Captured signals are replayed from the Special slot and cost **Signal meter**, not RAM.
- A replayed signal deals **75%** of the enemy's version — it's a copy, not the original.
- Captures persist across battles and are saved.

This makes execution compound into progression, and it gives `UNDODGEABLE` a second meaning beyond
"you take this": some attacks keep their secrets.

### 4.6 Rolling Charge

- Charge ticks toward its target at **1 per 60 ms** (base).
- Interval modifiers: `+10%` per point of `SHIELDED` · `×4` while Defending · `×0.5` with Hard Mode.
- **Pauses during the thinking phase** (menu open).
- Damage **≥125** applies instantly, bypassing the roll.
- **Brownout** (target 0): the player may still act while draining. Healing above 0 cancels it.
- Fleeing or winning mid-roll keeps whatever is currently displayed.

### 4.7 Signal — the module meter

Integer centi-units, **100 = one bar**.

- Battle starts at **50**. Max bars = `1 + modules_recovered`.
- Gains: `+10` per player attack · `+10` per enemy turn · `+35` from Focus
  (`+5` per `Deep Focus` stack).
- **Low-Charge bonus**: `+15` when attacking at ≤25% max Charge, `+20` at exactly 1.
- No party tax (solo only).

The low-Charge bonus is deliberately load-bearing: charging faster the closer you are to dying is the
solo risk/reward loop, and it pairs directly with being mid-roll (§4.6).

### 4.8 Enemy attributes — Milestone 1 subset

| Attribute | Effect | Block Tales equivalent |
|---|---|---|
| `SHIELDED(n)` | Flat reduction applied **per hit** — punishes multi-hit hardest | Defense |
| `AIRBORNE` | **NFC** (contact) cannot reach it; broadcast/projectile can | Flying |
| `ENCRYPTED` | **Sub-GHz** deals nothing and refunds 1 RAM (a Pass) | Spiky |
| `FAST` | Acts **before** the player | Mobile |

Later: `JAMMER` (locks the Signal meter), `SHIELD`, deflection attributes.

## 5. Reading the screen in 1-bit

The hardest port problem: Block Tales telegraphs attack class **with colour** (yellow GUARDED, red
UNDODGEABLE). We have none. Solution — encode it in the **attack-name banner border**:

| Class | Banner |
|---|---|
| Normal | plain 1px box |
| `GUARDED` | hatched/dashed box |
| `UNDODGEABLE` | inverted (white-on-black) |

1-bit also makes **inversion a free, extremely legible channel** — the guard window telegraph is an
invert-on-frame pulse. Arguably clearer than colour.

### Screen layout (128×64)

```
┌────────────────────────────────────────────┐  y=0
│ ROGUE BEACON          [SHLD:2][AIR]        │  status strip, 12px
├────────────────────────────────────────────┤  y=12
│                                            │
│   ▟                      ,-.   ,-.         │  battle scene, 32px
│  ▜█▛                    (   ) (   )        │  player left, enemies right
│                                            │
├────────────────────────────────────────────┤  y=44
│ CHG 18/25  RAM 7  SIG ▮▮▯                  │  player bars, 10px
│ > SUBGHZ  NFC   CARDS   ITEM               │  action menu, 10px
└────────────────────────────────────────────┘  y=63
```

The attack-telegraph banner overlays the battle scene, centred.

### 5.1 Verifying the layout

`make -C test preview` compiles the real renderer against a stub canvas
(`test/canvas_stub.c`), rasterises every battle phase to an image, and **fails
the build if a single pixel lands off-panel**. This exists because the first
pass at this screen was designed without ever seeing it, and shipped with text
running off both edges.

The stub's font advances are deliberately wider than the firmware's, so the
check errs toward reporting overflow the device would not actually show.

Character art lives in `tools/gensprites.py` as editable ASCII, which emits
`src/app/ft_sprites.h` and a preview PNG. Three rules keep a 16×16 sprite
legible at one bit: a solid 2px silhouette, a white interior, and features at
least 2×2 inside it — thin outline mixed with fill turns to mush at this size.
Each enemy's silhouette encodes its attribute class, so `AIRBORNE` and
`ENCRYPTED` are readable before the tags are.

**Width budget: 20 characters per line** at the standard font. Anything
data-driven (enemy names, attack titles) is measured with `canvas_string_width`
and truncated rather than trusted to fit.

## 6. Architecture

Two layers. Because §2.1 keeps all hardware out of scope, **the entire game is pure logic** and the
core compiles and runs on a host machine — combat maths is verified before anything is flashed.

```
src/core/     pure C99, ZERO Flipper headers — compiles with host gcc
              damage resolution · turn priority · rolling-Charge tick · status stack
              module effects · Signal Library · seeded deterministic RNG
src/app/      Furi / Canvas / input / storage. Deliberately thin.
assets/       1-bit sprites (XBM), 10x10 app icon
test/         host unit tests + headless battle simulator
```

The headless simulator runs thousands of fights for balance tuning without touching hardware.

## 7. Milestones

### M1 — Combat vertical slice *(in progress)*

Implemented: the whole of `src/core` (damage, rolling Charge, Signal meter and
Library, priority table, progression, the battle state machine and its timing
windows), plus `src/app` — the 128×64 battle screen, input handling and a
building `.fap` (13 KB, 6 KB text, 0 bss against a ~60–100 KB budget).

**Known gap:** the phase machine resolves the player before the enemy and does
not yet consult `ft_priority`, so the `FAST` attribute has no observable effect.
Ordering only matters once more than one enemy shares the board, which arrives
with M2.

Still to do: the module install/loadout screen, sound, and persistence.


One battle screen. Three enemies — plain, `AIRBORNE`, `ENCRYPTED` — to prove the attribute locks.
Sub-GHz + NFC. ~8 modules. The full stack: action commands, jam/capture/undodgeable, the Signal
Library, rolling Charge with the brownout window, priority resolution, ratings popups, Hard Mode
card. Host test harness and balance simulator. A `.fap` that builds.

Modules in M1:

| Module | Slot | Flash | Effect |
|---|---|---|---|
| Sub-GHz | Broadcast | 0 | Base ranged; hits all enemies, low per-hit; reaches `AIRBORNE` |
| Amplify | Broadcast | 2 | +damage, stackable |
| NFC | Contact | 0 | Base melee; high single-target; pierces 50% `SHIELDED` |
| Payload | Contact | 2 | Contact hit + damage-over-time (BadUSB flavour) |
| Charge+ | Passive | 3 | +5 max Charge, stackable |
| Faraday | Passive | 1 | Jamming reduces more |
| Deep Focus | Passive | 1 | +5 Signal on Focus, stackable |
| Hard Mode | Passive | 0 | +50% XP; 2× damage, halved guard window, 2× roll speed |

Strategies (Defend / Focus / Pass / Run) are built in at 0 Flash.

#### Balance notes

Figures from `make -C test sim` (2000 battles per cell), which plays whole
battles headlessly against a scripted policy. Two findings so far:

- **`Sub-GHz` is base power 3, not 2.** At 2, the `AIRBORNE` lock forced the
  player onto a module dealing 1 damage per hit through the Drift Beacon's
  shield — the enemy that teaches the lock was harder than the one two levels
  above it. A lock should redirect the player, not punish them. The Beacon also
  lost its shield for the same reason: `AIRBORNE` already constrains the tool
  choice, and shielding it taxed that same forced choice twice.
- **The skill gradient is real.** Base loadout against the Drift Beacon wins
  0% / 77% / 98% / 100% at guard skill 0 / 35 / 70 / 95%. Losing every battle
  at zero execution is the intended shape of pillar 1.

Open balance watch-items:

- `Amplify + Charge+` (5 Flash, reachable at level 2) currently wins 100% at
  zero guard skill against every M1 enemy. Flash investment may be allowed to
  substitute for execution too cleanly.
- The simulator fights every enemy from fresh level-1 stats and does not level
  the player between encounters, so later enemies read harder there than they
  will in play. Hard Mode's +50% XP upside is not modelled at all, only its
  costs — its rows are a floor, not a verdict.

### M2 — Overworld
Top-down tile grid, 128×64 viewport, visible enemies you walk into, dash on OK, SD-card maps,
save/load.

### M3 — Chapter 1
Town, NPCs, a shop, the module economy, a boss, the first module recovery (+1 Signal bar),
level cap 8.

### M4 — Catalog submission
Open-source licence, 10×10 icon, qFlipper screenshots, README, changelog,
`manifest.yml` PR to `flipperdevices/flipper-application-catalog`.

## 8. Open questions

- **Tone.** Two candidates: *warm* (a small device in a world of chatty appliances, EarthBound-ish)
  or *cold* (a wiped tool inside a decaying system, rogue processes as enemies). The mechanics don't
  care; the script does.
- **Title.** "Flipper Tales" is the working title. "Flipper" is Flipper Devices' trademark, and we
  are otherwise being deliberately catalog-safe — worth a decision before M4, not before M1.
- **Module order.** Which modules are recovered in which chapter determines the difficulty curve.
- Save slots: one file, or three? (Defaulting to three.)
