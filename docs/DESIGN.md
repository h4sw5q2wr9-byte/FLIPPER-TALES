# Pixel Tales — Design Bible

A turn-based RPG for the **Flipper Zero**, in the lineage of Paper Mario 64 / EarthBound, with
[Block Tales](RESEARCH.md) as the immediate mechanical reference.

Status: **design locked for Milestone 1**. Nothing here is implemented yet.

---

## 1. Pillars

1. **Timing is the whole defence.** You are never safe because of a stat. You are safe because you
   pressed OK at the right moment. One button carries the entire defensive game.
2. **Damage is a scene, not a number.** Rolling HP means a lethal hit gives you a window to act.
   Deaths are dramatic and occasionally survivable.
3. **Builds come from scarcity.** Three stats, one choice per level, stackable cards. Every build is
   a visible sacrifice.
4. **Everything is readable at 128×64 in 1-bit.** If it can't be understood in black and white on a
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
Momentum-built FAP does not run on OFW. Nothing this game needs exists outside the core Furi API, so
OFW is a strict superset of audience — and the official Apps Catalog
[requires](https://raw.githubusercontent.com/flipperdevices/flipper-application-catalog/main/documentation/Contributing.md)
compatibility with the latest Release/RC firmware.

### Hard rules falling out of the hardware

- **No floating point.** All maths is integer. NRG is stored in centi-units (100 = one bar).
- **Static tables live in flash** (`const`), never RAM. Battle state stays under ~2 KB.
- **~4 lines × ~20 characters** of text at a time. Writing must be EarthBound-terse. This shapes the
  script more than any other constraint.
- **Content streams from SD** (dialogue, maps, enemy tables) so it can grow without a rebuild.

## 3. Deviations from Block Tales

Deliberate, and each has a reason.

| Change | Why |
|---|---|
| **Original IP throughout** — no Roblox references, our own world and relics | Catalog requirement #2 is "no infringement on rights or trademarks". Block Tales itself is ineligible for the official catalog. |
| **Solo only.** No party scaling, no shared-meter party tax, and **no solo double-turn rule** | The double-turn exists only to compensate for having no party. With solo as the only mode we balance 1v1 honestly instead of bolting on a handicap. |
| **Integer NRG** (centi-units) | No FPU budget. |
| ~28 cards (from 85), ~24 items (from 158) | RAM, and a 128×64 menu shows ~5 rows. |
| **Ratings become a universal damage multiplier** | In Block Tales, ratings are per-move feedback labels. A single rating→multiplier table is far cheaper and more legible here. |
| **Attack class encoded by banner border, not colour** | We have no colour. See §5. |
| Cards that *exploit* being mid-roll (a "gambler" archetype) | Block Tales only uses rolling HP defensively. There's an untapped build space there. |

**Kept wholesale:** HP/SP/BP with one choice per level · stackable cards · the three-tier guard ·
rolling HP and mortal damage · the published priority table · enemy attributes as puzzle-locks ·
difficulty-as-an-equippable-card.

## 4. Combat specification

### 4.1 Stats

- Start: **10 HP**, 5 SP, 3 BP.
- Level costs a flat **100 XP**, carries the remainder, and fully restores HP/SP.
- One choice per level: `+5 HP` / `+5 SP` / `+3 BP`.
- Caps: HP 100, SP 100, BP 30. Level cap starts at 4, +4 per chapter.
- Underlevelled enemies award 0 XP; a single battle awards at most 100 XP.

### 4.2 Damage resolution (all integer)

```
raw        = base_power + atk_up - atk_down
raw        = raw * rating_pct / 100          // action command, see 4.3
effective  = raw - max(0, target_def - pierce)

if effective <= 0            -> DEFLECT     (0 damage, no status applied)

switch (guard_result):
  NONE       -> dmg = effective
  BLOCK      -> dmg = effective / 2          // floor; status nullified
  SUPERGUARD -> dmg = 0                      // counter if melee, dodge if ranged

final = max(0, dmg)
```

`Sword` pierces 50% of target DEF, rounded up.

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
| Block window | 150 ms | 75 ms |
| Superguard window | innermost 50 ms | innermost 25 ms |

### 4.5 Rolling HP

- HP ticks toward its target at **1 HP per 60 ms** (base).
- Interval modifiers: `+10%` per point of DEF · `×4` while Defending · `×0.5` with Hard Mode.
- **Pauses during the thinking phase** (menu open).
- Damage **≥125** applies instantly, bypassing the roll.
- **Mortal damage** (target 0): the player may still act while draining. Healing above 0 cancels it.
- Fleeing or winning mid-roll keeps whatever is currently displayed.

### 4.6 Turn priority

Deterministic, no hidden speed stat. Resolved in tiers:

1. Special (relic) slot
2. Healing actions
3. Status/buff actions
4. **Mobile** enemy actions
5. Debuff actions (DEF-down etc.)
6. Player attacks
7. Enemy actions
8. Focus
9. Revives

### 4.7 NRG — the relic meter

Integer centi-units, **100 = one bar**.

- Battle starts at **50**. Max bars = `1 + chapters_completed`.
- Gains: `+10` per player attack · `+10` per enemy turn · `+35` from Focus
  (`+5` per `Deep Focus` stack).
- Low-HP bonus: `+15` when attacking at ≤25% max HP, `+20` at exactly 1 HP.
- No party tax (solo only).

### 4.8 Enemy attributes — Milestone 1 subset

| Attribute | Effect |
|---|---|
| `DEFENSE(n)` | Flat reduction applied **per hit** — punishes multi-hit hardest |
| `FLYING` | Melee cannot target it; projectiles can |
| `SPIKY` | Ball attacks deal nothing and refund 1 SP (a Pass) |
| `MOBILE` | Acts **before** the player |

Later: `DISABLES_NRG`, `SHIELD`, `MELEE_DEFLECTING`, `PROJECTILE_DEFLECTING`.

## 5. Reading the screen in 1-bit

The hardest port problem: Block Tales telegraphs attack class **with colour** (yellow GUARDED, red
UNDODGEABLE). We have none. Solution — encode it in the **attack-name banner border**:

| Class | Banner | Blockable | Superguardable |
|---|---|---|---|
| Normal | plain 1px box | ✅ | ✅ |
| **GUARDED** | hatched/dashed box | ✅ | ❌ |
| **UNDODGEABLE** | inverted (white-on-black) | ❌ | ❌ |

1-bit also makes **inversion a free, extremely legible channel** — the guard window telegraph is an
invert-on-frame pulse. Arguably clearer than colour.

### Screen layout (128×64)

```
┌────────────────────────────────────────────┐  y=0
│ ENEMY NAME              [DEF:2][FLY]       │  status strip, 12px
├────────────────────────────────────────────┤  y=12
│                                            │
│   @                      ,-.   ,-.         │  battle scene, 32px
│  /|\                    (   ) (   )        │  player left, enemies right
│                                            │
├────────────────────────────────────────────┤  y=44
│ HP 18/25  SP 7   NRG ▮▮▯                   │  player bars, 10px
│ > SWORD   BALL   CARDS   ITEM              │  action menu, 10px
└────────────────────────────────────────────┘  y=63
```

The attack-telegraph banner overlays the battle scene, centred.

## 6. Architecture

Two layers. This split is what makes the combat maths **verifiable on a host machine** instead of
shipping a blind binary.

```
src/core/     pure C99, ZERO Flipper headers — compiles with host gcc
              damage resolution · turn priority · rolling-HP tick · status stack
              card effects · seeded deterministic RNG
src/app/      Furi / Canvas / input / storage. Deliberately thin.
assets/       1-bit sprites (XBM), 10x10 app icon
test/         host-compiled unit tests + headless battle simulator
```

The headless simulator runs thousands of fights for balance tuning without touching hardware.

## 7. Milestones

### M1 — Combat vertical slice *(current)*

One battle screen. Three enemies — plain, **Flying**, **Spiky** — to prove the attribute locks.
Sword + Ball. ~8 cards. The full stack: action commands, block/superguard/undodgeable, rolling HP
with the mortal-damage window, priority resolution, ratings popups, Hard Mode card. Host test
harness and balance simulator. A `.fap` that builds.

Cards in M1:

| Card | Slot | BP | Effect |
|---|---|---|---|
| Sword | Sword | 0 | Base melee; pierces 50% DEF |
| Power Stab | Sword | 2 | +damage, stackable |
| Ball | Ball | 0 | Base projectile; hits Flying |
| Fireball | Ball | 2 | Projectile + Burn |
| HP+ | Passive | 3 | +5 max HP, stackable |
| Safe Guard | Passive | 1 | Blocking reduces more |
| Deep Focus | Passive | 1 | +5 NRG on Focus, stackable |
| Hard Mode | Passive | 0 | +50% XP; 2× damage, halved guard window, 2× roll speed |

Strategies (Defend / Focus / Pass / Run) are built in at 0 BP.

### M2 — Overworld
Top-down tile grid, 128×64 viewport, visible enemies you walk into, dash on OK, SD-card maps,
save/load.

### M3 — Chapter 1
Town, NPCs, a shop, the card economy, a boss, the first relic (+1 NRG bar), level cap 8.

### M4 — Catalog submission
Open-source licence, 10×10 icon, qFlipper screenshots, README, changelog,
`manifest.yml` PR to `flipperdevices/flipper-application-catalog`.

## 8. Open questions

- Setting and tone. The mechanical frame ("one relic per chapter, each adding a meter bar") is
  settled; the fiction is not.
- Are relics **weapons** (like the source's swords) or a broader category — instruments, masks,
  tools? A non-weapon relic set would distance us further and open up non-damage ultimates.
- Save slots: one file, or three?
