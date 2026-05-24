# SPECTRA Language Reference

> **SPECTRA** is a general-purpose programming language with a Python-like syntax and C-level power.  
> Its unique primitive is the **Specton** — a base-10 quantum-inspired data unit with three modes: Fixed, Range, and Wave.  
> SPECTRA programs run via `spectra.exe <file.sp>`.

---

## Table of Contents

1. [Syntax basics](#1-syntax-basics)
2. [Types](#2-types)
3. [Variables & assignment](#3-variables--assignment)
4. [Operators](#4-operators)
5. [String methods](#5-string-methods)
6. [List methods](#6-list-methods)
7. [Dict (Map) methods](#7-dict-map-methods)
8. [Control flow](#8-control-flow)
9. [Functions](#9-functions)
10. [Decorators](#10-decorators)
11. [Try / Except](#11-try--except)
12. [F-strings](#12-f-strings)
13. [Imports & modules](#13-imports--modules)
14. [Built-in functions](#14-built-in-functions)
15. [Spectons](#15-spectons)
16. [SpectArray](#16-spectarray)
17. [SpectMatrix](#17-spectmatrix)
18. [Structs](#18-structs)
19. [Match statement](#19-match-statement)
20. [Full example](#20-full-example)

---

## 1. Syntax basics

- **Indentation** defines blocks (like Python) — 4 spaces recommended
- **Comments** start with `#`
- **Statements** end at newline (no semicolons needed)
- **Case-sensitive** — `myVar` ≠ `myvar`

```
# This is a comment
x = 10           # integer
name = "Alice"   # string
```

---

## 2. Types

| Type | Description | Example |
|------|-------------|---------|
| `int` | 64-bit integer | `42`, `-7` |
| `float` | 64-bit double | `3.14`, `-0.5` |
| `str` | UTF-8 string | `"hello"` |
| `bool` | Boolean | `true`, `false` |
| `null` | No value | `null` |
| `list` | Dynamic array | `list(1, 2, 3)` |
| `dict` / map | String-keyed hash map | `{"a": 1}` |
| `Specton` | Base-10 quantum unit | `~{4:0.6, 5:0.4}~` |
| `SpectArray` | Array of Spectons | `zeros(10)` |
| `SpectMatrix` | 2D matrix of Spectons | `zeros([3, 3])` |

> **Note:** `str` is a reserved type keyword. Use `tostr()` to convert values to strings.

---

## 3. Variables & assignment

```
x = 42
name = "Alice"
items = list(1, 2, 3)
data = {"key": "value"}

# Optional type annotation (informational, not enforced)
count: int = 0
label: str = "hello"
mat: matrix = zeros([4, 4])

# Augmented assignment
x = x + 1
x = x * 2
# NOTE: += style not supported; use x = x + 1
```

---

## 4. Operators

### Arithmetic
```
a + b        # addition (also string concat)
a - b        # subtraction
a * b        # multiplication
a / b        # float division
a // b       # floor division (floors toward -∞)
a % b        # modulo
a ** b       # exponentiation
```

### Comparison
```
a == b    a != b
a < b     a > b
a <= b    a >= b
```

### Logical
```
a and b
a or b
not a
```

### Membership
```
x in my_list         # true if x is in the list
x in my_string       # true if x is a substring
x in my_dict         # true if x is a key in the dict
x not in my_list     # negation
```

---

## 5. String methods

```
s = "  Hello World  "

s.upper()              # "  HELLO WORLD  "
s.lower()              # "  hello world  "
s.strip()              # "Hello World"  (removes leading/trailing whitespace)
s.len()                # 15
s[0]                   # " " (character at index)
s.contains("World")    # true
s.find("World")        # 8  (-1 if not found)
s.startswith("  He")   # true
s.endswith("  ")       # true
s.replace("World", "SPECTRA")  # "  Hello SPECTRA  "
s.split(" ")           # list of words split by separator
" ".join(my_list)      # join a list into a string with separator
```

**F-string interpolation:**
```
name = "Alice"
age = 30
greeting = f"Hello {name}, you are {age + 1} next year!"
print(greeting)  # Hello Alice, you are 31 next year!
```

---

## 6. List methods

```
items = list()                   # empty list
items = list(1, 2, 3)            # list with initial values
items = list("a", "b", "c")

items.append(4)                  # add to end
items.pop()                      # remove & return last item
items.insert(0, 99)              # insert 99 at index 0
items.remove(99)                 # remove first occurrence of 99
items.contains(3)                # true/false
items.reverse()                  # in-place reversal
items.extend(other_list)         # append all items from another list
items.get(0)                     # get item at index (same as items[0])
items.set(0, 99)                 # set item at index
items.len()                      # length

# Iteration
for item in items:
    print(tostr(item))

# Index access
first = items[0]
```

---

## 7. Dict (Map) methods

```
d = {"name": "Alice", "age": 30}    # dict literal
d = dict()                           # empty dict

d.get("name")          # "Alice"   (null if key missing)
d.set("age", 31)       # update or insert
d.has("name")          # true
d.has("email")         # false
d.delete("age")        # remove key
d.keys()               # list of all keys
d.values()             # list of all values
d.len()                # number of entries

# Iterate keys
keys = d.keys()
for k in keys:
    print(k + " = " + tostr(d.get(k)))
```

---

## 8. Control flow

### If / elif / else
```
if x > 0:
    print("positive")
elif x == 0:
    print("zero")
else:
    print("negative")
```

### While
```
i = 0
while i < 10:
    print(tostr(i))
    i = i + 1
```

### For loop — range
```
for i in range(5):          # 0, 1, 2, 3, 4
    print(tostr(i))

for i in range(1, 6):       # 1, 2, 3, 4, 5
    print(tostr(i))

for i in range(0, 10, 2):   # 0, 2, 4, 6, 8
    print(tostr(i))
```

### For loop — collections
```
for item in my_list:
    print(tostr(item))

for ch in "hello":          # iterate string characters
    print(ch)
```

### Break / continue
```
for i in range(10):
    if i == 5:
        break
    if i % 2 == 0:
        continue
    print(tostr(i))
```

---

## 9. Functions

```
func greet(name):
    return "Hello, " + name + "!"

result = greet("Alice")
print(result)
```

### Default-like patterns
```
func power(base, exp):
    result = 1
    i = 0
    while i < exp:
        result = result * base
        i = i + 1
    return result
```

### Closures (functions that capture variables)
```
func make_adder(n):
    func add(x):
        return x + n     # n is captured from outer scope
    return add

add5 = make_adder(5)
print(tostr(add5(10)))   # 15
```

### Recursion
```
func factorial(n):
    if n <= 1:
        return 1
    return n * factorial(n - 1)
```

---

## 10. Decorators

```
@memoize
func fib(n):
    if n <= 1: return n
    return fib(n - 1) + fib(n - 2)

@profile
func heavy_work():
    i = 0
    while i < 100000:
        i = i + 1

@deprecated
func old_api():
    return 42
```

- `@memoize` — caches function results (memoization)
- `@profile` — prints timing info when called
- `@deprecated` — prints a warning when called

---

## 11. Try / Except

```
try:
    result = 10 / 0
except e:
    print("Caught: " + e)

# Without binding the error variable
try:
    bad_call()
except:
    print("Something went wrong")

# Nested try
try:
    try:
        x = 1 / 0
    except inner:
        print("inner: " + inner)
    print("continuing after inner try")
except outer:
    print("outer: " + outer)
```

---

## 12. F-strings

```
name = "SPECTRA"
version = 3
pi_val = 3.14159

# Basic interpolation
print(f"Language: {name}")

# Expressions inside {}
print(f"Version {version + 1} coming soon!")
print(f"Pi rounded: {pi_val}")

# Calls inside {}
items = list(1, 2, 3)
print(f"List length: {items.len()}")
```

> Expressions inside `{}` are fully evaluated — you can use any expression.

---

## 13. Imports & modules

### Import entire module
```
import math
import neural
import random
```

### Selective import
```
from math import sin, cos, sqrt
```

After `from math import sin`, call `sin(x)` directly (not `math.sin(x)`).

### Built-in modules

**`math`** — mathematical functions
```
import math

pi()           # 3.14159...
e()            # 2.71828...
sin(x)         # sine (radians)
cos(x)
tan(x)
log(x)         # natural log
sqrt(x)
abs(x)
floor(x)
ceil(x)
pow(base, exp)
```

**`neural`** — neural network engine
```
import neural

# Create network: sizes array, learning rate
sz = zeros(3)
sz.set(0, ~{2:1.0}~)    # input: 2 neurons
sz.set(1, ~{4:1.0}~)    # hidden: 4 neurons
sz.set(2, ~{1:1.0}~)    # output: 1 neuron

net = neural_create(sz, 0.05)
neural_summary(net)

# Build input/target matrices, then train
loss = neural_train(net, inp_matrix, tgt_matrix, 100)
```

**`random`** — random utilities
```
import random

w = random_wave(10)      # random SpectArray of 10 wave Spectons
n = random_int(1, 100)   # random int in [1, 100]
```

**`io`** — file I/O
```
import io

io.write("out.txt", "hello\n")
content = io.read("out.txt")
lines = io.lines("out.txt")        # list of strings
io.append("out.txt", "more\n")
exists = io.exists("out.txt")      # true/false
```

### `.sp` file modules
Any `.sp` file in the same directory can be imported:
```
import myutils       # loads myutils.sp and imports its globals
```

---

## 14. Built-in functions

```
print(a)              # print value
print(a, b, c)        # print multiple values, space-separated

len(x)                # length of string, list, map, or array

range(n)              # 0..n-1
range(start, stop)    # start..stop-1
range(start, stop, step)

list()                # empty list
list(a, b, c)         # list with values

dict()                # empty map

tostr(x)              # convert any value to string
toint(x)              # convert to int
tofloat(x)            # convert to float

sorted(my_list)       # return new sorted list (numeric or string)
sum(my_list)          # sum of numeric list or SpectArray
min(a, b)             # minimum of two values
min(my_list)          # minimum of list
max(a, b)
max(my_list)

enumerate(my_list)    # [[0,item0], [1,item1], ...]
zip(list1, list2)     # [[a0,b0], [a1,b1], ...]  (up to shorter length)

map(builtin_fn, list) # apply a builtin function to each element
filter(builtin_fn, list)  # keep elements where builtin_fn returns truthy

format(val, spec)     # number formatting (e.g. format(3.14159, ".2f"))
```

---

## 15. Spectons

A Specton is the core data unit — a base-10 (states 0–9) quantum-inspired value.

### Three modes

**Fixed** — definite value:
```
a = ~{5:1.0}~    # fixed at state 5
```

**Range** — span across states:
```
r = |[2..7]|     # uniform across states 2 to 7
```

**Wave** — weighted superposition:
```
w = ~{4:0.6, 5:0.4}~   # 60% chance state 4, 40% chance state 5
```

### Specton operations
```
a = ~{4:0.6, 5:0.4}~
b = ~{5:0.7, 6:0.3}~

a.print_wave()         # visual bar chart of weights
a.entropy()            # Shannon entropy of the distribution
a.peak()               # most probable state

c = a.interfere(b)     # XOR-like quantum interference
d = a.resonate(b)      # AND-like resonance (peak alignment)
```

---

## 16. SpectArray

A fixed-length array of Spectons.

```
arr = zeros(5)         # 5-element array, all fixed at 0

arr.set(0, ~{3:1.0}~)  # set element 0 to fixed state 3
arr.set(1, ~{7:0.5, 8:0.5}~)

v = arr.get(2)         # get element (returns Specton)
arr.print()            # print all elements

# Useful functions
collapse_all(arr)      # collapses all wave states to fixed → returns new SpectArray
mean_entropy(arr)      # average entropy across all elements

# From neural/random modules
signal = random_wave(10)   # 10-element SpectArray with random wave Spectons
```

---

## 17. SpectMatrix

A 2D matrix of Spectons.

```
m = zeros([3, 3])          # 3×3 matrix, all zeros

m.set(0, 0, ~{9:1.0}~)    # set element at row 0, col 0
v = m.get(1, 2)            # get element at row 1, col 2

m.rows                     # number of rows (attribute)
m.cols                     # number of columns (attribute)
m.print()                  # print matrix

t = m.transpose()          # returns transposed copy
result = a.matmul(b)       # matrix multiply
```

---

## 18. Structs

```
struct Point:
    x: float
    y: float

struct Circle:
    center: Point
    radius: float

p = Point(1.0, 2.0)
print(tostr(p.x))

c = Circle(Point(0.0, 0.0), 5.0)
print(tostr(c.radius))
```

---

## 19. Match statement

```
x = 3

match x:
    case 1:
        print("one")
    case 2:
        print("two")
    case 3:
        print("three")
    else:
        print("other")
```

---

## 20. Full example

```
import math

# ── helpers ──────────────────────────────────────
@memoize
func fib(n):
    if n <= 1: return n
    return fib(n - 1) + fib(n - 2)

func make_multiplier(factor):
    func mul(x):
        return x * factor
    return mul

# ── main ──────────────────────────────────────────
name = "SPECTRA"
version = 1

print(f"Welcome to {name} v{version}")
print("")

# Fibonacci
seq = list()
for i in range(10):
    seq.append(fib(i))
print("Fibonacci: " + tostr(seq))

# Closures
double = make_multiplier(2)
triple = make_multiplier(3)
nums = list(1, 2, 3, 4, 5)
doubled = list()
for n in nums:
    doubled.append(double(n))
print("Doubled: " + tostr(doubled))

# Dict
scores = {"Alice": 95, "Bob": 87, "Carol": 92}
names = scores.keys()
for name in names:
    grade = scores.get(name)
    if grade >= 90:
        print(f"{name}: A  ({grade})")
    else:
        print(f"{name}: B  ({grade})")

# Try/except
try:
    x = 1 / 0
except e:
    print("Caught division error: " + e)

# Math
print(f"sin(π/4) = {sin(pi() / 4.0)}")
print(f"sqrt(2)  = {sqrt(2.0)}")

# Spectons
a = ~{3:0.5, 4:0.5}~
b = ~{4:0.8, 5:0.2}~
c = a.resonate(b)
print("Resonated peak: " + tostr(c.peak()))
```

---

## Quick syntax cheat-sheet

```
# Variable
x = 42

# String conversion  (NOT str() — that's a keyword)
tostr(x)

# List
items = list(1, 2, 3)
items.append(4)
for item in items: print(tostr(item))

# Dict
d = {"key": "value"}
d.get("key")
d.set("key2", 99)

# F-string
print(f"x = {x}, doubled = {x * 2}")

# Floor division
7 // 2      # = 3
-7 // 2     # = -4

# Membership
"hello" in my_list
"world" not in my_string

# Try/except
try:
    risky()
except e:
    print("Error: " + e)

# Specton
w = ~{5:0.7, 6:0.3}~
w.peak()          # 5
w.entropy()       # ~0.881

# Import
import math
from math import sqrt, pi
```

---

*SPECTRA Language Reference — generated 2026-05-23*
