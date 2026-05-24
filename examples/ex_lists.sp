# ex_lists.sp — List creation, all methods, and iteration
# Lists are SPECTRA's dynamic array type.

# --- Creation ---
empty = list()
nums  = list(10, 20, 30, 40, 50)
print("nums: " + tostr(nums))
print("len:  " + tostr(nums.len()))

# --- Append and pop ---
nums.append(60)
print("after append(60): " + tostr(nums))
last = nums.pop()
print("popped: " + tostr(last) + " → list: " + tostr(nums))

# --- Insert and remove ---
nums.insert(0, 5)          # prepend 5
print("after insert(0,5): " + tostr(nums))
nums.remove(5)             # remove first occurrence of 5
print("after remove(5):   " + tostr(nums))

# --- get / set ---
nums.set(0, 99)
print("after set(0,99): " + tostr(nums))
print("get(0) = " + tostr(nums.get(0)))

# --- Membership ---
print("contains 30: " + tostr(nums.contains(30)))
print("contains 99: " + tostr(nums.contains(99)))

# --- Reverse and extend ---
nums.reverse()
print("reversed: " + tostr(nums))
extra = list(100, 200)
nums.extend(extra)
print("extended: " + tostr(nums))

# --- Iteration: for item in list ---
print("items:")
for item in nums:
    print("  " + tostr(item))

# --- Iteration: for i in range with index ---
print("indexed:")
for i in range(nums.len()):
    print(f"  [{i}] = {tostr(nums.get(i))}")

# --- enumerate ---
pairs = enumerate(nums)
print("enumerate sample:")
for pair in pairs:
    idx = pair.get(0)
    val = pair.get(1)
    print(f"  index {tostr(idx)} → {tostr(val)}")
