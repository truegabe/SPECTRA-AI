# ex_spectons.sp — Wave, range, fixed Spectons; interfere, resonate, entropy, peak
# Spectons are SPECTRA's base-10 quantum-inspired numeric type.

# ─── Fixed Specton (deterministic value) ───────────────────────────
print("--- fixed ---")
fixed = ~{7:1.0}~          # state 7, certainty 100 %
fixed.print_wave()
print("peak:    " + tostr(fixed.peak()))
print("entropy: " + tostr(fixed.entropy()))

# ─── Wave Specton (superposition of states) ────────────────────────
print("")
print("--- wave ---")
a = ~{3:0.5, 6:0.8, 9:0.3}~   # three competing states
print("Specton A:")
a.print_wave()
print("peak:    " + tostr(a.peak()))
print("entropy: " + tostr(a.entropy()))   # higher → more uncertain

b = ~{4:0.6, 6:0.7, 8:0.4}~
print("Specton B:")
b.print_wave()

# ─── interfere: destructive (XOR-like, cancels shared states) ──────
print("")
print("--- interfere (A XOR B) ---")
c = a.interfere(b)
print("A interfere B:")
c.print_wave()
print("peak of interference: " + tostr(c.peak()))

# ─── resonate: constructive (AND-like, amplifies shared states) ────
print("")
print("--- resonate (A AND B) ---")
d = a.resonate(b)
print("A resonate B:")
d.print_wave()
print("peak of resonance: " + tostr(d.peak()))

# ─── Range Specton (uniform distribution lo..hi) ───────────────────
print("")
print("--- range ---")
r = |[2..8]|
print("Range [2..8]:")
print(tostr(r))
print("collapsed: " + tostr(r.collapse()))

# ─── SpectArray ────────────────────────────────────────────────────
print("")
print("--- SpectArray ---")
arr = zeros(4)
arr.set(0, ~{1:1.0}~)
arr.set(1, ~{5:0.5, 6:0.5}~)
arr.set(2, ~{9:0.8, 8:0.2}~)
arr.set(3, ~{0:1.0}~)
print("array:")
arr.print()
print("mean entropy: " + tostr(mean_entropy(arr)))
collapsed = collapse_all(arr)
print("collapsed array:")
collapsed.print()
