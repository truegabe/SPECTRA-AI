# ex_dicts.sp — Dict literal, all map methods, and iteration over keys
# Dicts are SPECTRA's key→value map type.

# --- Literal creation ---
scores = {"alice": 95, "bob": 82, "carol": 91, "dave": 78}

# --- get and has ---
print("alice's score: " + tostr(scores.get("alice")))
print("has 'bob':     " + tostr(scores.has("bob")))
print("has 'eve':     " + tostr(scores.has("eve")))

# --- set (add or update) ---
scores.set("eve", 88)
print("after adding eve: " + tostr(scores.len()) + " entries")

scores.set("bob", 90)           # update existing key
print("bob updated to: " + tostr(scores.get("bob")))

# --- delete ---
scores.delete("dave")
print("after deleting dave, len = " + tostr(scores.len()))

# --- keys and values ---
keys = scores.keys()
vals = scores.values()
print("keys:   " + tostr(keys))
print("values: " + tostr(vals))

# --- Iterating over keys ---
print("all scores:")
for k in keys:
    v = scores.get(k)
    print(f"  {k} → {tostr(v)}")

# --- Computing the average via values list ---
total = sum(vals)
avg   = total / vals.len()
print("average score: " + tostr(avg))

# --- empty dict ---
config = dict()
config.set("debug", 1)
config.set("threads", 4)
print("config debug:   " + tostr(config.get("debug")))
print("config threads: " + tostr(config.get("threads")))
