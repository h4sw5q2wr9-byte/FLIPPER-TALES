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

A resolved action plays out in two parts: `FT_ANIM_MS` (900 ms) of sprite
animation, and only then does the result popup take the arena. The animation is
staged — wind-up, emit, travel, strike, recover — and themed to the module
rather than being one generic lunge:

- **Broadcast** charges the antenna, then sends a train of three chevrons
  across the arena, washing over every foe in turn.
- **Contact** closes the gap to the target and crackles a field at the point of
  contact.
- **Enemy attacks** use the same vocabulary in reverse, chosen by their own
  delivery, so a ranged foe reads differently from one that comes at you.

A chevron's apex points where the wave is going and its arms trail behind; the
first version had this inverted, so waves appeared to fly backwards. Without the split the popup covers the sprites for the whole
hold and the animation is never seen.

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

### 4.7a The action set

Five actions. The first pass had four, of which two were dead weight — Focus
charged a meter nothing could spend, and Defend only slowed the Charge roll
without reducing damage. Both are now real choices:

| Action | Effect |
|---|---|
| **SUB** (Sub-GHz) | Broadcast: strikes every living foe, low power each |
| **NFC** | Contact: one target, high power, halves its shield |
| **DEF** | Brace: shield 2 for the turn, +1 RAM, slower Charge roll |
| **FOC** | Charge the Signal meter |
| **SIG** | Spend one bar to replay a captured attack at 75% power |

`SIG` is what gives the Signal meter a sink, and so gives Focus and capture a
point. A replayed broadcast still hits everything; a replayed contact attack
still needs a target.

Unusable actions are shown struck through, never hidden, and the row beneath
the menu says **why** — naming the remedy ("Flying: use SUB."), not just the
problem. That row is also what stops five three-letter buttons from being
unreadable.

### 4.8 Enemy attributes — Milestone 1 subset

| Attribute | Effect | Block Tales equivalent |
|---|---|---|
| `SHIELDED(n)` | Flat reduction applied **per hit** — punishes multi-hit hardest | Defense |
| `AIRBORNE` | **NFC** (contact) cannot reach it; broadcast/projectile can | Flying |
| `ENCRYPTED` | **Sub-GHz** deals nothing and refunds 1 RAM (a Pass) | Spiky |
| `FAST` | Acts **before** the player | Mobile |

Later: `JAMMER` (locks the Signal meter), `SHIELD`, deflection attributes.

### 4.9 Multiple foes

Up to `FT_MAX_ENEMIES` (3) foes, which is what fits across a 128px arena at
16px each. They are laid out grouped on the right — a spread-out row reads as
three separate fights rather than one crowd.

Each foe acts in turn: the player moves, then every living foe telegraphs and
strikes in order before the menu returns. Targeting is UP/DOWN, and the target
automatically falls through to a living foe when the chosen one dies.

This is what makes the broadcast-versus-contact tradeoff real. Against one foe,
contact is simply better; against three, weaker-but-wider wins. The mixed
group — airborne plus encrypted — is the first fight where neither module can
cover the board alone.

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

### 5.0 Teaching the game

Two layers, because the timing windows are the whole game and nothing about
them is self-evident.

**A three-page help deck** on launch (the fight, your strike, their turn),
navigated with LEFT/RIGHT and dismissed with OK. Reachable again with UP from
any outcome screen.

**A contextual coach** during play: one line, keyed to the exact situation —
which module is locked and why, whether to wait or tap, what a jam was missing
to become a capture. It is derived from battle state in `ft_tutorial.c` rather
than tracked as its own step counter, so there is nothing to fall out of sync
with the fight and every line is testable by constructing an encounter and
asking what it would say. DOWN silences it outside the menu grid; the choice
carries across fights.

A test walks every reachable combination of enemy, phase, menu index, attack
and ready state, and fails if any line the coach can produce exceeds the
20-character width budget.

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

### M2 — Overworld *(partly built)*

Built: tiles, collision, camera, renderer, and a four-room prologue chain.
`make -C test map` renders every room at panel resolution and whole.
Still to build: entities, room transitions, the strike, terminals, the pause
menu, and saving.

#### Tiles and the viewport

8px tiles give a **16×8 viewport**: coarse enough to read at one bit, fine
enough that a room is more than a few paces across. Maps are one byte per tile,
row-major — the format production maps will stream from the SD card, so nothing
about the renderer changes when they do. Maps, tiles and sprites are all
authored as editable ASCII under `tools/`.

Ten tiles: floor, wall, void, grass, cable, door, terminal, locked port, crate,
ladder.

**Tiles that belong to a run orient themselves to it.** A door in a horizontal
wall is walked through vertically and reads face-on; the same door in a vertical
wall is walked through sideways and must read as a gap. Rather than make map
authors pick the right variant, `ft_map_art_index` derives it from the
neighbours: solid above and below is a side-on passage, solid left and right is
front-facing, ambiguous falls back to front-facing. Locked ports follow doors;
conduit follows its own run.

**Walls use the same mechanism for depth.** A wall with floor below is showing
its south-facing side and draws as brick with a solid base; a wall with more
wall below is seen from above and draws as a near-solid cap. The contrast is
what gives a run apparent height. A dithered band then falls on whatever sits
under a wall — 50% dither, because a solid bar is indistinguishable from more
wall at one bit.

Open floor grows **procedural greenery**: `ft_map_scatter` hashes the tile
coordinate against a per-map density, so weeds are deterministic (nothing
shimmers as the camera scrolls) and stay out of the map data.

#### Movement

The avatar is **the same handheld device as the battle sprite**, shrunk to
8×12 — it is the player character, so it must be recognisably the thing you are
in combat. Facing lives in the screen (shifted pupils), since at 8px wide a
turned body is unreadable. It is drawn with a one-pixel white halo, without
which it carries a crate's visual weight and vanishes into the floor stipple.

Only the lower rows collide, the standard top-down trick that lets a head pass
in front of scenery. Movement resolves each axis separately, so a diagonal into
a wall slides along it. The camera centres the player and clamps to the map.

#### Shape of an area

**An area is a numbered chain of rooms, walked left to right** — not an open
map, and not rooms with backtracking. This is the thing the first sketch got
wrong: it drew 2.5-screen open fields when the reference builds areas out of
screen-sized set-pieces (RESEARCH.md, "Area structure").

A room is therefore **about one screen**, sometimes a little over for a touch of
scroll, and holds a small fixed amount:

- at most **one overworld foe**, which on contact fights a *predefined group* —
  the sprite you can see, plus friends you cannot
- at most **one obstacle**, with exactly one answer
- usually one item, sometimes one hidden thing in a corner
- occasionally a **side room** off the path, holding an item and nothing else

A room is an authored set-piece, not a space to explore. The interest is the
encounter, the obstacle and the hidden thing — not the floorplan. It is also
why rooms are cheap: a dozen small maps beat two big ones.

An area ends with a **mini-boss standing in the exit**; beating it opens the way
on, which is how a chapter paces itself without a quest log. Towns are the same
chain with foes swapped for a shop, an inn and NPCs.

#### Controls

| Button | Overworld |
|---|---|
| D-pad | Walk |
| OK | Interact — talk, open, read, and **strike** |
| Back | Pause menu: loadout, journal, save, quit |

No dash. Block Tales needs one because its areas are large and 3D; ours are a
screen at a time, and a dash would mostly clip through the collision footprint.

#### Encounters

Foes are **visible and placed**, never random — an encounter is a decision, not
a tax on walking. Each drifts near a home tile and moves toward you inside a
short alert radius. Who makes contact decides the opening:

| Opening | How | Effect |
|---|---|---|
| **First Strike** | Press OK facing an adjacent foe | That foe starts damaged and you act first |
| **Neutral** | Walk into it | Normal start |
| **Jumped** | It reaches you while you face away | It acts first |

`First Strike` is a real status in the reference, so this is faithful rather
than invented. The harsher version — losing your guard when jumped — is
deliberately **not** taken: with guarding being the entire defensive game and
Hard Mode doubling damage, that punishes one mistake twice.

Defeated foes stay down for the visit and return when the area is re-entered.

#### Obstacles

One per room at most, and **each has exactly one answer**, so an obstacle is a
recognition test rather than a puzzle:

| Obstacle | Answer | From |
|---|---|---|
| Sealed hatch | Infrared, along a clear line | Ch. 1 |
| Shuttered vent | RFID reads what is behind it | Ch. 2 |
| **Locked port** | iButton | Ch. 3 |
| Dead lift or bridge | GPIO powers it | Ch. 4 |
| Inert drone in the way | BLE pairs and moves it | Ch. 5 |
| Gap between terraces | A ladder, already there | — |

Locked ports are seeded from the prologue onward, so early areas hold things
that cannot be taken yet. That is the whole backtracking design: light,
optional, and visible the first time through.

#### Interactions

- **Door** — transition to a linked room and position.
- **Terminal** — full restore and save. The only save point, so its placement
  is the pacing.
- **Ladder** — walk through a terrace edge. Top-down has no elevation, so a
  terrace is drawn as wall and the ladder is the gap in it.

#### Entities

Up to a dozen per room, in a fixed array — no allocation:

```c
typedef struct {
    FtEntKind kind;   /* foe, NPC, item, receiver */
    FtPos     pos;
    uint8_t   data;   /* roster id, dialogue id, contents */
    uint8_t   home_tx, home_ty;
    uint8_t   flags;  /* defeated, taken, triggered */
} FtEntity;
```

A foe's `data` indexes a **roster** — the group it fights as — which is what
lets one visible sprite mean "and two friends", as the reference does. This is
what the multi-foe battle work feeds.

Persistent flags (an item taken, a port unlocked, a boss beaten) live in the
save as a bitfield keyed by room and entity index, so the world remembers what
you did without storing the world.

#### The world

Five chapters after the prologue, each recovering one module. **Each module is
both a combat tool and a traversal verb**, which is what makes gating an area
behind it honest rather than arbitrary.

```
[Cold Boot]      prologue — wake up wiped, start with SUB + NFC
     |
[The Scrapline] --> Infrared : trigger receivers across gaps
     |
[Cold Storage]  --> RFID     : read through walls, reveal hidden doors
     |
[The Turnstile] --> iButton  : open the locked ports
     |
[Signal Hill]   --> GPIO     : power dead lifts and bridges
     |
[The Deadzone]  --> BLE      : pair with devices and move them
```

| Area | Module | Combat role |
|---|---|---|
| The Scrapline | **Infrared** | Line-of-sight: huge damage, front foe only |
| Cold Storage | **RFID** | Penetrates, ignores `SHIELDED` |
| The Turnstile | **iButton** | Strips enemy buffs |
| Signal Hill | **GPIO** | Support, buffs, RAM regen |
| The Deadzone | **BLE** | Control — and where `JAMMER` foes live |

#### Progression and saving

Battles already grant XP and level-ups choosing Charge, RAM or Flash. The
overworld adds the **Flash economy**: modules found in the world are installed
from the pause menu against a budget, so a new module is a decision rather than
a strict upgrade.

One save file per slot, written at terminals: stats, loadout, captured signals,
current room and position, and the entity flag bitfield. Three slots.

#### The prologue chain

| Room | Size | Holds |
|---|---|---|
| [1] Cold Boot | 16×8 | A terminal, to teach saving. No foe, no obstacle. |
| [2] Boot Corridor | 20×8 | First encounter, seen before it is reached. |
| [3] The Drop | 18×14 | A terrace split by a ladder; the shelf item is passed before it can be taken. |
| [4] Cold Gate | 18×10 | A sealed side room behind a locked port — the reason to come back. |


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
