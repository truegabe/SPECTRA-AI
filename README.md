# SPECTRA

A small, base-10 language built around **Spectons** — values that hold
probability distributions instead of single numbers.

## What it is

SPECTRA isn't binary. A variable can be:

  - **sharp** — `5` ("state 5, period")
  - **ranged** — `3..7` ("somewhere in 3 through 7")
  - **wave-shaped** — `~{0:0.1, 1:0.7, 2:0.2}~`
    ("70% at state 1, 10% at state 0, 20% at state 2")

The language treats all three forms as first-class values, and two
operators make the rest interesting:

  - `a.resonate(b)` — keeps the states where both `a` and `b` carry
    weight. Like an AND that amplifies agreement.
  - `a.interfere(b)` — cancels the states where `a` and `b` overlap.
    Like an XOR that erases sameness.

With `.peak()` (the most likely state) and `.entropy()` (how unfocused
the value is), you can write probabilistic logic that reads almost
like normal code.

## Hello world

```spectra
s = ~{0:0.1, 1:0.7, 2:0.2}~
print("peak: "    + tostr(s.peak()))      # 1
print("entropy: " + tostr(s.entropy()))   # around 0.80
```

See [`examples/`](examples/) for more, and
[`SPECTRA_LANGUAGE_REFERENCE.md`](SPECTRA_LANGUAGE_REFERENCE.md) for
the full language reference.

## What it can do

A few things you can express in 20–40 lines of SPECTRA that come out
awkwardly in scalar-only languages:

  - **Composing moods.** Take four neuromodulator levels (dopamine,
    norepinephrine, serotonin, acetylcholine) as Spectons. Then
    `resonate` and `interfere` give you an overall affective state:
    approach vs. avoid, focused vs. scattered, sharp vs. diffuse.

  - **Picking the right goal.** Build a Specton from current attention,
    one from each candidate goal, then resonate every pair. The goal
    whose resonance peaks sharpest wins — and the entropies tell you
    whether the choice was decisive or a coin flip.

  - **Resolving conflicting evidence.** Two sources disagree on a fact.
    Each side's trust, recency, and confidence resonate together into a
    verdict Specton. The side whose verdict is higher-peaked and
    lower-entropy is the one to believe — and if both come out diffuse,
    you know to escalate instead of guessing.

In each case the win isn't the operator — it's that the *entropy* and
*peak* of the result tell you not just **what** the answer is, but
**how confident** the language is in it.

## Build

### Windows (MinGW)

```
build.bat
spectra.exe examples/hello.sp
```

### Linux / macOS / CMake

```
cmake -B build
cmake --build build
./build/spectra examples/hello.sp
```

## Tests

```
make test            # runtime tests (C)
python tests/run_all.py   # language tests (.sp files)
```

## License

Licensed under the **Apache License, Version 2.0** — see [LICENSE](LICENSE).
Free to use, modify, and redistribute, including commercially, with attribution.

---

*Questions or suggestions: open an issue on this repo.*
