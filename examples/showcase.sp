import neural
import math

# ═══════════════════════════════════════════════════════════════════
#          SPECTRA LANGUAGE  —  FEATURE SHOWCASE
# ═══════════════════════════════════════════════════════════════════

print("╔══════════════════════════════════════════╗")
print("║       SPECTRA LANGUAGE SHOWCASE          ║")
print("╚══════════════════════════════════════════╝")
print("")

# ───────────────────────────────────────────────────────────────────
# 1. STRINGS  — rich method suite
# ───────────────────────────────────────────────────────────────────

print("━━━ 1. STRING POWER ━━━")

title = "  spectra programming language  "
print(title.strip().upper())

sentence = "the quick brown fox jumps over the lazy dog"
words = sentence.split(" ")
print("Word count: " + tostr(words.len()))

caps = list()
for w in words:
    first = w[0].upper()
    rest_len = w.len() - 1
    if rest_len > 0:
        rest = w[1]
        i = 2
        while i < w.len():
            rest = rest + w[i]
            i = i + 1
        caps.append(first + rest)
    else:
        caps.append(first)

print(" ".join(caps))
print("Has 'fox': " + tostr(sentence.contains("fox")))
print("'fox' at: " + tostr(sentence.find("fox")))
print("Replaced: " + sentence.replace("fox", "SPECTON"))
print("")

# ───────────────────────────────────────────────────────────────────
# 2. LISTS  — Python-style dynamic arrays
# ───────────────────────────────────────────────────────────────────

print("━━━ 2. LIST OPERATIONS ━━━")

primes = list(2, 3, 5, 7, 11, 13, 17, 19, 23, 29)
print("Primes: " + tostr(primes))

squares = list()
for i in range(1, 11):
    squares.append(i * i)
print("Squares: " + tostr(squares))

stack = list("alpha", "beta", "gamma")
top = stack.pop()
print("Popped: " + top)
print("Stack: " + tostr(stack))

stack.insert(0, "delta")
print("After insert at 0: " + tostr(stack))

stack.reverse()
print("Reversed: " + tostr(stack))
print("")

# ───────────────────────────────────────────────────────────────────
# 3. SPECTRONS  — base-10 quantum-inspired units
# ───────────────────────────────────────────────────────────────────

print("━━━ 3. SPECTON QUANTUM STATES ━━━")

a = ~{4:0.6, 5:0.4}~
b = ~{5:0.7, 6:0.3}~

print("Specton A:")
a.print_wave()
print("  Entropy: " + tostr(a.entropy()))
print("  Peak:    " + tostr(a.peak()))

print("Specton B:")
b.print_wave()

interfered = a.interfere(b)
print("A interfere B:")
interfered.print_wave()

resonated = a.resonate(b)
print("A resonate B:")
resonated.print_wave()
print("")

# ───────────────────────────────────────────────────────────────────
# 4. SPECTON ARRAYS  — vectorized quantum math
# ───────────────────────────────────────────────────────────────────

print("━━━ 4. SPECTON ARRAYS ━━━")

signal = random_wave(10)
print("Random wave signal:")
signal.print()
print("Mean entropy: " + tostr(mean_entropy(signal)))
collapsed = collapse_all(signal)
print("Collapsed:")
collapsed.print()
print("")

# ───────────────────────────────────────────────────────────────────
# 5. FIBONACCI  — memoized recursion
# ───────────────────────────────────────────────────────────────────

print("━━━ 5. MEMOIZED FIBONACCI ━━━")

@memoize
func fib(n):
    if n <= 1:
        return n
    return fib(n - 1) + fib(n - 2)

seq = list()
for i in range(16):
    seq.append(fib(i))
print("Fib(0..15): " + tostr(seq))
print("Fib(30) = " + tostr(fib(30)))
print("")

# ───────────────────────────────────────────────────────────────────
# 6. CLOSURES  — higher-order functions
# ───────────────────────────────────────────────────────────────────

print("━━━ 6. CLOSURES ━━━")

func make_adder(n):
    func add(x):
        return x + n
    return add

func make_power(exp):
    func power(base):
        result = 1
        i = 0
        while i < exp:
            result = result * base
            i = i + 1
        return result
    return power

add10 = make_adder(10)
square_fn = make_power(2)
cube_fn   = make_power(3)

out = list()
for i in range(1, 6):
    out.append(add10(i))
print("add10(1..5): " + tostr(out))

out2 = list()
for i in range(1, 6):
    out2.append(square_fn(i))
print("squares:     " + tostr(out2))

out3 = list()
for i in range(1, 6):
    out3.append(cube_fn(i))
print("cubes:       " + tostr(out3))
print("")

# ───────────────────────────────────────────────────────────────────
# 7. MATH MODULE
# ───────────────────────────────────────────────────────────────────

print("━━━ 7. MATH MODULE ━━━")

print("pi  = " + tostr(pi()))
print("e   = " + tostr(e()))
print("sin(pi/2) = " + tostr(sin(pi() / 2.0)))
print("cos(0)    = " + tostr(cos(0.0)))
print("log(e)    = " + tostr(log(e())))
print("sqrt(144) = " + tostr(sqrt(144.0)))
print("")

# ───────────────────────────────────────────────────────────────────
# 8. TEXT PIPELINE  — strings + lists together
# ───────────────────────────────────────────────────────────────────

print("━━━ 8. TEXT PIPELINE ━━━")

corpus = "spectra is fast powerful quantum and intelligent"
all_words = corpus.split(" ")
long_words = list()
for w in all_words:
    if w.len() >= 6:
        long_words.append(w.upper())

print("Long words: " + tostr(long_words))
print("Count: " + tostr(long_words.len()))

report = ", ".join(long_words)
print("Report: " + report)
print("")

# ───────────────────────────────────────────────────────────────────
# 9. NEURAL NETWORK  — built-in learning engine
# ───────────────────────────────────────────────────────────────────

print("━━━ 9. NEURAL NETWORK XOR ━━━")

# Build layer sizes as SpectArray [2, 4, 1]
sz = zeros(3)
sz.set(0, ~{2:1.0}~)
sz.set(1, ~{4:1.0}~)
sz.set(2, ~{1:1.0}~)

net = neural_create(sz, 0.05)
neural_summary(net)

# XOR training data (4 samples, 2 inputs, 1 output)
inp: matrix  = zeros([4, 2])
tgt: matrix  = zeros([4, 1])

# 0 XOR 0 = 0
inp.set(0, 0, ~{0:1.0}~)
inp.set(0, 1, ~{0:1.0}~)
tgt.set(0, 0, ~{0:1.0}~)

# 0 XOR 9 = 9
inp.set(1, 0, ~{0:1.0}~)
inp.set(1, 1, ~{9:1.0}~)
tgt.set(1, 0, ~{9:1.0}~)

# 9 XOR 0 = 9
inp.set(2, 0, ~{9:1.0}~)
inp.set(2, 1, ~{0:1.0}~)
tgt.set(2, 0, ~{9:1.0}~)

# 9 XOR 9 = 0
inp.set(3, 0, ~{9:1.0}~)
inp.set(3, 1, ~{9:1.0}~)
tgt.set(3, 0, ~{0:1.0}~)

print("Training 100 epochs...")
final_loss = neural_train(net, inp, tgt, 100)
print("Final loss: " + tostr(final_loss))
print("")

# ───────────────────────────────────────────────────────────────────
# DONE
# ───────────────────────────────────────────────────────────────────

print("╔══════════════════════════════════════════╗")
print("║         SHOWCASE COMPLETE  ✓             ║")
print("╚══════════════════════════════════════════╝")
