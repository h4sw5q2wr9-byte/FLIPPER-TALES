"""Author overworld maps as ASCII and emit them as C tile data.

Maps are drawn here, not in hex. Production maps will stream from the SD card
in this same byte-per-tile format, so nothing about the renderer changes when
they do.

Grass is '*' rather than a quote character: these maps live inside a Python
triple-quoted string.
"""

LEGEND = {
    ".": 0,  # floor
    "#": 1,  # wall
    "~": 2,  # void
    "*": 3,  # grass
    "=": 4,  # cable
    "D": 5,  # door
    "T": 6,  # terminal
    "L": 7,  # locked port
    "C": 8,  # crate
    "H": 9,  # ladder
    "S": 10, # scrap    (The Scrapline)
    "f": 11, # frost    (Cold Storage)
    "Y": 12, # pylon    (Signal Hill)
    "s": 13, # static   (The Deadzone)
    "G": 14, # gate     (a way somebody is holding shut)
    "t": 15, # trunk    (lowercase: "T" is already the terminal)
    "l": 16, # leaf     (canopy, walkable, drawn in front)
    "h": 17, # hut      (a building's wall)
    "r": 18, # roof     (walkable, drawn in front)
    "d": 19, # hut door (scenery, solid: nothing here goes indoors)
}

MAPS = {}

# An AREA is a numbered chain of ROOMS, each about a screen, walked left to
# right — see RESEARCH.md. A room is a small authored set-piece: an encounter,
# maybe one obstacle, and something hidden in a corner. It is not a space to
# explore, so rooms are deliberately close to viewport-sized rather than the
# 2.5-screen open fields the first sketch used.

# [1] Wake. No foe, no obstacle: a terminal to teach saving and one exit.
#
# The Carrier has been dead long enough that things grow in it, which is the
# one piece of the premise that has to land before anything else: the tree and
# the grass bed are in the first room you ever see, lit by the hole they came
# in through. Everything mechanical is on the left, everything alive on the
# right, and the door is past both.
MAPS["cb1"] = ("Cold Boot", 0, """
##################
#................#
#..T.............#
#.......C........#
#................D
#....***lll***...#
#....***lll***...#
#....***ltl***...#
#................#
##################
""")

# [2] First encounter. Wider than a screen so the foe is seen before it is
# reached, which is the whole point of visible encounters.
#
# The conduit run is the room's spine: it comes down the left wall and turns
# along the top, so the corridor has a direction even where the floor is
# empty. The one thing standing in the middle of it is the thing you fight.
MAPS["cb2"] = ("Boot Corridor", 8, """
####################
#....==============#
D....=.............#
#..C.=....***......#
#....=....***......D
#....=....***......#
#..................#
####################
""")

# [3] Vertical section. A terrace splits the room and the ladder is the only
# way down; the foe on the upper shelf is passed before it can be avoided.
#
# The terminal is on the LOWER floor, past the drop. Save points sit after the
# commitment, never before it.
MAPS["cb3"] = ("The Drop", 8, """
##################
#................#
D....C...........#
#................#
#.......***......#
#####H############
#................#
#......lll....T..#
#......lll.......#
#......ltl.......#
#...***..........D
#...***..........#
#................#
##################
""")

# [4] The gate. A sealed side room behind a locked port, which cannot be opened
# until the iButton several chapters later — the reason to come back.
#
# The chamber is drawn as a room with a barred way in rather than as a blank
# patch of wall, so what you are being refused is legible from outside it.
MAPS["cb4"] = ("Cold Gate", 6, """
##################
#................#
D................#
#.....############
#.....#..........#
#.....L.....C....#
#.....#..........#
#.....############
#................D
##################
""")


# ---- Chapter 1: Weldhome -----------------------------------------------
#
# The first place in this game with people in it. See STORY.md 6.
#
# The shape is the point: a fork you walk past, a gate that will not open,
# and the fork again — now with a reason. Nothing here is locked with a key.

# ---- Chapter 1: Weldhome -----------------------------------------------
#
# The first place in this game with people in it. See STORY.md 6.
#
# The shape is the point: a fork you walk past, a gate that will not open,
# and the fork again — now with a reason. Nothing here is locked with a key.

# [10] The Approach. Outside, and drawn the way outside is drawn: a field of
# grass with one worn path cut through it. The path is the room's instruction
# — it runs west to east, door to door, and everything you might stop for is
# a step off it.
#
# The middle tree stands ON the path with its canopy across it, so the first
# thing you do out here is walk under leaves and watch yourself show through
# them.
#
# The drop south is a spur off the same path ending in a ladder. It used to be
# a shaft framed by two columns of wall, which was corridor logic applied
# outdoors: a walled passage standing up out of a meadow, marking a route
# nobody needed marking. Out here a track worn through the grass says the same
# thing and belongs to the place it is in.
#
# Neither path edge runs straight for long, and weeds come up through the
# swept ground (ft_map_scatter). A road ruled across a field with grass butted
# hard against it does not MEET the grass, it cuts it off; a ragged edge with
# something growing through it is the join.
MAPS["ap1"] = ("The Approach", 20, """
########################
#**********************#
#*lll*************lll**#
#*lll**lll********lll**#
#*ltl**lll***...**ltl**#
D......ltl.............D
#*...******.***...*****#
#*lll******.******lll**#
#*lll*****...*****lll**#
#*ltl*****.H.*****ltl**#
###########D############
""")

# [11] Weldhome Gate. A junction village, laid out the way a village is: the
# road comes in the west gate and goes out the east one, the houses face the
# road across a strip of swept ground, and the square with the well of this
# world — a terminal — is off to the side of it.
#
# Two houses, each with its door onto the road, because a building with no
# door in it reads as a crate with a roof on. The way through is a GATE rather
# than a doorway, so a route held shut against you never looks like one you
# simply have not tried.
#
# The swept ground is deliberately ragged: wide aprons in front of the doors,
# a rounded square off to one side, and the road between them. A straight
# one-tile road with grass butted against it reads as a stripe painted on a
# field rather than as ground people have walked flat.
MAPS["wh1"] = ("Weldhome Gate", 20, """
########################
#**rrrr******rrrr******#
#**rrrr******rrrr******#
#**hhdh******hhdh******#
#**.....*****.....*****#
D......................G
#**lll***.......***lll*#
#**lll***...T...***lll*#
#**ltl****.....****ltl*#
#**********************#
########################
""")

# [12] East Junction. Down, dark, and full of the things that took Wren.
#
# Below ground, so no grass and no sky: bare deck, heaped wreckage and a
# bulkhead straight down the middle with a single gap punched in the bottom of
# it. That gap is why the thing at the far end is a wall and not a crowd —
# there is no route round it, so the fight IS the room. Everything on the way
# to the gap (the heaps, the cache, the one tree still alive down here) is
# passed on a route you have no choice about.
MAPS["ej1"] = ("East Junction", 10, """
#####D##################
#............#.........#
#..lll.......#....SS...#
#..lll.......#...SSSS..#
#..ltl....SS.#....SS...#
#.........SS.#.........#
#..SS........#...lll...#
#.SSSS.......#...lll...#
#..SS............ltl...#
#............#.........#
########################
""")


# ---- Concept slices ----------------------------------------------------
#
# One room from each of the five chapters, so the areas exist as places
# rather than as a table in the design doc. They are deliberately a single
# room each: enough to establish a silhouette, a hazard and the thing the
# chapter's module is for, and not a chapter.
#
# Each carries the locked port that gates the area after it, which is the
# whole progression made walkable: you can see every door you cannot open
# yet, and what would open it.

# [5] The Scrapline. Heaped wreckage and a gap in the floor. Infrared is
# line-of-sight, so the chapter's verb is triggering a receiver across a span
# you cannot walk — here the void, with the terminal stranded past it.
MAPS["sl1"] = ("The Scrapline", 24, """
######################
#...............######
D......SS....~~~~..T.#
#.....SSSS...~~~~....#
#...S..SS....~~~~....#
#.........S..~~~~....#
#..S.........~~~~....#
#....SS......L########
#....................D
######################
""")

# [6] Cold Storage. Rimed floor and racks. RFID reads through walls, so the
# chapter's verb is finding the door that is not drawn — the sealed cell in
# the middle has no visible way in.
MAPS["cs1"] = ("Cold Storage", 10, """
######################
#ffffffffffffffffffff#
Dffff#########fffffff#
#ffff#.......#ffffCff#
#ffff#...T...#fffffff#
#ffff#.......#fffLfff#
#ffff####D####fffffff#
#fffffffffffffffffCff#
#ffffffffffffffffffffD
######################
""")

# [7] The Turnstile. Ranks of locked ports across the only route. iButton
# opens them; until then this is the wall the whole chapter is named after,
# and you can walk up to it and read it.
MAPS["ts1"] = ("The Turnstile", 12, """
######################
#........#L#.........#
D....C...#.#....C....#
#........L.L.........#
#........#.#.........#
#........#L#....C....#
#....C...#.#.........#
#...T....###.........#
#....................D
######################
""")

# [8] Signal Hill. Pylons and dead cable runs climbing a terrace. GPIO powers
# things, so the chapter's verb is making the dead lift move — the ladder is
# there, the run to it is not.
MAPS["sh1"] = ("Signal Hill", 28, """
######################
#..*....Y......*.....#
D...*...Y...*........#
#=============....*..#
######H###############
#....*......Y........#
#..*.......=Y=.....T.#
#.......L...Y........#
#...*................D
######################
""")

# [9] The Deadzone. Interference on the ground and nothing that reads
# straight. BLE pairs with devices and moves them, so the chapter's verb is
# making the crates in the way carry you through.
MAPS["dz1"] = ("The Deadzone", 16, """
######################
#ssssssssssssssssssss#
Dss~~ssssCsss~~~sssss#
#ss~~sssssssss~~~ssss#
#sssssCsss~~~ssssCsss#
#sssssssss~~~ssssssss#
#sL~~ssssssssssss~~ss#
#ss~~sssTsssssssss~~s#
#ssssssssssssssssssssD
######################
""")


SOLID = {1, 2, 8, 10, 12, 15, 17, 19}  # wall, void, crate, scrap, pylon, trunk, hut, hut door
DOOR = 5
GATE = 14  # a way out, like a door, and allowed on a border


def parse(art):
    rows = art.strip("\n").split("\n")
    w = max(len(r) for r in rows)
    out = []
    for r in rows:
        r = r.ljust(w, "#")
        out.append([LEGEND[c] for c in r])
    return w, len(rows), out


def check_border(key, w, h, rows):
    """A room must be sealed except at its doors.

    A row that is the full width but ends in floor leaks the player straight
    off the edge of the map, and it is invisible when you are counting
    characters by eye. Caught here rather than on hardware."""
    bad = []
    for y in range(h):
        for x in range(w):
            if 0 < x < w - 1 and 0 < y < h - 1:
                continue
            t = rows[y][x]
            # A gate is a way out like a door is: it may sit on the border.
            if t in SOLID or t == DOOR or t == GATE:
                continue
            bad.append((x, y, t))

    if bad:
        for x, y, t in bad[:8]:
            print("  %s: open border at (%u,%u), tile %u" % (key, x, y, t))
        raise SystemExit("%s is not sealed (%d open border tiles)" % (key, len(bad)))


with open("src/app/ft_maps.h", "w") as fh:
    fh.write("""/* Built-in overworld maps.
 *
 * Generated by tools/genmaps.py, which holds them as editable ASCII art.
 * Edit the maps there and re-run; do not hand-edit the tables below.
 *
 * Production maps will stream from the SD card in this same byte-per-tile
 * format, so the renderer does not change when they do.
 */
#ifndef FT_MAPS_H
#define FT_MAPS_H

#include "../core/ft_map.h"

""")
    for key, (name, scatter, art) in MAPS.items():
        w, h, rows = parse(art)
        check_border(key, w, h, rows)
        fh.write("/* %s: %ux%u tiles (%ux%u px) */\n" % (name, w, h, w * 8, h * 8))
        fh.write("static const uint8_t FT_MAP_%s_TILES[%u] = {\n" % (key.upper(), w * h))
        for r in rows:
            fh.write("    " + ",".join(str(v) for v in r) + ",\n")
        fh.write("};\n\n")
        fh.write(
            'static const FtMap FT_MAP_%s = {FT_MAP_%s_TILES, %u, %u, "%s", %u};\n\n'
            % (key.upper(), key.upper(), w, h, name, scatter))
        print("%-8s %-16s %2ux%-2u tiles  %.2f x %.2f screens"
              % (key, name, w, h, w / 16, h / 8))

    fh.write("#endif /* FT_MAPS_H */\n")

print("wrote src/app/ft_maps.h")
