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

# Blank Wall (BULWARK). A slab. It has to read as an obstacle rather than a
# creature, so: no face, no legs, and the widest silhouette in the game.
SPRITES["wall"] = """
################
#..............#
#.############.#
#.#..........#.#
#.#.########.#.#
#.#.#......#.#.#
#.#.#.####.#.#.#
#.#.#.#..#.#.#.#
#.#.#.#..#.#.#.#
#.#.#.####.#.#.#
#.#.#......#.#.#
#.#.########.#.#
#.#..........#.#
#.############.#
#..............#
################
"""

# Cold Booter (SLEEPER). Shut down: a closed shell with a dormant indicator.
# Rounded and low, so it looks like something switched off rather than
# something waiting.
SPRITES["booter"] = """
................
................
....########....
..############..
.##############.
##..##....##..##
#...##....##...#
#..............#
#..............#
#....######....#
#...##....##...#
.##############.
..############..
....########....
................
................
"""


# ---- One enemy per area ------------------------------------------------
#
# Each has to be told apart from the prologue three at a glance, on a 1-bit
# panel, at 16x16. Silhouette does that work: legs, round, blocky, tall, wide.

# Scrap Crawler (The Scrapline). FAST. Low and many-legged, so "this thing
# moves first" is readable before it proves it.
SPRITES["crawler"] = """
................
................
..#..######..#..
..##########.#..
.#.##########.#.
.###.##..##.###.
.############.#.
#.############.#
.##############.
.#.##########.#.
..############..
.#.#.#.##.#.#.#.
#..#.#.##.#.#..#
..#...#..#...#..
.#...#....#...#.
................
"""

# Rime Shell (Cold Storage). ENCRYPTED, shield 3. A closed armoured box with
# a seam down it — deliberately the most solid silhouette in the game.
SPRITES["rime"] = """
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

# Gate Drone (The Turnstile). AIRBORNE and FAST. Swept back and pointed, with
# nothing under it: it reads as hovering and as going somewhere.
SPRITES["drone"] = """
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

# Mast Relay (Signal Hill). JAMMER. Tall and thin with a wide base and rings
# coming off the top — a transmitter, which is what is eating your meter.
SPRITES["relay"] = """
#..............#
.#....####....#.
..#..######..#..
...#.##..##.#...
......####......
.......##.......
......####......
.....##..##.....
.....##..##.....
.....##..##.....
....########....
...##########...
..############..
.####......####.
####........####
................
"""

# Null Field (The Deadzone). ENCRYPTED and a JAMMER. A hollow ring with
# nothing in the middle: the one silhouette here that is mostly absence.
SPRITES["nullf"] = """
....########....
..##........##..
.#............#.
##..........#.##
#...........#..#
#....#..#...#..#
#...#....#..#..#
#..#......#....#
#..#......#....#
#...#....#.....#
#....#..#......#
##.............#
.#............#.
..##........##..
....########....
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

# Stray Packet: a spiked ball. Deliberately round and legless so it cannot be
# mistaken for the boxy, footed player.
SPRITES["packet"] = """
................
....#..##..#....
....##.##.##....
...##########...
..############..
.####......####.
.###.##..##.###.
.###.##..##.###.
.###........###.
.###.######.###.
..############..
...##########...
....##.##.##....
....#..##..#....
................
................
"""

# Drift Beacon: an airborne eye. Round body, solid pupil, swept wings and a
# pulse antenna, so AIRBORNE reads instantly.
SPRITES["beacon"] = """
.......##.......
.......##.......
.....######.....
....########....
...##########...
##.####..####.##
##.###....###.##
##.###....###.##
##.####..####.##
...##########...
....########....
.....######.....
.......##.......
................
................
................
"""

# Sealed Lock: a padlock with a scowl. Tall shackle above a heavy body.
SPRITES["lock"] = """
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
