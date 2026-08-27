# Tamil Font Rendering

Tamil is an abugida: combining vowel signs attach to base consonants to form
single *orthographic clusters* (e.g. `க` + `ு` → `கு`). A correct terminal /
editor must render each cluster as **one connected glyph**, not as separate
loose characters. If your output looks "spaced out" or the vowel signs float
away from their base, the font or terminal does not support Tamil shaping.

## Check installed fonts

```
ta check-fonts
```

Lists every Tamil-capable font found via `fontconfig` (`fc-list :lang=ta`) and
reports whether recommended families (Noto, Lohit, Catamaran, …) are present.

## Install fonts

```
ta install-fonts
```

Runs `tools/install-tamil-fonts.sh`, which auto-detects your package manager
(`apt`, `dnf`, `yum`, `pacman`, `apk`, `brew`) and installs a Tamil font such
as *Noto Sans Tamil*. Requires administrator/root privileges on most systems.

## Configure the terminal (Konsole)

On KDE Konsole, Tamil clusters often break because the default font lacks
shaping. Fix it with:

```
ta setup-konsole
```

This sets Konsole's font to the best available Tamil family and recommends a
terminal with better shaping support (`kitty` or `WezTerm`) when detected.

## Recommended fonts

- **Noto Sans Tamil** / **Noto Serif Tamil** — broadest coverage, ships with
  most distributions.
- **Lohit Tamil** — common on Fedora/RHEL.
- **Catamaran**, **Bamini**, **Suruma** — alternative display faces.

## Cross-platform notes

- **Linux / macOS**: rely on `fontconfig`. `ta doctor` gives a full environment
  report including font availability.
- **Windows**: the runtime switches the console to UTF-8 automatically
  (`ta_rt_init` calls `SetConsoleOutputCP(CP_UTF8)` and `_setmode(_O_U8TEXT)`).
  Use a terminal that supports Unicode (Windows Terminal) and install a Tamil
  font for correct glyph shaping.
- **Web / C backend**: when compiling via `ta build --target=c`, output is plain
  UTF-8 and renders anywhere UTF-8 text is accepted.

## Diagnosing broken rendering

If `ta check-fonts` finds no fonts, install one (above). If fonts exist but
clusters still break, the issue is the *terminal*, not Tamizhi — switch to a
terminal with OpenType shaping support.
