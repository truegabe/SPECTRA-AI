# ex_floor_div.sp — // floor division, in/not in on strings, lists, dicts
# Covers three operators that differ from plain Python.

# ─── Floor division  (//) ───────────────────────────────────────────
print("--- floor division ---")

a = 17
b = 5
print(f"{tostr(a)} / {tostr(b)}  = {tostr(a / b)}")    # 3.4
print(f"{tostr(a)} // {tostr(b)} = {tostr(a // b)}")   # 3  (truncates toward -∞)

neg = -17
print(f"{tostr(neg)} // {tostr(b)} = {tostr(neg // b)}")  # -4  (floor, not truncate)

# Practical: integer page calculation
items_per_page = 8
total_items    = 53
full_pages     = total_items // items_per_page
leftover       = total_items % items_per_page
print(f"{tostr(total_items)} items → {tostr(full_pages)} full pages + {tostr(leftover)} leftover")

# ─── in / not in on strings ────────────────────────────────────────
print("")
print("--- in / not in on strings ---")

haystack = "the quick brown fox"
print("'fox' in haystack:    " + tostr("fox" in haystack))
print("'cat' in haystack:    " + tostr("cat" in haystack))
print("'cat' not in haystack:" + tostr("cat" not in haystack))

# ─── in / not in on lists ──────────────────────────────────────────
print("")
print("--- in / not in on lists ---")

primes = list(2, 3, 5, 7, 11, 13)
print("7 in primes:    " + tostr(7 in primes))
print("9 in primes:    " + tostr(9 in primes))
print("9 not in primes:" + tostr(9 not in primes))

# Filter even numbers using not in a primes guard
candidates = list(2, 4, 6, 7, 8, 9, 10, 11, 12, 13)
non_prime  = list()
for n in candidates:
    if n not in primes:
        non_prime.append(n)
print("non-prime candidates: " + tostr(non_prime))

# ─── in / not in on dicts (checks keys) ───────────────────────────
print("")
print("--- in / not in on dicts ---")

config = {"host": "localhost", "port": 8080, "debug": true}
keys   = config.keys()

print("'host' in keys:    " + tostr("host" in keys))
print("'timeout' in keys: " + tostr("timeout" in keys))

required = list("host", "port", "auth")
for key in required:
    if key not in keys:
        print(f"  missing required key: {key}")
    else:
        print(f"  key '{key}' present")
