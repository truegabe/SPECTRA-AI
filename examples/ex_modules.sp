# ex_modules.sp — import math, random, io modules
# Shows every import style and the key functions from each module.

import math
import random
import io

# ─── math module ──────────────────────────────────────────────────────
print("--- math module ---")
print("pi()      = " + tostr(pi()))
print("e()       = " + tostr(e()))
print("sqrt(2)   = " + tostr(sqrt(2.0)))
print("log(e())  = " + tostr(log(e())))
print("abs(-7)   = " + tostr(abs(-7)))
print("floor(3.9)= " + tostr(floor(3.9)))
print("ceil(3.1) = " + tostr(ceil(3.1)))
print("pow(2,10) = " + tostr(pow(2.0, 10.0)))

# ─── selective import (from math import) ──────────────────────────────
print("")
print("--- selective import: sin / cos ---")
angle = pi() / 6.0     # 30 degrees
print("sin(pi/6) = " + tostr(sin(angle)))
print("cos(pi/6) = " + tostr(cos(angle)))
print("sin^2 + cos^2 = " + tostr(sin(angle) ** 2.0 + cos(angle) ** 2.0))

# ─── Trig table via loop ───────────────────────────────────────────────
print("")
print("--- trig table (0, 30, 60, 90 degrees) ---")
deg_list = list(0, 30, 60, 90)
for deg in deg_list:
    rad = tofloat(deg) * pi() / 180.0
    print(f"  sin({tostr(deg)} deg) = {tostr(sin(rad))}")

# ─── random module ────────────────────────────────────────────────────
print("")
print("--- random module ---")
n1 = randint(1, 10)
n2 = randint(1, 10)
print("randint(1,10): " + tostr(n1) + ", " + tostr(n2))
n3 = random()
print("random() in [0,1): " + tostr(n3))

# ─── io module (flat namespace after import io) ────────────────────────
print("")
print("--- io module ---")

filename = "tmp_test.txt"
write(filename, "line one\nline two\nline three\n")
print("file exists after write: " + tostr(exists(filename)))

content = read(filename)
print("full content length: " + tostr(content.len()) + " chars")

file_lines = lines(filename)
print("line count: " + tostr(file_lines.len()))
for ln in file_lines:
    print("  > " + ln)

append(filename, "line four\n")
lines2 = lines(filename)
print("after append, line count: " + tostr(lines2.len()))
