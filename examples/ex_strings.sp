# ex_strings.sp — String methods and f-strings
# Every built-in string operation in one place.

s = "  Hello, SPECTRA World!  "

# --- Case and whitespace ---
print(s.upper())               # full caps
print(s.lower())               # all lower
print(s.strip())               # trim leading/trailing spaces

# --- Search and test ---
clean = s.strip()
print("contains 'SPECTRA': " + tostr(clean.contains("SPECTRA")))
print("find 'World':       " + tostr(clean.find("World")))
print("startswith 'Hello': " + tostr(clean.startswith("Hello")))
print("endswith '!':       " + tostr(clean.endswith("!")))

# --- Modification ---
swapped = clean.replace("World", "Universe")
print("replaced: " + swapped)

# --- Split and join ---
sentence = "one two three four five"
words = sentence.split(" ")
print("split result: " + tostr(words))
print("word count:   " + tostr(words.len()))
rejoined = "-".join(words)
print("rejoined:     " + rejoined)

# --- Character access and length ---
word = "SPECTRA"
print("length: " + tostr(word.len()))
print("first char: " + word[0])    # 'S'

# --- f-strings ---
lang = "SPECTRA"
version = 1
print(f"Welcome to {lang} version {version}!")
print(f"The word '{lang}' has {word.len()} characters.")
