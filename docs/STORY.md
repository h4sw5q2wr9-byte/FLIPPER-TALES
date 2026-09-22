# Flipper Tales — Story Bible

Everything in this game has had a mechanic before it had a reason. This document
is the reason. Nothing here contradicts what is already built; it explains it.

**Rule zero: original IP only.** No real company, product, protocol or brand
appears as a name, a character or a place. The module names are the exception
the design already makes — they are theme, and they do nothing to real hardware
(DESIGN.md §2.1).

---

## 1. The world in one paragraph

**The Carrier** is a vast derelict relay network that once carried every message
in the region. It is not a computer. It is a *landscape* — masts, spans,
corridors, junction halls, cold vaults — built over generations and lived in by
ordinary people: wreck-pickers, gate-wardens, signal-keepers. Their villages sit
in its junctions. Their work is its upkeep. Their kids play in its yards.

Then the Carrier went quiet.

---

## 2. The Silence

Messages stopped arriving. Not all at once — district by district, over a season,
until whole regions could no longer reach each other. Nobody has an explanation.
Everyone has a theory.

The Silence did two things.

**It cut people off.** A village three spans away might as well be on the moon.
Nobody knows if the next district is thriving or empty. This is why the people
you meet are wary, insular, and starved of news — and why a stranger who can
actually *travel* is either the best thing that ever happened to them or the
worst.

**It orphaned the machines.** The Carrier maintained itself with small automatic
things — packet runners, beacons, locks, gate drones, relay masts. They were
never dangerous, because they were told who belonged. With nothing left to tell
them, they kept doing their jobs and lost the part that made the jobs safe.

> A Sealed Lock still seals. A Gate Drone still guards its gate. A Blank Wall
> still stands in front of the thing it was built to stand in front of.
> None of them are evil. All of them are still working, and now everyone is an
> intruder.

This is the design's enemy roster, explained. It is also why they have no
dialogue and never will: they are not characters, they are unattended
machinery.

---

## 3. You

You are a **Courier** — one of the handheld units the Carrier used to move
messages that could not be trusted to the open network. Couriers were rare,
trusted, and physically walked their routes.

You wake in **Cold Boot** with:

- no memory,
- your module bay stripped down to the two things nobody bothers to steal
  (Sub-GHz and NFC),
- one **undelivered message** you cannot open.

Somebody wiped you. They left the message.

**Your goal is to deliver it.** That is the spine of the whole game: find out who
sent it, who it is for, and why erasing you was worth the trouble. The recipient
is on the far side of the Carrier, which means crossing every district between
here and there.

### Why you look like the enemy

You are made of the same parts as the things that have gone feral. To a village
that has been losing people to unattended machines, a strange unit walking out of
the dark is not a traveller — it is the problem, arriving. **Every settlement in
this game starts by not trusting you**, and the game's social beats are about
earning your way past that.

This is the story's most load-bearing idea. It gives every village a reason to
have a gate, every gate a reason to have a test, and the player a reason to do
something other than fight.

---

## 4. Why the road is locked

The Carrier was built in layers, and every layer checked credentials in a
different way. A district's boundary is a **port** you cannot open, and the thing
that opens it is a module you do not have yet.

That is the progression, and it is also why a module is a *story object* and not
just a stat:

| Ch. | District | Module | What the district is | Why that module opens it |
|---|---|---|---|---|
| 1 | **The Scrapline** | Infrared | Collapsed spans, missing floor, wreck-pickers living off the debris | Line-of-sight. Reaches across a gap you cannot walk. |
| 2 | **Cold Storage** | RFID | A sealed archive district nobody has opened since the Silence | Reads through a wall. Finds the door that was never drawn. |
| 3 | **The Turnstile** | iButton | Where the Carrier checked who you were. Now it checks everyone and passes nobody. | A contact key. You prove identity by touching. |
| 4 | **Signal Hill** | GPIO | Masts and dead cable runs. Everything still standing, nothing running. | Powers what is already there. |
| 5 | **The Deadzone** | BLE | Interference over everything. Where the Silence started. | Pairs with things and moves them. The way through *is* the obstacle. |

Each district already exists in the game as a **concept slice** — one room, its
locked port, and its enemy. The story says what the full district is for.

---

## 5. The people

Three things are true of everyone you meet:

1. **They have been alone for a season.** Whatever they know, they know from
   before the Silence or from inside their own walls.
2. **They are not helpless.** Nobody needs rescuing from their own life. They
   need one specific thing done that they cannot do because they cannot travel.
3. **They will not just tell you things.** Information is currency now.

Named characters so far:

- **The Keeper** (Cold Boot) — the one person still living in the boot district,
  maintaining a terminal nobody calls. Gives the Clean Run quest. Has been
  waiting a long time for something to come through.
- **Warden Coll** (Weldhome gate) — holds the gate into the Scrapline. Lost
  people to the feral machines. Will not open for a unit, and says so plainly.
- **Wren** (taken) — a Weldhome kid who went into a junction she was told to stay
  out of. See §6.

---

## 6. Chapter 1 — Weldhome, and the kid in the junction

The first real chapter, and the template for every chapter after it: **a village
that will not let you through, a reason you cannot fight your way past, and a
problem only a traveller can solve.**

### The beats

1. **The turn you cannot take.** Early in the Scrapline approach the corridor
   forks. One way carries on toward the village. The other drops into a dark
   side junction. Try it and the Courier says so: *"Nothing down there for me.
   Keep going."* It is not locked. You simply have no reason.

2. **The gate.** Weldhome's gate is held by **Warden Coll** and it does not open
   for you. Not a puzzle, not a fee — she has watched machines take three people
   and you are a machine that walks.

3. **The ask.** She names the terms herself, because she would rather be wrong
   about you than right: *"A kid went into the east junction two days back.
   Bring her out and I will believe you are not one of them."*

4. **The turn you can take now.** Walk back. The fork is still there. This time
   the Courier goes in — the line changes, because the reason changed.

5. **The junction.** Wren is held by a group at the far end. It is the hardest
   fight so far and it is guarded the way things guard: a **Blank Wall** in front
   of two live ones, so you cannot skip to the end.

6. **The walk back.** Wren follows you. She is small, quick, talks constantly,
   and is not scared of you at all — which is the point, and the first time
   anybody in this world isn't.

7. **The gate opens.** Coll does not apologise and does not gush. She opens the
   gate, and she is the first person to tell you something you did not know:
   there was another Courier, before you, going the same way.

### Why this shape

- The backtrack **re-uses a room you already walked**, which is cheap to build
  and makes the world feel like a place rather than a corridor.
- The "I don't need to go there" line is a **soft lock that costs nothing** — no
  key, no gate art, no new tile. The world opens because your reason changed.
- The escort walk back is a **victory lap with a companion**, which is the only
  time this game is not lonely, and therefore worth doing.
- Coll's last line is the **first thread of the main plot**. Chapter 1 ends by
  making the spine visible.

---

## 7. Where it is going

Kept here so the chapters point somewhere, not because any of it is built.

**The message is from the last Courier.** Sent before their own wipe, addressed
to whoever woke up next, because they knew they were about to be erased and knew
another unit would be started.

**The Silence was deliberate.** The Carrier was shut down to stop something from
travelling along it. Whoever did it was not a villain — they were out of options
and out of time, and they cut the network rather than let it carry what it was
about to carry.

**So the player's goal and the world's good are in tension.** Every port you open
makes the road a little more passable — for you, and for the thing the Silence
was holding back. The last chapter is the choice about whether to deliver the
message at all.

Nothing in the first four chapters needs to know this. It only has to stay true.

---

## 8. Voice

Short. Twenty characters a line, so nobody makes a speech.

- **The Courier** thinks in single practical sentences. No jokes, no angst. When
  the player would ask "why", the Courier states a fact instead.
- **Villagers** are direct, tired, and specific. They talk about their own work,
  not about the world's condition. Nobody explains lore at anybody.
- **Nothing that is not a person speaks.** Ever. If it has a health bar, it is
  silent.

### Lines that carry the tone

> *Nothing down there for me. Keep going.*

> *You walk like they do.*

> *A kid went east two days back.*

> *You are not the first one through here.*
