# ex_try_except.sp — try/except, catching errors, nested try, error variable
# SPECTRA uses a single except clause that binds the error message.

# --- Basic try/except ---
print("--- basic ---")
try:
    result = 10 / 0
    print("unreachable: " + tostr(result))
except e:
    print("caught division error: " + tostr(e))

# --- try that succeeds (no exception) ---
print("--- success path ---")
try:
    val = 100 / 4
    print("10 / 4 = " + tostr(val))
except e:
    print("this should not print: " + tostr(e))

# --- Using a helper that might fail ---
func safe_divide(a, b):
    try:
        return a / b
    except e:
        print("safe_divide caught: " + tostr(e))
        return 0

print("--- safe_divide ---")
print("safe_divide(9,3) = " + tostr(safe_divide(9, 3)))
print("safe_divide(9,0) = " + tostr(safe_divide(9, 0)))

# --- Nested try/except ---
print("--- nested try ---")
try:
    x = toint("not_a_number")
    try:
        y = x / 0
        print("y = " + tostr(y))
    except inner_e:
        print("inner caught: " + tostr(inner_e))
except outer_e:
    print("outer caught: " + tostr(outer_e))

# --- try in a loop: process a mixed list ---
print("--- loop with try ---")
values = list("10", "20", "oops", "30", "???")
parsed = list()
for v in values:
    try:
        parsed.append(toint(v))
    except e:
        print(f"skipping '{v}': " + tostr(e))

print("successfully parsed: " + tostr(parsed))
print("sum = " + tostr(sum(parsed)))
