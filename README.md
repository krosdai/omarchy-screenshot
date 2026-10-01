# Omarchy Screenshot

English | [Simplified Chinese](README.zh.md)

A Qt 6 screenshot and annotation tool for Hyprland and Omarchy. It captures each monitor before displaying a frozen overlay across all monitors. Selections use Hyprland's global coordinates, so you can drag from one screen to another.

## Features

- Hover to detect windows in the current workspace and the monitor beneath the pointer, then click to select the highlighted region.
- Drag to select any rectangle, including across monitors. Move or resize the selection afterward.
- The selection tool (V) shows eight square resize handles at the corners and edge midpoints, each with a generous hit area. Arrow keys expand the corresponding edge by 1 px; Shift+arrow keys shrink it by 1 px. Hold a key to keep adjusting. Handles disappear when you switch tools.
- Annotate with rectangles, ellipses, arrows, a pen, text, rectangular mosaic redaction, lines, a highlighter, spotlights, numbered markers, rounded or filled shapes, and curved or double-headed arrows. Undo and redo are supported. Mosaic redaction shows a temporary border while you drag; the border disappears on release.
- Rectangles, ellipses, arrows, and pens each have a primary toolbar button. Lines belong to the arrow group; spotlights belong to the ellipse group. Click a group button to choose a style from an icon-only secondary toolbar, or start drawing in a blank area to dismiss it automatically. Each group button shows its current style, which its keyboard shortcut recalls.
- Pen strokes filter mouse jitter and connect sampled points with smooth quadratic Bézier curves. Preview and export use the same path.
- The text editor has a transparent background and a dashed border, with vertically centered lines. Use Shift+Enter for a new line.
- Choose a drawing color from the leftmost toolbar button. Select a preset or drag the palette's hue slider and color area to pick any color. The app remembers your choice between launches. New shapes, pen strokes, text, and temporary mosaic borders use the new color; existing annotations retain theirs.
- The toolbar uses thin-line icons inspired by QQ's screenshot tool. Switch between light and dark themes at the top of the palette; the app remembers your choice. Selected buttons leave space between the highlight, shortcut, and icon.
- Shortcuts are within easy reach of the left hand on a QWERTY keyboard. Hover over toolbar icons for descriptions. OCR remains available through F but has no toolbar button.
- Copy a PNG to the Wayland clipboard or save it to your Pictures directory. Set `OMARCHY_SCREENSHOT_DIR` to use a different save directory.
- Tooltips, tool names, theme names, and error messages follow the system language. Long text wraps, and Hebrew uses right-to-left layout.
- Optional OCR copies recognized text to the clipboard when `tesseract` and the appropriate `tesseract-data-*` packages are installed. It prefers the current interface language and includes English if its data is installed. If data for the selected language is unavailable, it falls back to English.
- Supports negative coordinates, rotated monitors, and mixed scaling. Export combines the images from each monitor.

## Interface languages

As of September 30, 2026, Omarchy's upstream [installation options](https://github.com/omacom/omarchy/blob/8b4eae66da2938ba9559f103b18dbf85cdf28a70/install/provisioning/setup-form.sh#L32-L102) offer 48 **keyboard layouts**, not interface languages. The installer defaults to an English system locale. This tool covers the languages associated with those layouts, plus Simplified and Traditional Chinese: 42 language or regional variants in total, comprising the English source text and 41 translations. Changing the keyboard layout does not change the interface language.

By default, the app uses Qt's system interface language preferences, honoring `LANGUAGE`, `LC_ALL`, `LC_MESSAGES`, and `LANG`. You can also select a language for this app without changing your system settings:

```sh
omarchy-screenshot --language en
omarchy-screenshot --language zh_CN
omarchy-screenshot --language de
OMARCHY_SCREENSHOT_LANGUAGE=ja omarchy-screenshot
```

Language selection takes precedence in this order: `--language` > `OMARCHY_SCREENSHOT_LANGUAGE` > system language preferences. The app selects its language at startup; changes take effect on the next launch. Qt language tags such as `pt-BR` and `zh-Hant` are supported. If no matching translation is available, the app falls back to English. Shortcuts remain the same in every language.

| Language | Code | Language | Code |
| --- | --- | --- | --- |
| English | `en` | Azerbaijani | `az` |
| Belarusian | `be` | Bulgarian | `bg` |
| Croatian | `hr` | Czech | `cs` |
| Danish | `da` | Dutch | `nl` |
| Estonian | `et` | Finnish | `fi` |
| French | `fr` | Georgian | `ka` |
| German | `de` | Greek | `el` |
| Hebrew | `he` | Hungarian | `hu` |
| Icelandic | `is` | Irish | `ga` |
| Italian | `it` | Japanese | `ja` |
| Kazakh | `kk` | Kyrgyz | `ky` |
| Lao | `lo` | Latvian | `lv` |
| Lithuanian | `lt` | Macedonian | `mk` |
| Norwegian (Bokmål) | `nb` | Polish | `pl` |
| Portuguese (Portugal) | `pt_PT` | Portuguese (Brazil) | `pt_BR` |
| Romanian | `ro` | Russian | `ru` |
| Serbian (Cyrillic) | `sr` | Slovak | `sk` |
| Slovenian | `sl` | Spanish | `es` |
| Swedish | `sv` | Tajik | `tg` |
| Turkish | `tr` | Ukrainian | `uk` |
| Simplified Chinese | `zh_CN` | Traditional Chinese | `zh_TW` |

British English uses the English source text. Canadian and Swiss French, Swiss German, and Latin American Spanish use the French, German, and Spanish translations, respectively. Portuguese and Chinese have separate regional or script variants. Fonts are provided by the system; `noto-fonts` and `noto-fonts-cjk` are recommended.

Translations use Qt Linguist's `.ts` format in `translations/`. The build compiles them into `.qm` resources embedded in the executable, so you do not need to copy the translation directory when installing. After adding or changing interface text, run `cmake --build build --target update_translations` to update the catalogs, then complete each translation. Do not translate `%1` placeholders, application names, or shortcuts.

## Build and run

On Arch Linux or Omarchy, you need `cmake`, `gcc`, `qt6-base`, `qt6-declarative`, `qt6-wayland`, `qt6-tools`, `layer-shell-qt`, `grim`, `wl-clipboard`, and `hyprland`. Building the automated tests, which are enabled by default, also requires `python`. Pass `-DBUILD_TESTING=OFF` to build only the app.

To install from the AUR on Omarchy:

```sh
omarchy pkg aur add omarchy-screenshot
```

The AUR package downloads the source from the corresponding GitHub release tag, then builds and installs `omarchy-screenshot`. For OCR, optionally install `tesseract` and the language data you need, such as `tesseract-data-eng` (English), `tesseract-data-deu` (German), `tesseract-data-chi_sim` (Simplified Chinese), or `tesseract-data-chi_tra` (Traditional Chinese).

After installation, you can bind Ctrl+Alt+A to the app in `~/.config/hypr/bindings.lua`. Remove any existing binding for the same shortcut first:

```lua
hl.unbind("CTRL + ALT + A")
o.bind("CTRL + ALT + A", "Omarchy Screenshot", "omarchy-screenshot")
```

To build and run from source:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/omarchy-screenshot
```

Run `ctest --test-dir build --output-on-failure` to check translation completeness, placeholders, embedded catalog loading, language precedence and fallback, and OCR language selection. These tests do not require a desktop session.

With at least two monitors connected, run `./build/omarchy-screenshot --self-test` to check cross-monitor selection, undo and redo, and numbered markers in memory, without displaying overlays or saving images.

Run `./build/omarchy-screenshot --ui-self-test --language he` to briefly display the overlays, simulate toolbar and palette interactions, switch light and dark themes, and draw mosaic redactions and pen strokes. It checks saved preferences, selection padding, previews, exported images, text input, and translated layouts on a narrow screen, then exits automatically. The self-test uses a temporary settings directory and does not change your saved color or theme. Set `OMARCHY_SCREENSHOT_TEST_ARTIFACT_DIR` to export cropped UI screenshots in both themes; these screenshots exclude captured desktop pixels.

To install locally, run `cmake --install build --prefix ~/.local`. The executable is installed at `~/.local/bin/omarchy-screenshot`. The app requires a Hyprland Wayland session.

### AUR publishing

The root [PKGBUILD](PKGBUILD) defines the AUR package. `.github/workflows/publish-aur.yml` follows the publishing approach used by lazycat-terminal: pushing a `vMAJOR.MINOR.PATCH` tag triggers GitHub Actions to update the package version and source checksum, then commit to the AUR. Before the first release, configure the `AUR_USERNAME`, `AUR_EMAIL`, and `AUR_SSH_PRIVATE_KEY` GitHub Actions secrets, and add the corresponding public key to the AUR account.

## Controls

| Action | Result |
| --- | --- |
| Hover, then click | Select a window or monitor |
| Drag | Select a rectangle; move or resize an existing selection |
| Double-click a blank area inside the selection | Copy the current screenshot to the clipboard and close the app |
| V / T / G / B / W | Selection / text / mosaic redaction / numbered marker / line |
| R / E / A / D | Recall the last rectangle / ellipse / arrow (including line) / pen (including highlighter) style; click a group button to open its icon toolbar |
| Shift+B | Spotlight |
| Shift+R / Shift+D / Shift+E | Rounded rectangle / filled rectangle / filled ellipse |
| Shift+A / Shift+W | Curved arrow / double-headed curved arrow |
| Q / leftmost color button | Choose a preset or drag in the palette to pick a color; saved automatically |
| Sun / moon icons at the top of the palette | Switch to the light / dark toolbar theme; saved automatically |
| Arrow keys / Shift+arrow keys in V mode | Expand / shrink the corresponding edge by 1 px; hold to repeat |
| Click with the text tool | Type text; Shift+Enter inserts a new line, Enter or clicking elsewhere confirms |
| Alt with the text tool | Confirm the text and return to the previous tool |
| Z | Undo the last annotation |
| X | Redo an undone annotation |
| C | Copy the screenshot and exit |
| S | Save the screenshot and exit |
| F | Recognize text in the selection, copy it, and exit |
| Esc / right-click | Exit |

Window detection selects a geometric region on the screen; the screenshot contains the pixels visible there at capture time. If another window obscures part of the selected window, the screenshot includes that overlapping window. This is not the same as exporting an individual window's buffer.

## License

Copyright (C) 2026 Andy Stewart. The source code is licensed under the GNU General Public License, version 3 only (`GPL-3.0-only`). See [LICENSE](LICENSE) for the full text.
