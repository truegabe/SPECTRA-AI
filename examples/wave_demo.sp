func main() -> void:
    print("=== Wave Arithmetic Demo ===")

    a: spect = ~{2:0.3, 5:0.9, 8:0.5}~
    b: spect = ~{5:0.7, 8:0.6, 9:0.2}~

    print("A:")
    a.print_wave()
    print("B:")
    b.print_wave()

    resonated = a.resonate(b)
    print("A resonate B (shared peaks):")
    resonated.print_wave()
    print("Resonance peak:")
    print(resonated.peak())

    spread = a.spread(b)
    print("A spread B (constructive):")
    spread.print_wave()

    interfered = a.interfere(b)
    print("A interfere B (destructive):")
    interfered.print_wave()

    print("=== Range Demo ===")
    r: spect = |[2..8]|
    print("Range specton:")
    print(r)
    print("Range collapsed:")
    print(r.collapse())

    print("=== Wave If Demo ===")
    s: spect = ~{1:0.1, 4:0.3, 7:0.9}~
    wave_if s:
        at 1:
            print("low signal")
        at 4:
            print("mid signal")
        at 7:
            print("high signal — dominant")
        else:
            print("unknown state")

    print("=== Match Demo ===")
    match s:
        Fixed(v):
            print("certain value")
        Range(lo, hi):
            print("range value")
        Wave(w):
            print("wave — peak at:")
            print(w.peak())
