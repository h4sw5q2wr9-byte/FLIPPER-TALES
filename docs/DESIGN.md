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

| Class | Banner (§5) | Jam | Capture | PROTECT still helps |
|---|---|---|---|---|
| Normal | plain 1px box | ✅ | ✅ | ✅ |
| `GUARDED` | hatched box | ✅ | ❌ | ✅ |
| `UNDODGEABLE` | inverted box | ❌ | ❌ | ✅ |

The last column is the point of the class, and the banner used to hide it.
`UNDODGEABLE` means **no timed guard** — it does not mean nothing helps. The
Defend shield still blunts the hit, which is exactly what the coach line has
always told you to do ("brace"). A banner reading "UNDODGEABLE" over an attack
that PROTECT reduces is a lie by omission, so the banners now say what is
true: **"NO JAM - PROTECT"** and **"JAM ONLY - NO CAPTURE"**.

An enemy whose whole moveset is unguardable is just damage with extra steps,
so no enemy has one. The Sealed Lock has two attacks: an ordinary Clamp you
can jam *and* capture, and the Seal, which you can only brace against. That is
why the Lock can be blocked — half the time.

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

#### Attributes retarget; they never lock a module

An enemy's attributes decide **who an attack lands on**, never whether you may
choose it. `ft_encounter_effective_target()` walks the row from the near end
and takes the first living foe `ft_encounter_can_reach()` says the attack can
touch: swing NFC at a board of two flyers and a grounded foe, and it hits the
grounded one.

**There is no target cursor.** Picking a foe by hand was a whole extra control
— and a caret, and a coach line — for a decision that, on a row of at most
three with reach deciding most of it, makes itself. UP and DOWN now just move
the menu cursor like LEFT and RIGHT, so the whole menu works on either axis.

This replaced attribute-based lockouts, which were wrong on two counts. They
are not how the reference works — there, a grounded attack on a flyer is a
targeting problem, not a greyed-out button. And they made the player's own
toolset feel confiscated: the screen kept taking options away rather than
showing what they would do.

A strike-through now means exactly one thing: **you cannot afford this**. No
capture yet, no bar, meter jammed. The only case reach cannot explain by
itself is a swing with nothing at all to hit, and the description line says so
("None on the floor").

#### One row, five actions

The action row shows one action at a time — its name in a box, arrows either
side, and a line underneath saying what it does. LEFT and RIGHT cycle all
five. Nothing else.

It was a bar of three words with the three attack modules a drill-down behind
ATTACK. That put two of the five actions an extra press away and left the
battle screen carrying two rows of competing buttons on top of a status strip
and a description — four things asking to be read at once on a screen 64
pixels tall. Reported, twice, as cluttered.

**UP and DOWN do nothing here.** The row is horizontal, so it is steered
horizontally: they used to move the cursor as well, which meant a stray thumb
changed what you were about to do.

### 4.7b Losing

A lost fight **reloads the last save**, in full: the stats, the captures, the
cleared encounters, all of it.

It did not. Being downed healed you to the brim, moved you to the save point
and left everything else exactly as it was — so losing cost nothing and was
*strictly better* than walking away hurt. You kept the signals you had just
captured, kept every foe you had beaten since saving, and got a free top-up
for the trouble. Reported as "if you fail a battle you basically still win",
which is precisely right.

Reloading is what a checkpoint means. It is also the only thing that makes a
terminal worth walking to, and the DOWNED screen says so before the player
presses anything. A run with no save yet starts over instead — the first room
has a terminal and no foe, specifically so that cannot happen by surprise.

### 4.7c FAST, which had never once been true

`FT_ATTR_FAST` was a published attribute that nothing read. `ft_priority.c`
computed `FT_PRIO_FAST_ENEMY` and the encounter never asked, so the Sealed
Lock carried the tag through the whole prologue without ever acting early.

The rule now: **the player always opens a fight**, and from then on a round
runs *FAST foes → the player's turns → everything else*. Over a whole cycle
everyone still acts once, so raw damage is unchanged; what FAST buys is
position — its hit lands immediately before your turn, so you cannot brace
for it, and being low on Charge when it is about to act is a real problem.

Giving the fast foes the *opening* instead was tried and rejected: it took the
prologue's last fight from 57% to 32% at low skill, which is not "quick", it
is "ambushed".

FAST and JAM are on the title bar now. An attribute that changes the fight and
is not on the screen is a rule the player can only learn by losing to it.

### 4.7d Why Protect was never worth a turn

Bracing now also heals `FT_DEFEND_HEAL` Charge.

There was no way to recover Charge in a fight at all — not an item, not a
skill, nothing. Every fight was pure attrition, so attacking was always the
right answer and defending only changed how long the same loss took. "I never
use Protect and Focus" is the correct read of that game.

A small heal makes the turn a real question: spend it staying alive, or spend
it ending the fight. It still shields as well, because a heal that only buys
back what the turn cost is not a choice either.

### 4.7e HP, MP, SP

The three numbers on the status row are now called what they are: **HP**, **MP**
and **SP**. They were Charge, RAM and Signal — flavourful, and the reason
people could not tell what any of them was.

MP had a worse problem: it did nothing. `ft_module_ram_cost` was called from
nowhere, so MP was earned by guarding and by locked hits and never spent on
anything. A third of the readout was decoration, which is its own kind of
confusion. **NFC costs 1 MP now**, which gives MP a job, gives Guard a second
reason to exist, and gives a long fight a rhythm: hit hard until you are out,
then brace to get it back. Out of MP, the row says so and names the fix.

### 4.7f Two enemies that change the shape of a fight

Both are built from one new attribute each, and neither changes a number the
player has to track.

**`FT_ATTR_BULWARK` — Blank Wall.** Never attacks, and nothing behind it can
be touched while it stands. A broadcast included: a way round would make it
scenery. The row becomes a queue, and the foes behind it keep attacking the
whole time — being safe from you is not the same as being idle.

The first version blocked single-target attacks and was *transparent to the
one attack that hits everything*, because the broadcast path struck every
living foe directly and never asked about reach (`ft_resolve_hit` only knows
about AIRBORNE and ENCRYPTED). That roster measured 100% at every skill. With
the block honest it measured 15% at low skill, which was the mechanic and the
numbers both doing the work — so the wall came down to 14 HP and no shield. It
is a gate, not a boss, and it is worth little XP for the same reason.

**`FT_ATTR_SLEEPER` — Cold Booter.** Sits the fight out while anything else
lives, then wakes as the hardest thing on the board and leads with its biggest
attack. Clearing the room is what starts the fight, which inverts the usual
read: the quiet one in the corner is the reason you should have kept something
alive. It can be attacked while asleep, so dealing with it early is a choice
you are offered rather than a trap.

A foe that is not taking turns **says so** — a dormant badge on the sprite,
and `WALL` / `SLP` on the title bar. A bulwark that looks like any other enemy
is just an enemy with confusing rules, and a sleeper you cannot tell is asleep
is a surprise rather than a decision.

### 4.7g Status payloads, which now exist

`CORRUPT`, `DRAIN` and `STALL` were in the data from the first commit and none
of them did anything. `ft_resolve_hit` even computed `payload_applied` and
nobody ever read it — so an attack whose whole character was "this one
corrupts you" hit for its number and left nothing behind.

| Payload | Per round | Tag |
|---|---|---|
| `CORRUPT` | `FT_CORRUPT_DAMAGE` off your HP | `DOT` |
| `DRAIN` | `FT_DRAIN_MP` off your MP | `MP-` |
| `STALL` | one of your two actions | `SLOW` |

They last `FT_STATUS_TURNS` rounds and bite **once at the top of each player
round**, not once per action: a per-action drip would charge twice over for no
reason the player could see. A jam or a capture still nullifies them outright,
which `ft_resolve_hit` already decided and which matters now that it means
something.

Only one tag shows at a time, beside the player in the arena — worst first,
because three at once would need a row the screen does not have. The guide
lists a payload as its own column on the attack line, since an attack that
corrupts you is a different problem from one that hits for the same number and
does not.

Turning them on cost the ladder a few points exactly where it should: the Rime
Shell's freeze took its fight from 97% to 86% at low skill, and the Mast
Relay's surge took its from 89% to 69%.

### 4.7h Who reached whom

Attacking a foe in the overworld already gave you a free hit. Now the reverse
is true: a foe whose **step lands on you** gets the opening turn.

`FtWorld` reports it as `ambushed`, set for one update like `arrived`, and set
only when a *walker's* step completes on the player's tile — walking into one
yourself does not count, which is the whole distinction. The app turns that
into `ft_encounter_enemy_opens()`, which hands the round's first turn to the
foes, quickest first, exactly as any other round opens.

It falls back safely: a board whose only foe never takes turns — a lone
bulwark — cannot be handed the opening, so the fight starts normally rather
than stalling before it begins.

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

**A broadcast reaches them in order.** `ft_encounter_foe_hit_at(e, i)` returns
the animation frame at which foe `i` is struck, spread across `FT_ANIM_EMIT`
→ `FT_ANIM_RECOVER` by position: nearest first, furthest last. The three foes
used to flinch and die in unison while the signal was still leaving the
player. A contact attack has nothing to sweep across and lands on
`FT_ANIM_STRIKE`, as before.

**A contact attack closes the real distance.** The lunge is scaled to
`foe_x(target) - reach`, so striking the far side of a three-wide row is a
longer approach than striking the one standing next to you.

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
│ ▮▮▮▮▯▯▯ 18  R7  S ▮▮▯                      │  player bars, 10px
│ [ATTACK]  PROTECT  FOCUS                   │  root menu, 10px
│  One foe, strong.                          │  one line, 8px
└────────────────────────────────────────────┘  y=63
```

The attack-telegraph banner overlays the battle scene, centred. So does the
module panel, on the left half only.

**Four rules keep that from being cluttered**, all learned by looking at it:

- **Only the cursor is boxed.** Three framed buttons over a framed bar over a
  framed popup is four competing rectangles; the highlight alone already says
  which entry is selected.
- **The coach speaks on the description line.** It used to get its own framed
  callout floating over the arena, which covered the fighters at exactly the
  moment you were choosing what to do to them. One line, one place: the hint
  when there is one, the action's description otherwise.
- **No "CHG" label.** A quarter-ticked bar with a number beside it is already
  unambiguous, and those three characters were the difference between a row
  that reads and a row that is merely full.
- **The modules take the row over; they do not float above it.** Choosing
  Attack swaps the three root words for the three module names in the same
  band, with a `<` marking the level. The first version was a panel floating
  over the arena: it hid the foes you were aiming at, and it overlapped the
  status strip so a full Charge bar appeared sliced off mid-fill. It read as a
  rendering fault rather than as a menu.

Two more glitches came out of looking at the panel at 6x rather than at 1x:

- The **target caret** was a wedge in the two spare pixels between the title
  rule and the arena — which is exactly where the tallest enemy's antenna
  lives, so on a real board it simply disappeared. It is now a pair of
  brackets flanking the foe, in the 4px gaps that the 20px spacing guarantees
  are empty, and it is drawn only where aiming means something: never for a
  broadcast, never for Defend or Focus.
- Everything in the status strip now stops at `FT_STATUS_Y + 6`, one row short
  of the menu band. Drawn to the full height, a full Charge bar and the menu's
  highlight ran together into one black slab.

#### Attacks with weight

Three rules, each fixing something that read as a sprite being slid around
rather than as something hitting something.

**The lunge eases.** `lunge_px()` pulls away from the target (`ease_out`, so
it is a quick withdrawal that then hangs there — the pause before a punch),
accelerates across the gap (`ease_in`, so the fastest frame is the frame of
contact), holds at full extension for `LUNGE_HOLD`, then settles back. The
first version was three straight lines at constant speed in each direction.

**The impact burst exists only on impact.** Eight spokes thrown out from the
contact point, growing over `BURST_MS` and gone. The old spark was three fixed
diagonal scratches drawn for the whole approach, so the "impact" was on screen
long before anything arrived.

**Foes are seen to die.** `ft_encounter_foe_defeat()` returns 0–255 through a
fold: the sprite loses height from the top as it crumples, and past halfway it
starts dropping pixels so the silhouette comes apart rather than just
shrinking. Before this a foe vanished between two frames the instant its bar
hit zero. A foe struck early by a sweeping broadcast falls more slowly than
the last one reached, so the whole row finishes together as the turn ends.

Easing is integer: `ease_in`/`ease_out` are quadratics over 0..span, and the
worst case stays inside `int32_t` by a wide margin.

#### The hit flinch

A foe attack that actually takes Charge off you strobes both fighters for
`FT_FLICKER_MS`, inverting them every 45ms, and that is all.

It was briefly a full four-stage iris: flicker, close, hold black, open. That
was a mistake and it was removed. An interruption that good has to be rare,
and being hit is not rare — in a three-foe round it fired three times in one
turn, each time taking the fight off the screen for most of a second. A jam or
a capture never had one and still does not.

The iris itself was not the problem; **where** it was spent was. It now
belongs to the scene wipe below, which happens once per fight.

The XOR goes **over** the drawn sprite. Inverting the empty space first and
then drawing the sprite black-on-black just yields a solid brick, which is
what the first pass did.

#### The scene wipe

Starting or ending a fight uses the same ring, from `ft_wipe_at(ms)`:
`FT_WIPE_CLOSING` for `FT_WIPE_CLOSE_MS`, then `FT_WIPE_OPENING` for
`FT_WIPE_OPEN_MS`, then nothing. The scene behind it swaps at `FT_WIPE_SWAP`
— the instant it is fully shut — so the overworld is never seen turning into
a battle. While the wipe runs the app freezes the world, stops ticking the
encounter and drops every input but held direction, because anything that
happens behind the black is something the player did not see and cannot have
reacted to.

The hit iris and the scene wipe share one shape on purpose: the screen
closing means *something just changed*, whether that is a room or your Charge.

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

### 5.2 The practice arena

A second way in, for trying things rather than progressing: **Pause → Practice
arena**. Three settings and a button.

| Row | Values | What it is for |
|---|---|---|
| Foes | Random, or seven fixed line-ups | Random rolls 1–3 foes freely; the fixed sets cover each attribute, a crowd, and the boards where one module cannot do the job |
| Level | 1–10 | Applies real level-ups, cycling Charge / RAM / Flash, so the stats are ones the game can actually produce |
| Kit | Basic, Loaded, Max | Basic is what you start with; Loaded installs every card once; Max stacks each as far as it goes |

Two rules make it a place to experiment rather than a second campaign:

- **A match touches nothing.** Winning clears no entity, losing costs no
  Charge and sends you to no terminal. It ends and drops you back on the setup
  screen, ready to go again.
- **A kit you cannot use is not a kit.** The loaded sets start with a captured
  attack and a full meter, or Signal is a button that does nothing for the
  first four turns and the whole point of picking the kit is lost. Hard Mode is
  the one card never installed for you — "more abilities" should not silently
  mean "twice the damage taken".

`ft_practice.c` is pure core, so `make -C test test` checks that every setting
combination builds a legal encounter, that Random actually varies, that levels
raise the ceiling and that a match terminates. The preview renders every row
and every value and fails on overflow, which is how the FIGHT row was caught
colliding with the help line.

### 5.3 Saving

The format lives in `src/core/ft_save.c`; the SD card lives in
`src/app/ft_storage.c`. That split is the point: every byte of the layout,
every rejection and the whole world round trip are tested on a host with no
card in sight.

A file is a 6-byte header (magic, version, payload length), the payload, and a
4-byte FNV-1a checksum **over the header as well**. Fields are written
little-endian one byte at a time rather than by memcpy-ing structs, so the
layout cannot change silently when a field is reordered or the compiler pads
differently. A file that is foreign, the wrong version, the wrong declared
length, truncated or corrupt is **refused**, and a refusal means a new game —
a garbled save is worse than a missing one. The tests flip every single bit in
a save and assert none of them decodes.

**Terminals are the save point**, and saving is what a terminal is for: using
one restores Charge and RAM *and* writes the run out, because walking across a
room for something that does half its job is a bad deal. The pause menu's
**Save** works only at a terminal and says where to find one otherwise, which
is how the rule teaches itself.

A save also records **where you came back from**. Being downed used to return
you to a hardcoded room 0, which quietly undid everything past it; it now
returns you to the terminal you last saved at. Placing or moving an orb writes
the file straight away, as does collecting a quest reward, because progress
you made and then lost on the next screen is the worst possible outcome.

The payload carries the run's stats (including `orbs` and `spent[]`), the
loadout, the signal library, the field guide, quest state, where you stand,
where you save, the cleared bits, and whether coaching is on. Version **4**.

`ft_save_to_world()` restores the cleared-entity flags **before** entering the
room, because entering is what decides which foes spawn. Load-then-enter would
put a foe you already beat back on its tile and only then mark it dead.

**New game** is the one irreversible thing on the menu, so it asks first, and
No is the default answer.

### 5.4 Levelling, and orbs

`ft_encounter_xp()` totals a won fight, tapering each foe against the player's
level separately so a mixed group pays properly rather than being averaged.
`ft_xp_gain()` banks it — capped at `FT_XP_BATTLE_CAP` per battle, so no one
fight ever hands over two levels — and reports how many level-ups are **owed**.

A level is no longer a stat choice. `ft_level_take()` raises `level`, restores
HP and MP in full, and pays out `FT_ORBS_PER_LEVEL` **orbs**. What an orb
becomes is decided somewhere else, and can be decided again later.

That is the whole point. A build you cannot change is one you have to be told
about beforehand, and there is nowhere on a 128×64 panel to tell anyone that
MP only matters if they intend to lean on NFC. Under the old rule a player who
put four levels into Cards before finding out what Cards were for carried that
for the rest of the run.

**The orb screen** (`ft_render_orbs`) is reachable two ways: automatically
when a win leaves orbs in hand, and from the pause menu at any time outside a
fight. Three rows — HP, MP, Cards — each showing the current maximum and, in
brackets, how many orbs are sitting in it. RIGHT or OK puts one in, LEFT takes
one back out. Every move writes the save, so nothing here can be lost to a
bad next fight.

The rules that make it safe to be reversible:

- **`spent[]`, not accumulation.** `FtStats` records how many orbs went into
  each stat. Without that there is no way to tell a levelled stat from a
  starting one, and no way to know how much to give back.
- **HP is granted on the spot.** Moving an orb into HP heals you by that much
  immediately. This is a thing you are meant to be able to do when hurt, not a
  promise for the next fight.
- **Refunding HP never downs you.** Current HP is clamped to the new maximum
  and then floored at 1. Being downed is something a fight does.
- **Cards refuse a refund that would go negative.** `flash_used` is a budget
  something is already spending; pulling a slot out from under an installed
  card would leave `flash_used > flash_max`, which every install check
  downstream reads as "no room" forever.
- **Not mid-fight.** The pause menu's Orbs row says "not in battle" rather
  than disappearing. Moving a point to survive a hit you have already taken is
  not a build decision.

Caps still bite: a capped stat refuses the orb and the orb stays in hand,
rather than vanishing into a row that could not take it.

`ft_level_take()` is what actually raises `level`. Its predecessor did not,
which meant the level never moved, the chapter cap never bit, and every enemy
was worth full XP forever. The anti-farming taper only started working once
that was fixed.

### 5.4a Quests

A quest is a **condition over state the world already tracks** — which room
you walked into, whether a fight started — rather than a script with its own
idea of where you are. That is what keeps it testable, and what stops a quest
and the world disagreeing about what happened.

`ft_quest.c` holds one quest so far, **Clean Run**: touch the Cold Gate at the
far end of the prologue and come back, without a single fight. It pays two
orbs. Four rooms out and four back, with foes respawning behind you (§5.3),
so "without a fight" is a route to find rather than a formality.

States run `UNKNOWN → ACTIVE → READY → DONE`, with `FAILED` hanging off the
middle. `ft_quest_enter_room()` flips ACTIVE to READY on arrival at the goal;
`ft_quest_battle()`, called the moment a battle is built, fails anything live
that asked you not to fight. READY fails too — turning round at the gate and
punching your way home is not a clean run, and the reward is for the route.

Failing **re-offers** rather than closing the door. A quest that can only be
failed once is a punishment for trying it early, which is exactly when a
player would try it.

**The giver** is an `FT_ENT_NPC` entity standing in room 0, two tiles off the
line between where you wake up and the door, so you meet them by choice. NPCs
are solid: you walk into one, which leaves you facing them, and OK talks. Up
to three lines of at most 20 characters come back out of core as data; the app
only draws them. A test walks every state and measures every line.

The quest state rides in `FtWorld` and in the save (`FT_QUEST_BYTES`, sized
with headroom so adding a quest does not change the layout).

#### Gated exits, and the turn you cannot take

`FtExit` carries `need_quest` (a quest id plus one, 0 for always open) and
`need_state`. An exit whose quest has not reached that state refuses, and says
why. That one field covers both of Chapter 1's gates:

- **The drop** in the Approach needs `FT_QUEST_WREN` at `ACTIVE`. Before Coll
  asks, the Courier says *"Nothing down there."* and stays put.
- **Weldhome's gate** needs it at `DONE`. Warden Coll stands beside it, not on
  it, so the exit is what refuses and she is what explains.

Neither is locked with a key, an item or a tile of its own. **The world opens
because your reason changed**, which is the cheapest possible way to make a
place you already walked past mean something later — no new art, no new
mechanic, and the room is already built.

#### Walking somebody home

`FtWorld` carries an `escort` flag and one stepper. She enters the tile you
are leaving **on the frame you leave it**, hooked into the same branch that
starts the player's step, so the two move in lockstep and she never falls
behind however long the direction is held. Every tile she walks is a tile you
walked, so she can never end up inside a wall or cut a corner through one. A
door, a ladder or a drop moves you further than a step, and there she catches
up rather than walking it.

She is in the save, because a terminal half way home is a terminal.

### 5.4b Chapter 1

See STORY.md 6 for why. Mechanically it is three rooms appended after the
concept slices (`FT_ROOM_CH1_FIRST`), so nothing before them was renumbered:

| Room | What it is |
|---|---|
| 9 The Approach | The fork. Two ways on, and a drop you have no reason to take. |
| 10 Weldhome Gate | A village whose gate does not open for a unit. Warden Coll. |
| 11 East Junction | Wren, behind a wall and two live ones. |

The junction's roster is `{Blank Wall, Scrap Crawler, Stray Packet}` — measured
at 46/83/97 across the simulator's three skill levels, level with the hardest
roster in the game and still clearable. The first draft paired the wall with
two AIRBORNE foes and read 19/52/78: the wall blocks the broadcast and the
flyers refuse contact, so the fight had two locks and no key.

### 5.4c Pockets

The only healing used to be a terminal you walked back to and Protect, which
restores two. That makes every fight a one-way trip and every wrong turn a
reload.

Three items — Apple (+5 HP), Ration (+12 HP), Cell (+4 MP) — and **six slots
total**, across every kind. The cap is the design: without one, food stops
being a decision and becomes a chore you do before every fight.

Two sources, which behave differently on purpose:

- **Trees** grow back when you walk the room again, exactly as the foes do
  (§5.3). That is what makes re-walking a cleared room worth the trip.
- **Caches** stay taken. Something somebody left is a reason to have gone
  somewhere once, not a vending machine.

Face one and press OK. **Full pockets leave it where it is** rather than
swallowing it — picking something you cannot carry and watching it vanish is
the worst possible outcome. A tree is solid until it is picked, so a cleared
room does not keep a stump in the way.

Using one, in a fight, is the sixth action. The row names the actual item and
its effect ("Apple x2" / "+5 HP") and UP/DOWN choose which — a second menu
level would be the drill-down all over again, and the action row is already a
left/right ring. Healing goes through the rolling HP (§4.6), so a lethal hit
that has not landed yet can be eaten out of. Outside a fight, the pause menu's
**Pockets** screen does the same job.

Either way **eating at full health is refused**, not allowed: the row reads
"Nothing to mend" and the press does nothing. Six slots is too few to let a
stray OK throw one away.

### 5.4d Sound

The Flipper's speaker is a piezo buzzer and the official HAL offers exactly
one thing: `furi_hal_speaker_start(frequency, volume)`. So the game is a
chiptune with one voice, and every cue is a list of notes.

WAV playback is possible — community players bit-bang PWM — but it sounds
thin through a buzzer, needs constant CPU while the game is drawing 30fps,
and needs a streaming buffer in a FAP that is loaded entirely into RAM. Tones
are what actually sound like a game here.

The split is the usual one. `src/core/ft_audio.c` holds the cues as data, so
a test can walk every one of them and check it is audible, short enough, and
that a perfect block does not sound like a jam. `src/app/ft_sound.c` is the
only file that touches the speaker.

Two rules the player never sees but would notice immediately if they were
broken:

- **Nothing blocks.** A cue is started and then advanced from the game's own
  tick, so a fanfare never holds up a frame. It is ticked before the early
  returns, so it keeps playing through the pause menu and the wipe.
- **The speaker is handed back.** It is acquired per cue and released the
  moment there is nothing playing, and on exit before anything else. Leaving
  it held would lock every other app out of it until the Flipper restarted.

Cues fire off phase changes and the animation's own strike frame, not from
the resolver — so core stays free of the speaker, and a hit is heard when the
sprite moves rather than when the arithmetic happened.

**Sound** is a row in the pause menu and rides the save.

### 5.5 The debug menu

Everything that exists to test the game rather than to play it lives behind
one door, **Pause → Debug**, so the pause menu stays the player's.

| Row | What it does |
|---|---|
| Go | LEFT/RIGHT picks any room by name, OK walks you into it |
| Practice arena | The setup screen from 5.2 |
| Heal | Charge and RAM to full |
| Add 100 XP | Banks a battle's worth, and opens the level-up screen if it owes one |
| Clear room | Marks every encounter in the current room beaten |

Travel lands on the room's **first exit**, which is guaranteed to be a door
the player can stand in — the tests check that for every room, so the warp
cannot drop you inside a wall.

Every menu in the game now goes through one renderer, `ft_render_menu_list`:
a title, rows, an optional value per row, five visible at a time, and a
scrollbar when there is more. The pause menu had grown from four rows to seven
with the spacing re-derived by hand each time, which is how one version ended
up with its highlight touching the rules above and below.

One bug worth recording, because the preview harness could not see it: the
value is drawn right-aligned and the label was clipped against the row's full
width, so a long value printed straight through the label — "Travel" and
"Boot Corridor" on top of each other. Both were comfortably on screen, so the
off-panel check passed. The value takes its space first now and the label gets
what is left, and the row is labelled "Go" so there is space to give.

### 5.6 The field guide

**Pause → Field guide**: one entry per enemy, and nothing is listed until it
has been met. A guide that ships knowing everything is a manual; this is a
record of the run, so it is saved with the run and lost with it.

An entry is recorded when a fight *starts*, not when it is won — the thing
that beat you is exactly the one you want to look up.

Each page carries the sprite (so the page and the thing in the corridor
match), Charge, the trait tags, a line per trait saying **what it costs you**
rather than restating its name — "Airborne: NFC misses", "Fast: moves first",
"Jams the S meter" — and one line per attack: its reach, its power, and what a
guard can do about it ("Close 6  jam only").

The page is four rows of eight below the header, which is exactly what the
busiest enemy needs: two traits and two attacks. At nine rows the Sealed Lock
lost its second attack off the bottom, which is the half of its moveset you
most need to look up.

`ft_guide.c` is pure core — one bit per enemy, asserted at compile time to fit
the bitfield — so every line of entry text is width-checked by the tests and
every page is rendered by the preview.

### 5.7 Chasing

Foes chase on a **flow field**: breadth-first out from the player's tile, so
every walkable tile knows how far it is, and a chaser walks downhill.

Before this it was a greedy step with a few fallbacks, which is not
pathfinding — it cannot route around anything longer than itself. Cold
Storage's sealed cell, the Turnstile's ranks and the Deadzone's voids all
defeated it: a foe would walk into the wall between you and it until you left.
The test walks every room, alerts a foe and requires it to actually arrive;
three rooms failed before the field went in.

One search per room per player tile serves every chaser in it, which is what
makes it affordable. The buffers are file statics rather than part of
`FtWorld`: they are scratch, rebuilt whenever used, and `FtWorld` gets copied
around (saves, tests) where another kilobyte and a half would ride along for
nothing.

#### The beat before the chase

A foe that starts walking on the frame it notices you gives the player nothing
to react to: the first thing you know about it is that it is already moving.

`FT_FOE_NOTICE_MS` (500) is the pause between the two. The transition from
not-alert to alert arms `notice_ms` on the whole group; while it runs nobody
thinks or takes a new step, and a mark is drawn over the group's first walker.
Then they come.

Three details that matter:

- **Only the transition arms it.** Re-arming every frame the player stayed in
  range would freeze the room solid.
- **A step already under way finishes.** Stopping dead mid-tile would break
  the grid everything else depends on.
- **One mark, not three.** A roster of three with three marks is a row of
  punctuation, not a warning.

The mark is clamped downward rather than skipped when it would run off the top
of the panel. A warning you only get in open ground is not a warning, and a
skipped draw is invisible to the layout checker.

### 5.8 The guard aftermath

The strike check has always frozen its cursor where the player pressed. The
guard check drew nothing at all, so a block that went up a fifth of a second
too early and an attack that could not be blocked looked identical: you took
the hit either way and learned nothing from it.

Two readings now come out of the same press.

**While the wind-up finishes**, the marker freezes and flashes where the guard
went up, and a thin line keeps sweeping to the impact edge. The gap between
them is the error, closing in real time. The line is deliberately thin rather
than a second cursor — the eye should follow the gap shrinking, not mistake it
for another thing to aim with.

**After it lands**, the popup's second line carries the number.
`ft_encounter_guard_offset()` reports how many milliseconds before impact the
press was, or −1 for no press at all, and the renderer turns that into one of
three readings:

| reading | means |
|---|---|
| `at 40ms` | inside the window; the number is how tight it was |
| `450ms early` | outside it, by that much — the number you can act on |
| `no guard` | nothing was pressed |

Inside the window the figure is a skill readout; outside it, it is the
correction. "You were early" and "you did nothing" are different facts and now
look different.

Unlike the strike check, the press does **not** stop the clock. The hit is
still coming — only the marker freezes.

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

Built: tiles, collision, camera, renderer, grid stepping, a four-room prologue
chain, room transitions, the pause menu, patrolling foes, terminals, saving
and levelling. `make -C test map` renders every room at panel resolution and
whole.
Still to build: the overworld strike's feedback, items, and NPCs.

#### Tiles and the viewport

8px tiles, drawn at **2x**, give an **8×4 viewport**. At 1:1 a whole prologue
room fitted on the panel at once and everything in it was 8 pixels of a 128
pixel screen: legible, but it read as a diagram rather than a place. Doubling
halves the visible area and doubles how much of the screen the player
occupies.

The world is untouched by this — tiles, collision, stepping and the camera all
still work in 8px tiles, and `FT_ZOOM` lives only in `ft_overworld.c`, applied
on the way out. What changed alongside it:

- **The player and the foes are drawn at the panel's own resolution**, not the
  world's. The tiles are 8px art doubled and are meant to be chunky; keeping
  the two things you actually look at crisp is what makes the zoom read as
  "closer" rather than as "bigger pixels". Foes are their full 16×16 battle
  sprites now instead of being sampled down to 8×8, so the thing in the
  corridor is visibly the thing you are about to fight.
- **The avatar is a 16×20 sprite of its own**, bottom-aligned so it stands a
  little taller than the tile it occupies.
- **`FT_FOE_ALERT` dropped from 5 tiles to 3.** The viewport is ±4 tiles wide
  and ±2 tall now, so a 5-tile notice radius meant being charged by something
  that was never on screen. Maps are one byte per tile,
row-major — the format production maps will stream from the SD card, so nothing
about the renderer changes when they do. Maps, tiles and sprites are all
authored as editable ASCII under `tools/`.

Ten tiles: floor, wall, void, grass, cable, door, terminal, locked port, crate,
ladder.

#### One hero, both scenes

There is exactly one piece of player art, `FT_SPRITE_HERO`, 16x18, drawn by
the battle screen and the overworld alike.

There used to be two: a 16x16 battle sprite and a separate overworld avatar,
8x12 and then redrawn at 16x20 when the map zoomed. They were the same
character described twice and they did not match — which is the first thing
anyone reads, and reading it wrong makes the two halves of the game feel like
two games.

18 rows because that is exactly the battle arena's band: bottom-aligned on the
floor line it fills 11..29, two rows taller than a 16x16 foe, which is what a
protagonist standing next to one should look like. In the overworld the same
bottom alignment leaves him standing two pixels proud of his 16px tile.

The face is in the bitmap. Each scene overlays what it needs on the screen
area — the overworld knocks out the eye band and redraws the pupils to show
facing, the battle darkens the whole screen while Charge is draining.

Removing the second avatar also took `FT_AVATAR_W/H` out of `src/core`, which
turned out to matter: `ft_stepper_pos()` was still subtracting the difference
between the *old* avatar's height and a tile so its caller could draw
top-aligned. The renderers bottom-align themselves now, so that offset was
being applied twice and every actor in the overworld floated half a tile above
the ground it was standing on. Core does not know how tall anything is drawn.

#### Drawing the player and the foes

Both get a **white keyline**: the sprite's own shape widened by one pixel,
drawn white, with the black sprite on top. Two earlier attempts failed in
ways worth recording, because each looked fine in isolation:

- A white *box* behind the avatar swallowed whatever it stood next to.
- XOR-ing the sprite fixed that but broke the silhouette — over a dithered
  tile the body came out checkered, and standing half on a dark tile split it
  down the middle into two colours.

The keyline erases nothing beyond its own outline and keeps the figure black
and whole on every background, including the solid wall bands. Foes get the
same treatment, or one standing on a black band is a smudge in it.

The avatar is **solid, with the screen knocked out in white** after the body
is drawn. Drawing the case as an outline left the head an empty rectangle,
which reads as a picture frame standing on legs. Facing is two pupils on that
screen rather than four sprite sets, and facing away simply leaves the screen
dark.

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

Persistent flags (an item taken, a port unlocked) live in the save as a
bitfield keyed by room and entity index, so the world remembers what you did
without storing the world.

**Foes are not persistent.** Walking into a room repopulates it, every time,
including on a load. A cleared corridor used to stay cleared forever, which
made backtracking free and "go round again" the answer to everything; now the
room is as dangerous on the way back as it was on the way in. The bitfield
stays, because it is how anything genuinely permanent will be remembered — but
a beaten foe is only beaten for the visit.

**NPCs are.** `FT_ENT_NPC` reuses the roster byte as a quest id. They never
move, are never cleared, and are **solid**: walking into one stops you, which
is what leaves you facing them, and OK talks. Nothing about them starts a
fight — `ft_world_foe_ahead` and `ft_world_foe_contact` both ignore them, so
the strike-first rule cannot fire on a person.

#### Foe behaviour

A marker that fights as three **walks as three separate actors**. `FtFoeState`
is the encounter marker — alive, alert, and a count — and holds one
`FtFoeWalker` per roster member, each with its own stepper, think clock, home
tile and xorshift seed. The first pass drew one leader with two sprites pinned
at fixed offsets, which made a group of three read as a single object being
dragged about; the pass before that drew one sprite for all three, so what you
walked into was not what you fought.

Touching **any** walker starts the marker's fight, and beating the marker
removes the whole group. Walkers also refuse to step onto a tile another
walker occupies or is stepping into — without that, three wanderers converge
and sit on top of each other, which is the welded look again.

`foe_think()` runs on a `FT_FOE_THINK_MS` clock, per walker, so they move
independently rather than as one block.

Unaware, a foe wanders on its own seed and idles through most ticks. Once it
is more than `FT_FOE_LEASH` tiles from home it heads back, so an idle room
does not slowly empty itself into a corner. This is what keeps a foe in the
region it was placed in without fencing it in with collision.

**Alert is shared.** If any foe in the room has the player within
`FT_FOE_ALERT` tiles, every foe in the room is alerted. A group that reacts
one at a time reads as three oblivious animals instead of something that has
seen you.

An alerted foe closes the larger gap first, with a one-in-five jitter so
several chasers do not stack into a single column. `FT_FOE_STEP_MS` is close
enough to `FT_STEP_MS` that a chase is a real threat, but not so close that
you can never break away — the player reported the first version as
inescapable.

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

#### One enemy per area *(built)*

The slices looked like five places and fought like one — every encounter in
all nine rooms was the prologue's Packet, Beacon and Lock. Each area now has
a foe of its own, built from mechanics the prologue already taught:

| Area | Enemy | What it does |
|---|---|---|
| The Scrapline | **Scrap Crawler** | `FAST`, fragile, hits hard. Initiative is a stat: kill it early or brace for a hit you cannot out-race |
| Cold Storage | **Rime Shell** | `ENCRYPTED` *and* shield 3. Broadcast is refunded, unpierced contact barely scratches it — the fight that makes Payload worth its Flash |
| The Turnstile | **Gate Drone** | `AIRBORNE` + `FAST`. Out of contact's reach and moving before you |
| Signal Hill | **Mast Relay** | `JAMMER`. The meter is locked all fight: no Focus, no replays, the two base modules carry it |
| The Deadzone | **Null Field** | `ENCRYPTED` + `JAMMER`, and both its attacks are `GUARDED`. No broadcast damage, no meter, no new captures. The area is named for what it takes away |

Each roster pairs the area's enemy with something from the prologue, so a
fight is one new idea plus a thing you already know rather than two puzzles at
once. Measured at level 1 on the base kit, the ladder runs 100 / 75 / 97 / 57
/ 89 / 78 percent at low skill across the five areas — `make -C test sim`
walks every roster, not just the prologue's.

**No enemy is ever both `AIRBORNE` and `ENCRYPTED`.** That pair is immune to
both base modules at once, which is not difficulty, it is a fight that cannot
be finished. A test asserts it, along with: every attack id being unique
(duplicates would replay the wrong captured signal), and every enemy taking
damage from a perfect hit with whichever base module can reach it.

Sprites are picked by **enemy id**, not by attributes. Attributes stopped
being unique the moment two enemies shared one, and the old attribute lookup
silently handed the Gate Drone the Drift Beacon's body.

#### Concept slices *(built)*

One room from each of the five, chained on past Cold Gate so the areas can be
**walked** rather than read about. They are a sample of each chapter, not the
chapter: enough to establish what the place looks like, what is in it, and
what its module would be for.

| Room | What it shows | The module's job, made visible |
|---|---|---|
| The Scrapline | Wreckage and a span of missing floor | A terminal sealed in a pocket behind the gap. Infrared is line-of-sight, so it reaches what you cannot walk to |
| Cold Storage | A frosted floor around a sealed cell | The cell has no door drawn. RFID reads through walls and finds the one that was never there |
| The Turnstile | Ranks of locked ports across the route | The gate the chapter is named after. You squeeze past it below; iButton is the way through |
| Signal Hill | Pylons and dead cable runs on a terrace | Everything is in place and nothing is powered. GPIO turns it on |
| The Deadzone | Interference over the whole floor, and holes | Crates in the way that BLE would pair with and move |

Each area gets a **ground or obstacle of its own** — `FT_TILE_SCRAP`,
`FT_TILE_FROST`, `FT_TILE_PYLON`, `FT_TILE_STATIC` — because an area that
reuses the prologue's corridor tiles is not a place, it is the same corridor
with a different name over the door. The Turnstile is the one exception: its
identity is the locked ports, which already existed.

Every slice carries **the gate for the chapter after it**, so the whole
progression is walkable: you can see every door you cannot open yet.

Three checks keep the set honest, all of which caught something real:

- `tools/genmaps.py` refuses to emit a room whose border is not sealed except
  at its doors. Cold Storage was leaking off its right edge — invisible when
  you are counting characters by eye.
- The tests **flood-fill each room from its first door** and require every
  entity and every exit to be reachable. Signal Hill's upper terrace was cut
  in two by its own mast, stranding a foe; The Scrapline's gap could be
  walked around, which was the entire point of the room.
- The cleared-entity bitfield is asserted at compile time to cover every room
  times every entity. At 8 bytes the nine rooms used 54 of its 64 bits, and
  the eleventh room would have silently stopped being recorded.

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
