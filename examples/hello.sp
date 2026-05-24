func main() -> void:
    x: spect = ~{3:0.4, 7:0.8}~
    print("--- SPECTRA Hello World ---")
    print("Wave Specton created:")
    x.print_wave()
    print("Collapsed value:")
    print(x.collapse())
    print("Peak state:")
    print(x.peak())
    print("Entropy:")
    print(x.entropy())
