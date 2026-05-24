# ex_control_flow.sp — if/elif/else, while, for, break, continue
# Every control-flow construct available in SPECTRA.

# --- if / elif / else ---
temp = 72

if temp > 90:
    print("hot day")
elif temp > 70:
    print("warm day")
elif temp > 50:
    print("cool day")
else:
    print("cold day")

# --- while loop ---
print("counting up:")
i = 1
while i <= 5:
    print("  " + tostr(i))
    i = i + 1

# --- while with break ---
print("search with break:")
target = 7
n = 0
while true:
    n = n + 1
    if n == target:
        print("  found " + tostr(target))
        break

# --- for with range(stop) ---
print("squares 0-4:")
for i in range(5):
    print("  " + tostr(i) + "^2 = " + tostr(i * i))

# --- for with range(start, stop, step) ---
print("even numbers 2-10:")
for i in range(2, 11, 2):
    print("  " + tostr(i))

# --- for with list ---
fruits = list("apple", "banana", "cherry", "date", "elderberry")
print("fruit list:")
for fruit in fruits:
    print("  " + fruit)

# --- continue: skip short words ---
print("long fruits only (>5 chars):")
for fruit in fruits:
    if fruit.len() <= 5:
        continue
    print("  " + fruit)

# --- nested loops with break ---
print("first pair summing to 10:")
outer = list(1, 2, 3, 4, 5)
inner = list(9, 8, 7, 6, 5)
found = false
for a in outer:
    for b in inner:
        if a + b == 10:
            print(f"  {tostr(a)} + {tostr(b)} = 10")
            found = true
            break
    if found:
        break
