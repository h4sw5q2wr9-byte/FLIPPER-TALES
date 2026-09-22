"""Author the 8x8 overworld tiles as ASCII, preview them, and emit a C header.

Same approach as gensprites.py: the art is editable text, not hand-written hex.
"""
from PIL import Image, ImageDraw

TILES = {}

# Walkable ground. Sparse stipple so large areas read as floor without
# vibrating — a dense pattern is unbearable at this size.
TILES["floor"] = """
........
..#.....
........
.....#..
........
..#.....
........
.....#..
"""

# Wall FACE: the south-facing side, used where floor lies below. Brick courses
# with a solid base row so the wall looks like it stands on the ground.
TILES["wall"] = """
########
#..#..##
########
#..##..#
########
##..#..#
########
########
"""

# Wall TOP: the cap, used where the wall continues below. Near-solid so it
# reads as mass rather than as another face — the contrast between this and
# the brick face is what gives a wall run its height.
TILES["wall_top"] = """
########
########
###.####
########
########
####.###
########
########
"""

# Shadow cast onto whatever tile sits under a wall. 50% dither, because on a
# one-bit panel a solid band would just look like more wall.
TILES["shadow"] = """
#.#.#.#.
.#.#.#.#
........
........
........
........
........
........
"""

# Void / drop. Solid black: nothing there, and unmistakably not floor.
TILES["void"] = """
########
########
########
########
########
########
########
########
"""

# Tall grass. Walkable, hides things, rustles.
TILES["grass"] = """
...#....
..#.#...
........
........
.#....#.
#.#..#.#
........
........
"""

# Cable conduit: decorative floor that reads as "this place is machinery".
TILES["cable"] = """
........
########
.#....#.
........
........
.#....#.
########
........
"""

# Door seen face-on. Correct only in a HORIZONTAL wall, where you walk up or
# down through it. Heavy frame, clear opening, handle on the right.
TILES["door"] = """
########
##....##
#......#
#......#
#......#
#......#
#......#
#......#
"""

# The same door in a VERTICAL wall, where you pass left-to-right. Drawn as a
# gap with the wall continuing above and below — a face-on door here reads as
# if it were lying on its back.
TILES["door_side"] = """
########
#......#
........
........
........
........
#......#
########
"""

# A GATE somebody is holding, in a horizontal wall. Heavier than a door and
# crossed by two bars, so a way that is shut against you never looks like an
# ordinary doorway you have not tried yet.
TILES["gate"] = """
########
##....##
#.#..#.#
########
#.#..#.#
#.#..#.#
########
#.#..#.#
"""

# The same gate in a vertical wall, where you pass left-to-right.
TILES["gate_side"] = """
########
#.####.#
..#..#..
..#..#..
..#..#..
..#..#..
#.####.#
########
"""

# ---- Things with a top and a bottom ------------------------------------
#
# A trunk you bump into, a canopy you walk behind. The canopy is deliberately
# gappy: the blit only sets black pixels, so whoever is under it shows
# through the leaves instead of vanishing behind them.
#
# The trunk is the BOTTOM middle of the 3x3 block, not its centre. A trunk in
# the centre is a black slab with the crown cut off round it — it was tried,
# and it read as a barrel. Down here the bark meets the ground on one side and
# the leaves on the other, which is what a tree looks like.

TILES["trunk"] = """
.######.
.#.##.#.
.######.
.#.##.#.
.######.
.#.##.#.
.######.
.######.
"""

# An even checkerboard: on a one-bit panel that is mid grey, which is the only
# fill that is both solid enough to read as a mass of leaves from across the
# room and open enough that whoever walks under it shows through as a ghost
# rather than vanishing. Two denser patterns were tried first — a random
# scatter, which read as noise over the floor stipple, and a three-quarter
# grid, which swallowed the player. The canopy's SHAPE does the work instead:
# see the rounded corners further down.
TILES["leaf"] = """
#.#.#.#.
.#.#.#.#
#.#.#.#.
.#.#.#.#
#.#.#.#.
.#.#.#.#
#.#.#.#.
.#.#.#.#
"""

# A wall of a building, and the roof over it.
#
# Three textures meet at a house — wall, roof and the canopy of whatever grows
# beside it — and at 8x8 there is no room for detail to tell them apart. So
# they are told apart by DIRECTION instead: the wall is vertical boards, the
# roof is horizontal courses, and the canopy is an even checker. That reads at
# a glance and survives being one bit deep.
TILES["hut"] = """
.##..##.
.##..##.
.##..##.
.##..##.
.##..##.
.##..##.
.##..##.
########
"""

# Shingles: unbroken horizontal courses, with one joint punched through each
# gap so the roof reads as laid tiles rather than as corrugation.
TILES["roof"] = """
########
...#....
########
.......#
########
...#....
########
.......#
"""

# A door in a hut wall: a dark slab, with a handle, set into the light boards.
# Solid, because it is scenery and not an exit — but a village whose houses
# have no doors in them reads as a stack of crates with roofs on.
TILES["hut_door"] = """
.#..#..#
.######.
.######.
.######.
.####.#.
.######.
.######.
########
"""

# Locked port in a vertical wall: the same gap, barred.
TILES["lock_side"] = """
########
#......#
.##..##.
.##..##.
.##..##.
.##..##.
#......#
########
"""

# A single weed, scattered procedurally over floor (see ft_map_scatter).
# Kept inset from the tile edges so neighbouring tufts never merge into a
# hedge, and kept light so it does not fight the floor stipple.
TILES["tuft"] = """
........
........
...#....
..###...
...#....
........
........
........
"""

# Conduit running vertically, for cable runs that go up the screen.
TILES["cable_v"] = """
.#....#.
.#....#.
.#....#.
.#....#.
.#....#.
.#....#.
.#....#.
.#....#.
"""

# Terminal: save point and full restore. Deliberately a heavy, mostly-solid
# monitor on a narrow post — an outlined box here is indistinguishable from a
# crate at 8x8.
TILES["term"] = """
.######.
.######.
.#....#.
.#.##.#.
.######.
...##...
..####..
........
"""

# Ladder: walkable, and the way between terraces. Top-down has no elevation,
# so a terrace edge is drawn as wall and the ladder is the gap through it.
TILES["ladder"] = """
.#....#.
.######.
.#....#.
.######.
.#....#.
.######.
.#....#.
.######.
"""

# Locked port. Opened by the iButton module, once it is recovered.
TILES["lock"] = """
..####..
.##..##.
.#....#.
########
##....##
##.##.##
##....##
########
"""

# Crate: pushable, or just cover. X-braced so its silhouette is diagonal
# rather than another concentric square.
TILES["crate"] = """
########
##....##
#.#..#.#
#..##..#
#..##..#
#.#..#.#
##....##
########
"""


# ---- Area flavour ------------------------------------------------------
#
# One ground or obstacle per chapter. An area that reuses the prologue's
# corridor tiles is not a place, it is the same corridor with a different
# name over the door.

# The Scrapline: heaped wreckage. Solid. Jagged on purpose — the silhouette
# has to differ from the crate's neat X-brace at a glance.
TILES["scrap"] = """
..##....
.####.#.
##.####.
#.#####.
#####.##
.##.####
####.###
.#.####.
"""

# Cold Storage: rimed floor. Walkable. A sparse crystal lattice, lighter than
# the wall courses so a frosted room still reads as open ground.
TILES["frost"] = """
...#....
..###...
...#..#.
......#.
.#......
#.#.....
.#...#..
.....#..
"""

# Signal Hill: the base of a mast. Solid. Vertical, so a row of them reads as
# a line of pylons rather than as fencing.
TILES["pylon"] = """
...##...
...##...
..####..
.#.##.#.
#..##..#
...##...
..####..
.######.
"""

# The Deadzone: floor under interference. Walkable. Broken scanlines — the
# ground is still there, something is just sitting on top of it.
TILES["static"] = """
#.#..#..
........
..#...#.
........
.#..#..#
........
#...##..
........
"""


def parse(art):
    rows = [r for r in art.strip("\n").split("\n") if r.strip() or True]
    rows = [r for r in art.strip("\n").split("\n")]
    assert len(rows) == 8, f"need 8 rows, got {len(rows)}"
    out = []
    for r in rows:
        r = (r + "." * 8)[:8]
        bits = 0
        for x, ch in enumerate(r):
            if ch == "#":
                bits |= 1 << x
        out.append(bits)
    return out


data = {k: parse(v) for k, v in TILES.items()}


# ---- Canopy corners ----------------------------------------------------
#
# A tree is stamped as a 3x3 block (trunk in the middle, leaves all round),
# and a 3x3 block of one leaf tile is a square. Real canopies are not square,
# and on a 128x64 panel a dark rectangle in a field reads as a building.
#
# So the four outer corners get rounded copies of the leaf, cut with a
# quarter circle. The renderer picks them from the neighbours the same way it
# picks a wall cap or a side-on door — see ft_map_art_index — so maps stay one
# byte per tile and authors keep stamping plain leaves.
def _round_corner(rows, cx, cy, r=8.0):
    out = []
    for y, bits in enumerate(rows):
        keep = 0
        for x in range(8):
            if not (bits & (1 << x)):
                continue
            dx, dy = (x + 0.5) - cx, (y + 0.5) - cy
            if dx * dx + dy * dy <= r * r:
                keep |= 1 << x
        out.append(keep)
    return out


# The centre of the quarter circle sits at the INNER corner of the tile, so
# what survives is the side that faces the rest of the canopy.
for _suffix, _cx, _cy in (("tl", 8.0, 8.0), ("tr", 0.0, 8.0),
                          ("bl", 8.0, 0.0), ("br", 0.0, 0.0)):
    data["leaf_" + _suffix] = _round_corner(data["leaf"], _cx, _cy)

scale, pad = 8, 6
sheet = Image.new("L", (len(data) * (8 * scale + pad) + pad, 8 * scale + 2 * pad + 10), 200)
draw = ImageDraw.Draw(sheet)
for i, (name, rows) in enumerate(data.items()):
    img = Image.new("L", (8, 8), 255)
    px = img.load()
    for y, bits in enumerate(rows):
        for x in range(8):
            if bits & (1 << x):
                px[x, y] = 0
    ox = pad + i * (8 * scale + pad)
    draw.text((ox, 0), name, fill=0)
    sheet.paste(img.resize((8 * scale, 8 * scale), Image.NEAREST), (ox, 10 + pad // 2))
sheet.save("tools/tiles.png")

with open("src/app/ft_tiles.h", "w") as fh:
    fh.write("""/* Overworld tile art, 8x8, one bit per pixel.
 *
 * Generated by tools/gentiles.py, which holds the tiles as editable ASCII.
 * Change the art there and re-run rather than editing the hex below.
 *
 * Each row is a uint8_t; bit n is column n, counting from the left.
 */
#ifndef FT_TILES_H
#define FT_TILES_H

#include <stdint.h>

#include "../core/ft_map.h"

static const uint8_t FT_TILE_ART[FT_TILE_ART_COUNT][FT_TILE_PX] = {
""")
    # Must match FtTile, then the orientation variants (FT_TILE_ART_*).
    # Must match FtTile exactly, then the orientation variants (FT_TILE_ART_*).
    order = ["floor", "wall", "void", "grass", "cable", "door", "term", "lock",
             "crate", "ladder", "scrap", "frost", "pylon", "static", "gate",
             "trunk", "leaf", "hut", "roof", "hut_door",
             "door_side", "lock_side", "cable_v", "wall_top", "shadow", "tuft",
             "gate_side", "leaf_tl", "leaf_tr", "leaf_bl", "leaf_br"]
    for name in order:
        fh.write("    /* %-6s */ {%s},\n" % (name, ", ".join("0x%02X" % b for b in data[name])))
    fh.write("};\n\n#endif /* FT_TILES_H */\n")

print("wrote src/app/ft_tiles.h and tools/tiles.png (%d tiles)" % len(data))
