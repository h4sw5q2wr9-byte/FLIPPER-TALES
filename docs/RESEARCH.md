# Research: How Block Tales Works

Mechanics digest of [Block Tales](https://www.roblox.com/games/16483433878/) (Spaceman Moonbase), the
design reference for Pixel Tales. Pulled from the Fandom wiki's raw wikitext via its MediaWiki API
(the rendered pages 403 to scripted fetches; the SEO mirror wikis contradict the real ones and were
discarded).

Accurate as of Demo 5. This file is a **reference**, not a spec — see `DESIGN.md` for what we build.

---

## Stats

Three stats only: **HP** / **SP** / **BP**. Start at 10 HP.

- Levelling costs a flat **100 XP**; leftover XP carries over.
- Each level grants exactly **one** choice: `+5 HP`, `+5 SP`, or `+3 BP`.
- Caps: HP 100, SP 100, BP 30 (BP is the only hard cap; cards can push HP/SP past 100).
- Level cap starts at **4**, rises by **4** per chapter, max 24 at Chapter 5.
- Level-up fully restores HP and SP.
- Underlevelled enemies award no XP at all; a single battle is capped at 100 XP (anti-farming).

## Cards

85 unique obtainable cards, 187 max copies. Costs **0–4 BP**.

Cards **stack**: equipping N copies multiplies both the effect and the SP cost by N. This is the key
improvement over Paper Mario badges — "one expensive card" and "many cheap cards" are both real builds.

Slots: **Sword**, **Ball**, **Other**, **Special** (story swords, spend NRG), **Items**, **Strategies**.

## Defence — three tiers

Pressed on impact, not a stat:

| Tier | Effect |
|---|---|
| **Block** | −50% damage (rounded down), nullifies any status the attack would apply |
| **Superguard** (frame-perfect) | **0 damage**; *counters* melee, *dodges* ranged |
| — | Some attacks are exempt (see below) |

Attack classes are colour-coded in the source game:

- normal — blockable + superguardable
- **GUARDED** (yellow) — blockable, *not* superguardable
- **UNDODGEABLE** (red) — neither

`Training Wheels` card shows the superguard timing audibly + visibly. `Gusto` disables superguarding.

## Rolling HP

The standout mechanic. Damage does **not** apply instantly — HP ticks down over real time, pausing
during the thinking phase.

- **Mortal Damage** = a hit that sets your HP *target* to 0. You can still act while it drains: heal,
  kill the last enemy, or flee and keep whatever HP is on screen.
- Roll speed: −10% per point of DEF, −75% while Defending, −50% with Halved Damage,
  **+100% with Hard Mode**.
- Damage **≥125** bypasses rolling entirely (125 = the max reachable HP: 100 cap + 5 stacked `HP+`).
- Rare RNG "**barely survived!**" sets you to 1 HP instead of 0.
- `Aggressor` card disables rolling (instant damage) as a tradeoff.

## Turn order — a published priority table

No hidden speed stat for players. 11 tiers, resolved in order:

1. Special slot (story swords) · 2. Sacrifice / Pity SP · 3. Healing · 4. Status ·
5. **Mobile** enemies · 6. Launcher slot + DEF-down/Painted · 7. Player attacks · 8. Summoned allies ·
9. Enemy actions · 10. Focus / Sleep / Call cards · 11. Revives

Ties break left-to-right or right-to-left depending on tier.

## Deflecting

If **DEF ≥ incoming damage**, the attack is deflected outright (0 damage), unless the attacker
pierces. `Sword` pierces 50% of enemy DEF (rounded up). `Defense ∞` and `Shield` always deflect.

## Enemy attributes

16 attributes, shown as icons under the enemy health bar. The important ones are **puzzle-locks**:

- **Flying** — most melee cannot target it (`Rocket Boots` excepted); projectiles can
- **Spiky** — Ball attacks deal nothing and refund 1 SP (functionally a Pass) unless `Cannonball`
- **Defense(n)** — flat reduction applied *per hit*, so it punishes multi-hit hardest
- **Mobile** — acts *before* the party, can interrupt with Exhausted/Confusion/Dizzy
- **Disables NRG** — locks the sword bar entirely

Enemy drop tables are keyed to attribute (plain / Flying / Spiky each have their own, per chapter).

## Sword Energy (NRG)

Party-shared ultimate meter, the Star Power analogue.

- Battle starts at **0.5**; max capacity **+1 bar per story sword** obtained.
- Gains: +0.1 per player attack, +0.1 per ally/enemy turn, **+0.35 from Focus**
  (+0.05 per `Deep Focus` stack).
- **Low-HP bonus**: +0.15 when attacking at ≤25% HP, +0.2 at exactly 1 HP.
- **Party tax**: all NRG gain −33% at 3 players, −50% at 4 players.
- Shared pool, so an ally can spend it out from under you — you're forced to Focus instead.

## Multiplayer scaling

Up to 4 players; others join mid-battle via a dust cloud in the overworld.

| Party | Effect |
|---|---|
| 1 | Enemies act **every other turn** (player effectively gets double turns) |
| 2 | Enemies act every turn |
| 3 | Enemy HP ×1.5, slightly more damage taken |
| 4 | Enemy HP ×2 |

## Ratings

Feedback popups for well-executed actions: **Nice → Good → Great → Amazing → Excellent**.
Tied to specific moves (e.g. Great = superguarding a 1-hit attack; Excellent = fully skill-checking
Dynamite, or completely dodging a multi-hit).

## Status effects

Grouped into Passive Buffs, Passive Debuffs, Removed-on-Strike, Turn-Based, and Unique. Notable:
ATK/DEF Up/Down, HP/SP/NRG Regen, Poison, Burn, Freeze, Sleep, Dizzy, Confusion, Silenced (disables
SP moves), Invisible, Dodgy, Charge, Shield, Ankh (damage dealt to the target heals you), Undead,
Big/Small, Painted, Butterfingers, Exhausted.

## Difficulty as an equippable

**Hard Mode** is a **0 BP passive card** handed to you at the start of a new file:
`+50% XP` in exchange for `2× damage taken`, `halved guard window`, `2× HP roll speed`.
Difficulty is a build decision, not a menu setting. Excellent idea.

## Other systems

- **Cooking** — item → item transmutation via an NPC; incompatible inputs yield "**A Mistake**",
  which deals 125 damage to *you*. Failure is content.
- **Items** — 158 unique + 11 key items; 10-slot battle inventory. Using a full-restore item at full
  HP/SP auto-Passes and refunds SP instead of wasting it.
- **Overworld** — 3D, visible enemies, contact initiates battle. Dash is cancellable by jumping or
  swinging the sword, which creates a real movement tech skill ceiling.

---

## Sources

- [Block Tales (game)](https://block-tales.fandom.com/wiki/Block_Tales_(game))
- [Combat](https://block-tales.fandom.com/wiki/Combat)
- [Level Up](https://block-tales.fandom.com/wiki/Level_Up)
- [Cards](https://block-tales.fandom.com/wiki/Cards)
- [Sword Energy](https://block-tales.fandom.com/wiki/Sword_Energy)
- [Build Points](https://block-tales.fandom.com/wiki/Build_Points)
- [Special Points](https://block-tales.fandom.com/wiki/Special_Points)
- [Attributes](https://block-tales.fandom.com/wiki/Attributes)
- [Status Effects](https://block-tales.fandom.com/wiki/Status_Effects)
- [Hard Mode](https://block-tales.fandom.com/wiki/Hard_Mode)
- [Items](https://block-tales.fandom.com/wiki/Items)
- [Chapters](https://block-tales.fandom.com/wiki/Chapters)
