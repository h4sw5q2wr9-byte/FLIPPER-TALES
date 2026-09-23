"""Author the 1-bit character art as ASCII, preview it, and emit a C header.

Sprites live here rather than in hand-written hex so they stay editable: change
the ASCII, re-run, look at the PNG.
"""
import sys
from PIL import Image

SPRITES = {}

# Art rules that survive 16x16 at one bit: a solid 2px silhouette, a white
# interior, and dark features at least 2x2 inside it. Mixing thin outline with
# fill turns to mush at this size.

# Queue Barrier (BULWARK). Two posts and a belt between them — the thing that
# made people queue at the Turnstile, with its sign hanging off the belt. It
# never attacks; it makes you fight the others in line. No face, and the
# widest footprint in the game.
SPRITES["barrier"] = """
................
.####......####.
.####......####.
..##........##..
..############..
..##.######.##..
..##.#....#.##..
..##.#.##.#.##..
..##.#.##.#.##..
..##.#....#.##..
..##.######.##..
..##........##..
..##........##..
.####......####.
######....######
######....######
"""

# Night Shift (SLEEPER). Asleep in a nightcap, eyes shut, until the room goes
# quiet and it clocks in. It has to look harmless, because the point is that
# it is not.
SPRITES["nightshift"] = """
...........###..
.........####...
.......#####....
....#######.....
..###########...
.##############.
##............##
#..####..####..#
#...##....##...#
#..............#
#......##......#
#..............#
.##############.
..############..
....##....##....
....##....##....
"""


# ---- People ------------------------------------------------------------
#
# Everyone in this game is made of the same parts — a head, a body, two legs
# — so the silhouette is the only thing that can tell them apart at 16x16.
# Two rules do that work:
#
#   ORDINARY people are short (they start on row 3), plain-headed, and have
#   nothing on their shoulders. They read as "somebody".
#
#   IMPORTANT people are a head taller (row 1), and carry something on the
#   silhouette that an ordinary one never does — a hood, a helmet, a collar.
#   You can tell who is worth talking to across the room.
#
# That is a design rule, not a set of drawings: a new villager copies the
# short body, a new named character copies the tall one and changes the head.

# An ordinary villager. Short, bare-headed, arms down.
SPRITES["folk"] = """
................
................
................
....########....
...##......##...
...#.##..##.#...
...#........#...
...##......##...
....########....
...##########...
..##........##..
..##........##..
..##........##..
...##########...
....##....##....
....##....##....
"""

# A second villager, so a crowd is not one person twice. Same height and
# build, wider shoulders and a bundle on the back.
SPRITES["folk2"] = """
................
................
...##########...
....########....
...##......##...
...#.##..##.#...
...##......##...
....########....
..############..
.##..........##.
.##..........##.
.##..........##.
..############..
...##########...
....##....##....
....##....##....
"""

# THE KEEPER. Important: tall, hooded, long coat to the floor. The oldest
# thing still working in the boot district.
SPRITES["keeper"] = """
.....######.....
...##########...
..####....####..
..###......###..
..##.##..##.##..
..##........##..
..###......###..
..############..
...##########...
..##........##..
..##........##..
..##........##..
..##........##..
..############..
..############..
..############..
"""

# WARDEN COLL. Important: tall, helmeted, shoulder guards, and a bar held
# upright — she is holding a gate and looks like it.
SPRITES["warden"] = """
....########....
...##########...
...##########...
...#........#...
...##########...
....########....
.##############.
.##..........##.
..############..
..##........##..
..##........##..
..##........##..
..############..
...##########...
....##....##....
....##....##....
"""
# HALE. Important — tall and helmeted, the same body as Coll's — so you can
# tell across the room that he is somebody. What makes him not Coll is the
# spear: a line down his right side, held in one hand, that nobody else in
# the game carries. Coll holds the gate; Hale is the one who walks.
SPRITES["guard"] = """
....########..#.
...##########.#.
...#........#.#.
...#.##..##.#.#.
...#.##..##.#.#.
...##########.#.
....########..#.
..###########.#.
..##.......##.#.
..##.......####.
..##.......##.#.
..###########.#.
...##.....##..#.
...##.....##..#.
....##...##...#.
....##...##...#.
"""


# A cache somebody left: a strapped crate, squarer and lower than a tree so
# the two never get confused at a glance.
SPRITES["cache"] = """
................
................
................
....########....
...##########...
..############..
..##.######.##..
..##.######.##..
..############..
..##.######.##..
..##.######.##..
..############..
..############..
...##########...
................
................
"""


# Wren. Small, round-headed and upright, so she reads as a kid beside both
# the hero and the hooded adult: the shortest thing in the game with a face.
SPRITES["kid"] = """
................
................
................
................
................
................
.....######.....
....##....##....
....#.#..#.#....
....#......#....
....##....##....
.....######.....
....########....
...##......##...
....########....
.....##..##.....
"""


# ---- One enemy per area ------------------------------------------------
#
# Each has to be told apart from the prologue three at a glance, on a 1-bit
# panel, at 16x16. Silhouette does that work: legs, round, blocky, tall, wide.

# Sweeper (The Scrapline). FAST. A dome on a skirt of bristles that cleared
# debris off the spans — low and wide, so "this scurries" reads before it
# proves it. To a Sweeper, you are debris.
SPRITES["sweeper"] = """
................
................
.....######.....
...##########...
..###......###..
..##.##..##.##..
..##.##..##.##..
..###......###..
.##############.
################
.##############.
.#.#.#.#.#.#.#.#
.#.#.#.#.#.#.#.#
.#.#.#.#.#.#.#.#
................
................
"""

# Chiller (Cold Storage). ENCRYPTED, shield 3. The insulated unit that kept
# the vaults cold: a closed armoured box, the most solid silhouette there is.
SPRITES["chiller"] = """
................
..############..
.##############.
.##..######..##.
.##.########.##.
.##.##.##.##.##.
.####.####.####.
.##############.
.##############.
.####.####.####.
.##.##.##.##.##.
.##.########.##.
.##..######..##.
.##############.
..############..
................
"""

# Ticket Drone (The Turnstile). AIRBORNE and FAST. Swept back and pointed,
# with nothing under it: it hovers at the barrier and asks for your ticket.
SPRITES["ticket"] = """
.......##.......
......####......
.....######.....
##...######...##
.##.########.##.
..##########.#..
...###..###.....
##..##..##..####
.#####..#####.#.
...##....##.....
....######......
.....####.......
......##........
................
................
................
"""

# Loudhailer (Signal Hill). JAMMER. A mast speaker seen face on — rings round
# a dark driver, on a pole — still playing "please stay home" on a loop.
SPRITES["hailer"] = """
....########....
..###......###..
.##..######..##.
##..##....##..##
##.##..##..##.##
##.##.####.##.##
##.##.####.##.##
##.##..##..##.##
##..##....##..##
.##..######..##.
..###......###..
....########....
.......##.......
.......##.......
.....######.....
....########....
"""

# Shusher (The Deadzone). ENCRYPTED and a JAMMER. Hush's own quiet-maker: a
# round face with a finger held to its lips.
SPRITES["shusher"] = """
....########....
..###......###..
.##..........##.
##..##....##..##
##..##....##..##
##............##
##......##....##
##....######..##
##......##....##
.##.....##...##.
..###...##.###..
....#########...
........##......
.......####.....
.......####.....
................
"""


# The hero, 16x18 and the ONLY player art there is.
#
# There used to be two: a 16x16 battle sprite and a separate 8x12 overworld
# avatar, redrawn at 16x20 when the map zoomed. They were the same character
# described twice and they did not match, which is the one thing a player
# reads instantly. Both scenes now draw this.
#
# 18 rows because that is exactly the battle arena's band: bottom-aligned on
# the floor line it fills 11..29, and in the overworld it stands two pixels
# proud of its 16px tile, which is what makes it read as a character standing
# somewhere rather than as another tile.
#
# The face is in the bitmap. The overworld knocks out the eye band and
# redraws the pupils to show facing — see FT_HERO_EYE_*.
SPRITES["hero"] = """
.......##.......
.......##.......
..############..
.##############.
.##..........##.
.##.##....##.##.
.##.##....##.##.
.##..........##.
.##...####...##.
.##..........##.
.##############.
..############..
...##########...
....##....##....
....##....##....
....##....##....
...####..####...
................
"""

# Parcel Runner. A tied parcel on two wheels, with eyes either side of the
# string: it carried the mail, and now it "returns" anyone it finds
# wandering. Boxy but wheeled, so it is never mistaken for the footed hero.
SPRITES["runner"] = """
.......##.......
.....##..##.....
......####......
.##############.
.##....##....##.
.##.##.##.##.##.
.##.##.##.##.##.
.##....##....##.
.##############.
.##....##....##.
.##....##....##.
.##############.
..##........##..
.####......####.
.####......####.
..##........##..
"""

# Lamplighter. AIRBORNE. A floating lantern with little wings and a dark bulb
# — it lit the night roads, and its Glare is that lamp turned on you.
SPRITES["lamp"] = """
......####......
......#..#......
.....######.....
....########....
##.##########.##
.#.##......##.#.
.#.##.####.##.#.
...##.####.##...
...##.####.##...
...##......##...
...##########...
....########....
......####......
.......##.......
................
................
"""

# Curfew Lock. A padlock with a scowl — tall shackle over a heavy body. It
# locked the doors at night; it still does, and now you are the door.
SPRITES["curfew"] = """
.....######.....
....##....##....
....##....##....
....##....##....
.##############.
.##############.
.##..........##.
.##.##....##.##.
.##.##....##.##.
.##..........##.
.##....##....##.
.##...####...##.
.##....##....##.
.##############.
.##############.
................
"""


# ---- Menu icons --------------------------------------------------------
#
# The pause menu is a grid of these rather than a list of words, because a
# picture is easier to find again than a word in a list. Same rules as the
# characters: a bold silhouette and features at least two pixels wide.

# Pockets: a sack, tied at the neck.
SPRITES["icon_bag"] = """
......####......
.....#....#.....
......####......
.....######.....
....########....
...##......##...
..##........##..
.##..........##.
.##...####...##.
.##..######..##.
.##...####...##.
.##..........##.
.###........###.
..############..
...##########...
................
"""

# Orbs: a ball with a shine on it.
SPRITES["icon_orb"] = """
.....######.....
...##########...
..###......###..
.##..##......##.
.##.##.......##.
##..#.........##
##............##
##............##
##............##
##............##
##............##
.##..........##.
.##..........##.
..###......###..
...##########...
.....######.....
"""

# Quests: a clipboard with a list on it.
SPRITES["icon_quests"] = """
.....######.....
..####....####..
.##..######..##.
.##..........##.
.##.##.#####.##.
.##.##.#####.##.
.##..........##.
.##.##.#####.##.
.##.##.#####.##.
.##..........##.
.##.##.####..##.
.##.##.####..##.
.##..........##.
.##############.
..############..
................
"""

# Field guide: an open book.
SPRITES["icon_guide"] = """
................
................
.######..######.
##....####....##
##.####..####.##
##......##....##
##.####.##.##.##
##......##....##
##.####.##.##.##
##......##....##
##......##....##
##....####....##
.######..######.
................
................
................
"""

# Save: the terminal, the same one you save at in the world.
SPRITES["icon_save"] = """
.##############.
.##############.
.##..........##.
.##.##.......##.
.##.##.......##.
.##..........##.
.##..........##.
.##############.
.##############.
......####......
......####......
....########....
...##########...
................
................
................
"""

# How to play: a question mark.
SPRITES["icon_help"] = """
.....######.....
...##########...
..####....####..
..###......###..
...........###..
.........####...
........####....
.......###......
.......###......
.......###......
................
.......###......
.......###......
................
................
................
"""

# Settings: a gear.
SPRITES["icon_gear"] = """
......####......
..##..####..##..
..############..
...###....###...
..###......###..
####........####
####........####
..##........##..
..##........##..
####........####
####........####
..###......###..
...###....###...
..############..
..##..####..##..
......####......
"""

# Quit to the title: a door, and the way out of it.
SPRITES["icon_quit"] = """
................
.##########.....
.##......##.....
.##......##.....
.##......##..#..
.##......##..##.
.##...##.#######
.##......#######
.##......##..##.
.##......##..#..
.##......##.....
.##......##.....
.##########.....
................
................
................
"""


def parse(art, height=16):
    rows = art.strip("\n").split("\n")
    assert len(rows) == height, f"need {height} rows, got {len(rows)}"
    out = []
    for r in rows:
        r = (r + "." * 16)[:16]
        bits = 0
        for x, ch in enumerate(r):
            if ch == "#":
                bits |= 1 << x  # bit x = column x, LSB leftmost
        out.append(bits)
    return out


# The hero is taller than the foes; everything else is 16x16.
HERO_H = 18


def main():
    data = {k: parse(v, HERO_H if k == "hero" else 16) for k, v in SPRITES.items()}

    # Contact sheet so the art can actually be looked at.
    scale, pad = 6, 8
    sheet = Image.new(
        "L", (len(data) * (16 * scale + pad) + pad, HERO_H * scale + 2 * pad), 200)
    for i, (name, rows) in enumerate(data.items()):
        img = Image.new("L", (16, len(rows)), 255)
        px = img.load()
        for y, bits in enumerate(rows):
            for x in range(16):
                if bits & (1 << x):
                    px[x, y] = 0
        img = img.resize((16 * scale, len(rows) * scale), Image.NEAREST)
        sheet.paste(img, (pad + i * (16 * scale + pad), pad))
    sheet.save("tools/sprites.png")

    with open("src/app/ft_sprites.h", "w") as fh:
        fh.write("""/* Character art, one bit per pixel.
 *
 * Generated by tools/gensprites.py, which holds the sprites as editable ASCII.
 * Change the art there and re-run rather than editing the hex below.
 *
 * Each row is a uint16_t; bit n is column n, counting from the left.
 */
#ifndef FT_SPRITES_H
#define FT_SPRITES_H

#include <stdint.h>

#define FT_SPRITE_W 16
#define FT_SPRITE_H 16

/* The hero is the one piece of art both scenes share, so it is the one that
 * is not 16x16: two rows taller than a foe, which is what a protagonist
 * standing next to one should look like. */
#define FT_HERO_W 16
#define FT_HERO_H %d

/* The screen, in sprite coordinates. The overworld knocks out the eye band
 * and redraws the pupils to show facing; the battle darkens the whole screen
 * while Charge is draining. */
#define FT_HERO_SCREEN_X 3
#define FT_HERO_SCREEN_Y 4
#define FT_HERO_SCREEN_W 10
#define FT_HERO_SCREEN_H 6
#define FT_HERO_EYE_Y    5
#define FT_HERO_EYE_H    2

""" % HERO_H)
        for name, rows in data.items():
            dim = "FT_HERO_H" if name == "hero" else "FT_SPRITE_H"
            fh.write(f"static const uint16_t FT_SPRITE_{name.upper()}[{dim}] = {{\n")
            for r in rows:
                fh.write("    0x%04X,\n" % r)
            fh.write("};\n\n")
        fh.write("#endif /* FT_SPRITES_H */\n")

    print(f"wrote src/app/ft_sprites.h and tools/sprites.png ({len(data)} sprites)")


main()
