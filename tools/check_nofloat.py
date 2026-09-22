"""Refuse floating point in src/core.

The MCU has no FPU budget, so the core stays integer (DESIGN.md 2.2). This
used to be a grep for the words `float` and `double`, which matched prose:
three separate comments were reworded to appease it ("a double press", "the
HAL takes a float"), which is the check bullying the documentation rather
than the other way round.

So: strip comments and string literals first, then look for the types as
whole words. What is left is code.
"""
import re
import sys
from pathlib import Path

CORE = Path("src/core")
BAD = re.compile(r"\b(float|double)\b")


def strip(src):
    """Blank out comments and string literals, keeping newlines so line
    numbers still line up with the file on disk."""
    out = []
    i, n = 0, len(src)

    while i < n:
        two = src[i:i + 2]

        if two == "/*":
            end = src.find("*/", i + 2)
            end = n if end < 0 else end + 2
            out.append("\n" * src.count("\n", i, end))
            i = end
        elif two == "//":
            end = src.find("\n", i)
            end = n if end < 0 else end
            i = end
        elif src[i] in "\"'":
            quote = src[i]
            j = i + 1
            while j < n and src[j] != quote:
                j += 2 if src[j] == "\\" else 1
            out.append(" ")
            i = min(j + 1, n)
        else:
            out.append(src[i])
            i += 1

    return "".join(out)


bad = []
for path in sorted(list(CORE.glob("*.c")) + list(CORE.glob("*.h"))):
    for no, line in enumerate(strip(path.read_text()).splitlines(), 1):
        if BAD.search(line):
            bad.append("%s:%d: %s" % (path, no, line.strip()))

if bad:
    for line in bad:
        print(" ", line)
    sys.exit("ERROR: floating point found in src/core (see DESIGN.md 2.2)")

print("no floating point in src/core")
