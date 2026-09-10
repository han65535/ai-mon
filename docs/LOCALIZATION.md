# Languages and translation packs

AI Mon 0.2.0 embeds English and Korean in the same executable. No extra file is needed for either language.

## Choose a language

Open **Settings → Language**, choose **Automatic**, **English**, or **한국어**, then **Save**. The summary window, settings, tray menu, messages, and About dialog use the selected language. Changes apply immediately and survive restarts.

Automatic uses the current user's Windows display language, first matching a full language ID and then its base language. For example, `ko-KR` selects `ko`. When no matching pack is available, it uses English. Existing 0.1.0 settings default to Automatic without changing collection folders or cache data.

## Add a community language

1. Create `%LOCALAPPDATA%\AI Mon\languages` if it does not exist.
2. Copy `locales/en.json` from this repository into that folder under a new filename, such as `fr.json`.
3. Change `id` to a unique lowercase language ID and `name` to its native display name. Translate the values in `strings`, keeping the keys unchanged.
4. Save as UTF-8, reopen Settings, and select the new language. Restarting the app also discovers packs.

With `--data-dir`, the `languages` folder belongs under that custom data folder instead. Packs are per-user and are preserved when uninstalling the app.

This is a valid partial example, not a complete French translation:

```json
{
  "schema": 1,
  "id": "fr",
  "name": "Français",
  "strings": {
    "action.open": "Ouvrir",
    "action.cancel": "Annuler",
    "summary.updated": "{status} · Mise à jour il y a {seconds}s"
  }
}
```

Missing translations fall back to English per key. Keep named placeholders such as `{status}`, `{seconds}`, `{read}`, `{write}`, `{version}`, and `{language}` exactly as in English; they may be reordered. A translated value with incorrect placeholders is ignored. `&` marks a Win32 keyboard shortcut in button/menu text; `\n` creates a new line.

Use unique IDs of at most 35 lowercase ASCII letters, digits, and hyphens (for example, `fr` or `pt-br`). IDs cannot begin/end with a hyphen or contain consecutive hyphens. `auto` is reserved, and external packs cannot replace built-in `en` or `ko`. Duplicate IDs are rejected; only one file should define each ID.

The loader examines at most 32 top-level JSON files, each at most 64 KiB, with at most 256 string entries. Empty values, duplicate keys, invalid UTF-8, embedded NUL characters, and unsupported schemas are rejected. Unknown keys are ignored with a warning. Language files contain text only and run no code. Long translations can be truncated by the fixed Windows controls; check layout at your intended display scaling before distributing a pack. This release does not implement right-to-left layout or localized number/date formats.

If a previously selected pack is removed or invalid, the app displays English, retains the saved language ID, and reports the fallback. Reinstall the pack and reopen Settings/Save, or choose an available language.

## Built-in translations and architecture

| File | Purpose |
| --- | --- |
| `locales/en.json` | Canonical application keys and fallback text |
| `locales/ko.json` | Complete Korean translation |
| `src/localization.hpp`, `src/localization.cpp` | Resource loading, pack discovery, selection, and named placeholders |
| `resources/app.rc` | Embeds both JSON files as resources 201/202 |
| `settings.json` → `language` | `auto`, `en`, `ko`, or an external pack ID |
| `scripts/check-locales.ps1` | Build-time key and placeholder parity checks |

UI code uses translation keys, while collection status remains a language-independent enum. Starting in 0.2.0, `--scan` JSON emits stable English status codes: `disabled`, `collecting`, `ready`, `empty`, `missing`, `read_error`, `unsupported`, `partial`, and `unknown`. Consumers of the old Korean status text should update accordingly.

To change a built-in translation, edit its source JSON and rebuild. Adding another built-in language also requires adding a resource and registering it in `Localizer::initialize`; community packs need no compilation. The 1,000,000-byte release check includes all embedded translations. Optional community files add their own size outside the EXE/MSI.

## Installer language

```powershell
.\scripts\package.ps1 -Language en
.\scripts\package.ps1 -Language ko
```

These produce `AI-Mon-0.3.4-x64-en.msi` and `AI-Mon-0.3.4-x64-ko.msi`. Both install the same bilingual EXE. Choose one installer; they represent the same product/version and should not be installed side by side. The setup wizard's language does not override the app's saved selection or Windows-language default. Use the original MSI for repair.

Installer strings live separately in `installer/ui.en.json` and `installer/ui.ko.json`. Community app packs do not translate Windows Installer itself.
