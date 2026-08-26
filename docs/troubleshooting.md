# சிக்கல் தீர்வு / Troubleshooting — Tamil rendering

## Step 0: one-shot fixes

```bash
ta setup-konsole     # inside KDE Konsole: picks best Tamil font, applies it to
                     # the live session AND patches your default profile
source scripts/ta_env.sh   # sets UTF-8 locale + puts build/ on PATH
```

## Step 1: run the built-in check

```bash
ta doctor
```

It verifies locale codeset, lists installed Tamil fonts (via fontconfig),
prints a live glyph sample (`க கா கி கீ …`), and exits non-zero if something
needs fixing. Everything it suggests is mirrored below.

## Step 2: what "broken" looks like and why

| Symptom | Cause | Fix |
|---|---|---|
| □□□ boxes | active terminal font has no Tamil glyphs | pick a Tamil font in the terminal profile |
| letters look stacked/overlapping ("intertwined") | weak complex-script shaping in the terminal emulator | use a shaping-aware terminal or accept static glyphs |
| plain squares even in GUI apps | fonts not installed at all | install font package below |
| garbage bytes / � | program output invalid UTF-8 | Tamizhi output is always valid UTF-8; run `ta doctor`; if you can reproduce, file a bug |

## Step 3: Konsole (KDE) setup

1. Install a Tamil-capable font:
   - Fedora: `sudo dnf install google-noto-sans-tamil-fonts`
   - Debian/Ubuntu: `sudo apt install fonts-noto-core`
   - Arch: `sudo pacman -S noto-fonts`
2. Konsole → **Settings ▸ Edit Profile ▸ Appearance ▸ Font** → choose **Noto Sans Tamil**
   (or Lohit Tamil). Monospace-only profiles usually lack Tamil; Konsole's
   per-glyph fallback cannot fully shape Indic clusters.
3. Close and reopen the terminal window.
4. Verify with `ta doctor` sample line.

## Notes on terminal limits

Terminal cells render glyphs statically; full OpenType shaping of Tamil
(conjuncts like க்ஷ, repositioned matras) depends on the emulator. kitty and
wezterm shape noticeably better than most VTE/Konsole builds. For reading long
Tamil diagnostics, `ta check file.ta 2>&1 | less` or an editor problem panel is
more comfortable than any terminal.

## Compiler-side guarantees (v0.2.0)

- All compiler output (diagnostics, snippets, carets) is valid UTF-8 — verified
  by tests with `iconv`.
- Source lines echoed in errors are sanitized: stray invalid bytes become U+FFFD,
  control characters become `?`.
- Carets align by **grapheme cluster** (base letter + matras = one column), so
  markers sit under the right glyph in any conforming terminal.
- `setlocale(LC_ALL, "")` runs at startup; if no UTF-8 locale can be found,
  messages fall back to English automatically (with a warning). Sources are
  compiled with explicit `-finput-charset=UTF-8 -fexec-charset=UTF-8`.
- A UTF-8 locale is recommended (`LANG=en_US.UTF-8` or `ta_IN.UTF-8`).
- Previous anchor kept for reference:
  (`LANG=en_US.UTF-8` or `ta_IN.UTF-8`).
