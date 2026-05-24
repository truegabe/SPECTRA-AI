# ex_basics.sp — Variables, arithmetic, print, tostr
# Demonstrates the fundamental building blocks of SPECTRA.

# --- Variable declaration and basic types ---
x = 42
name = "Alice"
pi_val = 3.14159
flag = true
nothing = null

print("Integer:", tostr(x))
print("String: ", name)
print("Float:  ", tostr(pi_val))
print("Bool:   ", tostr(flag))
print("Null:   ", tostr(nothing))

# --- Arithmetic operators ---
a = 17
b = 5

print("a + b  = " + tostr(a + b))   # addition
print("a - b  = " + tostr(a - b))   # subtraction
print("a * b  = " + tostr(a * b))   # multiplication
print("a / b  = " + tostr(a / b))   # true division  → 3.4
print("a // b = " + tostr(a // b))  # floor division → 3
print("a % b  = " + tostr(a % b))   # modulo         → 2
print("a ** b = " + tostr(a ** b))  # exponentiation

# --- Mixed-type expression ---
result = (a + b) * pi_val - 1.0
print("(a+b)*pi_val - 1 = " + tostr(result))

# --- f-string shorthand ---
msg = f"Name={name}, x={x}, result={result}"
print(msg)
