# Translations

Vanta is translated by its community. This document explains how the catalogs
are put together, how to improve an existing translation, and how to add a new
language.

## How it works

- Every user-facing string is wrapped in `Vanta::tr("...")`. The single
  translation context is `Vanta`, declared in `src/core/I18n.h`; there are no
  per-class contexts, so a string is found by its English text alone.
- The source language is English. An unmarked string, or a string with no
  translation in the active catalog, is shown in English.
- Translations live in `translations/vanta_<code>.ts` (Qt Linguist format).
  The `.ts` files are the files translators edit and the files reviewed in a
  pull request.
- Complete catalogs are compiled to `resources/i18n/vanta_<code>.qm`, committed,
  and embedded in the binary through `resources.qrc`. A release build therefore
  needs no Qt Linguist tooling, and the shipped packages carry the translations
  inside the executable.
- The language is stored in `settings.json` as a stable code (`system`, `en`,
  `ru`, `pt_BR`, `pt`, `es`), never as a display name, so changing the interface
  language cannot corrupt a saved setting.

## Languages

Translated and shipped:

| Code    | Language              |
| ------- | --------------------- |
| `ru`    | Russian               |
| `pt_BR` | Portuguese (Brazil)   |
| `pt`    | Portuguese            |
| `es`    | Spanish               |

Scaffolds, kept up to date but not compiled into the binary until they are
complete:

| Code    | Language |
| ------- | -------- |
| `de`    | German   |
| `fr`    | French   |
| `it`    | Italian  |
| `pl`    | Polish   |
| `uk`    | Ukrainian |
| `zh_CN` | Chinese (Simplified) |
| `ja`    | Japanese |
| `ko`    | Korean   |

A scaffold becomes a shipped language only when it is complete: add its code and
native name to `I18n::languages()` in `src/core/I18n.cpp`, add the compiled
`.qm` to the shipped list in `scripts/update-translations.sh`, and add the file
to `resources.qrc` under the `/i18n` prefix.

## Improving an existing translation

1. Open `translations/vanta_<code>.ts` in Qt Linguist, or edit the XML directly.
2. Translate the messages under the `Vanta` context. Leave the source text
   (the `<source>` element) untouched.
3. Keep every `%1`, `%2`, ... placeholder, and keep the same count and order as
   the source.
4. Keep `&` accelerators. The letter after `&` should be unique inside a menu,
   but do not invent one if the source has none.
5. Do not translate product and brand names: Steam, Proton, Wine, GOG, Epic
   Games Store, EA App, Ubisoft Connect, OnlineFix, FreeTP, SteamFix, Photon,
   BepInEx, Thunderstore, r2modman.
6. Do not translate strings that are stored or compared rather than shown:
   settings values, file paths, command-line flags, environment variables and
   sizes such as `Small`/`Medium`/`Large`. Those are wrapped in `sourceDisplayName()`
   and friends, not left to the catalog.
7. Save the `.ts`, then regenerate the compiled catalogs with
   `scripts/update-translations.sh`.
8. Open a pull request that touches only the `.ts` (and, if you ran the script,
   the matching `.qm`). Nothing else changes.

Never add wording that claims a detected OnlineFix works or that a mod is
functional. Vanta reports what it detected and what it changed; a translation
must do the same.

## Adding a new language

1. Copy `translations/vanta_de.ts` to `translations/vanta_<code>.ts` and
   translate it.
2. When it is complete, add it to the shipped list in
   `scripts/update-translations.sh`, add the code and native name to
   `I18n::languages()` in `src/core/I18n.cpp`, and register the compiled file in
   `resources.qrc`.
3. Run `scripts/update-translations.sh` and commit the `.ts` and `.qm`.

## Testing a translation

Set the language in Settings, or launch Vanta with a matching locale so
`System default` picks it up:

```bash
LANG=ru_RU.UTF-8 ./build/vanta
```

A catalog must load before the first window appears; that happens in
`src/main.cpp` through `I18n::install()`, which also loads Qt's own `qtbase`
catalog for the standard dialog buttons when the system provides one.
