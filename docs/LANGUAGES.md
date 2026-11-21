# Languages

NyrexVJ ships with eight interface languages and can load more from files you write yourself.

| Code | Language | Source |
| --- | --- | --- |
| `en` | English | The texts in the program itself |
| `de` | Deutsch | `src/core/i18n_de.cpp`, written by the maintainer |
| `es` | Español | `src/core/i18n_es.cpp`, machine assisted |
| `fr` | Français | `src/core/i18n_fr.cpp`, machine assisted |
| `it` | Italiano | `src/core/i18n_it.cpp`, machine assisted |
| `pt` | Português | `src/core/i18n_pt.cpp`, machine assisted |
| `nl` | Nederlands | `src/core/i18n_nl.cpp`, machine assisted |
| `ja` | 日本語 (Japanese) | `src/core/i18n_ja.cpp`, machine assisted, needs a system font |

Spanish, French, Italian, Portuguese, Dutch and Japanese were translated with machine help and have **not** been reviewed by native speakers. They follow the German table key for key, keep the terms of the VJ world (look, deck, beat, preset, clip, Spout) where the language uses them, and, apart from Japanese, avoid characters outside Latin-1. Wrong or awkward wording is a bug: fix the line in the table and open a pull request, or open an issue with the text and the better translation.

Words that mean one thing in the interface were translated the same way everywhere: layer is `livello`, `camada`, `laag` and `レイヤー`, column is `colonna`, `coluna`, `kolom` and `列`, output is `uscita`, `saída`, `uitvoer` and `出力`, and the audio Level is `Volume`, `Nível`, `Niveau` and `レベル`. The deck stays `deck` in Italian, Portuguese and Dutch and is `デッキ` in Japanese.

Choose the language under Settings, General, under Help, Language or on the welcome screen. The choice is saved. `--lang CODE` starts the program in one language for a single run.

What is translated: every button, label, tooltip and message of the interface, including the error messages that come from the updater, the audio and MIDI code and the preset loader. What stays English on purpose: clip and effect names (they match what other VJ programs call them), the changelog on the welcome screen (only German has its own changelog), the log file and the OSC addresses. The note under the language list in Settings names all built-in languages and is the only text that was changed in German, Spanish and French when the new languages came.

## Japanese and the font

The built-in font only has Latin letters. When Japanese is selected, NyrexVJ looks for a Japanese system font (Windows: Yu Gothic, Meiryo or MS Gothic in the Fonts folder; Linux: Noto Sans CJK or IPAGothic; macOS: Hiragino) and merges it into the interface font, which then supplies only the characters the main font lacks. The Latin text keeps its look. Nothing is bundled and nothing is downloaded. The language list shows `日本語` once such a font is loaded and `Japanese` before that or if none is found, and Settings, General then says in English that no Japanese system font was found. The merge was tried with Noto Sans CJK on Linux; Windows fonts were not tried by hand.

## Your own language

1. Settings, General, Export translation template writes `template.json` into the `languages` folder (inside the NyrexVJ data folder). Open languages folder shows it.
2. Copy `template.json` to a new name, for example `svenska.json`, and edit it:

```json
{
  "code": "sv",
  "name": "Svenska",
  "translations": {
    "Save": "Spara",
    "Layer ": "Lager ",
    "Update available: ": "Uppdatering tillgänglig: "
  }
}
```

3. Press Reload languages. The new language shows up in every language list and can be started with `--lang sv`.

Rules:

- `code` is 2 to 12 characters of `a-z`, `0-9` and `-` and must not be one of the built-in codes. `name` is what the list shows, 1 to 32 characters.
- The key on the left is the English text and must match exactly. Only the value on the right changes. A text you leave out or delete stays English, so a file can start small and grow.
- Keys that end in a space (`"Layer "`, `"Saved preset: "`) are the first part of a message that continues with a name or a number. NyrexVJ puts the space back if you forget it. Keep placeholders such as `%.2f`, `%d` and `<name>` and numbers like `16` unchanged.
- Values must be plain text with at most 1000 characters. Empty values and values that are not text are ignored.
- The built-in font only has the letters up to U+00FF (English, German, Spanish, French, Italian, Portuguese, Dutch, the Nordic languages and similar). Texts with other characters, such as Cyrillic, Greek or Asian scripts, are skipped in your own files and Reload languages tells you how many. The system font that shows built-in Japanese is not used for files you write yourself.
- A file can be at most 1 MiB, there is room for 16 language files, and a file that is not valid JSON or has no `code`, `name` or `translations` is skipped. The first problem is shown after Reload languages and written to the log.

## Adding a built-in language

Built-in languages are for translations the project is ready to maintain. To add one:

1. Add the language to the `Language` enum and to `kBuiltIn` in `src/core/i18n.{hpp,cpp}` and raise `kBuiltInLanguageCount`.
2. Copy `src/core/i18n_es.cpp` to a new file with the same key list, translate the values, add the file to `NYREXVJ_CORE_SOURCES` in `CMakeLists.txt` and to `builtInTable` and the declarations.
3. Add the language to `LANGUAGE_FILES` in `scripts/check_i18n.py`. A language that needs characters beyond Latin-1 also needs a font; Japanese is the example (`languageNeedsCjkFont` in `src/core/i18n.cpp` and `src/app/fonts.cpp`).

`python scripts/check_i18n.py` (run in CI) checks that every table has exactly the keys of the German table, that edge spaces and format specifiers match and that no character outside Latin-1 is used in the tables other than German and Japanese. `nvj_i18n_tests` repeats the key and character checks on the compiled tables and covers loading, reloading and the template of your own languages.
