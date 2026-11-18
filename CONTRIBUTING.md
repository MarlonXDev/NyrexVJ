# Contributing

Thanks for helping with NyrexVJ.

## Setup

```
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build
python scripts/check_i18n.py
```

Before opening a pull request run `build/NyrexVJ --check-shaders`. It must exit with code 0.

## Rules

- Keep the code free of comments, names and structure carry the meaning.
- Every UI text goes through `tr()` and needs a German entry in `src/core/i18n_de.cpp`. `scripts/check_i18n.py` fails otherwise.
- New clips and effects live in `src/render/clips_extra.cpp` and `src/render/effects_extra.cpp` (or the matching library file). Use the helpers in the shader headers and keep names in English.
- Add a line to `CHANGELOG.md` and `CHANGELOG.de.md` for every user-visible change. Both files must have the same number of items per version, the tests check it.
- Windows-only code stays in `*_win.cpp` files with a fallback next to it.

## Issues

Use the bug report or feature request form. For bugs include the version and the end of the log file.
