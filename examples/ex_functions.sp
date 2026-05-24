# ex_functions.sp — Basic funcs, closures, @memoize, @profile, recursion
# Shows SPECTRA's full function system.

# --- Basic function ---
func add(a, b):
    return a + b

func clamp_val(v, lo, hi):
    if v < lo:
        return lo
    if v > hi:
        return hi
    return v

print("add(3,4) = " + tostr(add(3, 4)))
print("clamp(15, 0, 10) = " + tostr(clamp_val(15, 0, 10)))

# --- Recursion ---
func factorial(n):
    if n <= 1:
        return 1
    return n * factorial(n - 1)

print("10! = " + tostr(factorial(10)))

# --- @memoize — cache repeated sub-calls ---
@memoize
func fib(n):
    if n <= 1:
        return n
    return fib(n - 1) + fib(n - 2)

fibs = list()
for i in range(12):
    fibs.append(fib(i))
print("Fibonacci 0-11: " + tostr(fibs))
print("fib(30) = " + tostr(fib(30)))

# --- @profile — time the function ---
@profile
func sum_range(n):
    total = 0
    for i in range(n):
        total = total + i
    return total

print("sum_range(10000) = " + tostr(sum_range(10000)))

# --- Closure: factory that captures a free variable ---
func make_multiplier(factor):
    func multiply(x):
        return x * factor
    return multiply

double = make_multiplier(2)
triple = make_multiplier(3)

print("double(7) = " + tostr(double(7)))
print("triple(7) = " + tostr(triple(7)))

# --- Closure with mutable state accumulator ---
func make_accumulator(start):
    total = start
    func add_to(amount):
        total = total + amount
        return total
    return add_to

acc = make_accumulator(100)
print("acc(10) = " + tostr(acc(10)))
print("acc(25) = " + tostr(acc(25)))
print("acc(5)  = " + tostr(acc(5)))
