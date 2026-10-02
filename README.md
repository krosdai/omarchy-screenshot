# Omarchy Screenshot

English | [Simplified Chinese](README.zh.md)

A Qt 6 screenshot and annotation tool for Hyprland and Omarchy. It captures each monitor before displaying a frozen overlay across all monitors. Selections use Hyprland's global coordinates, so you can drag from one screen to another.

## Features

- Hover to detect windows in the current workspace and the monitor beneath the pointer, then click to select the highlighted region.
- Drag to select any rectangle, including across monitors. Move or resize the selection afterward.
- The selection tool (V) shows eight square resize handles at the corners and edge midpoints, each 9×9 px with a 31×31 px hit area. Regular screenshots and long images share the same handles; on a long image they neither scale with zoom nor get clipped by the image. Arrow keys expand the corresponding edge by 1 px; Shift+arrow keys shrink it by 1 px. Hold a key to keep adjusting. Handles disappear when you switch tools.
- Annotate with rectangles, ellipses, arrows, a pen, text, rectangular mosaic redaction, lines, a highlighter, spotlights, numbered markers, rounded or filled shapes, and curved or double-headed arrows. Undo and redo are supported. Mosaic redaction shows a temporary border while you drag; the border disappears on release.
- Rectangles, ellipses, arrows, and pens each have a primary toolbar button. Lines belong to the arrow group; spotlights belong to the ellipse group. Click a group button to choose a style from an icon-only secondary toolbar, or start drawing in a blank area to dismiss it automatically. Each group button shows its current style, which its keyboard shortcut recalls.
- Pen strokes filter mouse jitter and connect sampled points with smooth quadratic Bézier curves. Preview and export use the same path.
- The text editor has a transparent background and a dashed border, with vertically centered lines. Use Shift+Enter for a new line.
- Choose a drawing color from the leftmost toolbar button. Select a preset or drag the palette's hue slider and color area to pick any color. The app remembers your choice between launches. New shapes, pen strokes, text, and temporary mosaic borders use the new color; existing annotations retain theirs.
- The toolbar uses thin-line icons inspired by QQ's screenshot tool. Switch between light and dark themes at the top of the palette; the app remembers your choice. Selected buttons leave space between the highlight, shortcut, and icon.
- Annotation shortcuts are within easy reach of the left hand on a QWERTY keyboard. H starts or continues a scrolling capture, and = and - zoom a long image. Toolbar buttons show their shortcut and icon; hover over them for descriptions.
- Neither regular screenshots nor scrolling captures include the mouse cursor. Copy a PNG to the Wayland clipboard or save it to your Pictures directory. Set `OMARCHY_SCREENSHOT_DIR` to use a different save directory.
- Before you annotate, click the scrolling capture button to capture a long image. With one window in the selection, capture starts immediately; with several, click the window to capture, or press Esc to cancel. The app captures only where that window and the selection overlap, scrolls with a virtual mouse wheel, and stitches the frames together. It avoids repeating fixed headers and footers, and keeps the long image captured so far if frames cannot be stitched reliably.
- During scrolling, no preview is shown. Click a blank area of the capture region to stop; moving the mouse does not stop it. The long image then fits on screen at no more than 4/5 of the screen height. After zooming in, drag inside the image with the selection tool (V) to scroll through it, while the border and handles still resize the selection and annotation tools still draw. You can also scroll with the mouse wheel, zoom with Ctrl+wheel, or drag with the middle button, then annotate, copy, or save. Continue capture extends the long image and keeps your annotations; if the window stops responding, click the area that actually scrolls, then click Continue scrolling.
- Regular screenshots and long images share the toolbar style, grouped icons, and palette. For regular screenshots, the scrolling capture button sits right of the numbered marker; for long images, zoom out, zoom in, and Continue capture sit between the numbered marker and undo.
- Tooltips, tool names, theme names, hints, and error messages follow the system language. Long text wraps, and Hebrew uses right-to-left layout.
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

On Arch Linux or Omarchy, you need `cmake`, `gcc`, `pkgconf`, `qt6-base`, `qt6-declarative`, `qt6-wayland`, `qt6-tools`, `layer-shell-qt`, `wayland`, `wayland-protocols`, `grim`, `wl-clipboard`, and `hyprland`. The app captures each display directly through the `ext-image-copy-capture-v1` protocol and falls back to `grim` when the compositor lacks the protocol or a display is rotated. Scrolling capture uses the wlr virtual pointer protocol that Hyprland exposes. Building the automated tests, which are enabled by default, also requires `python`. Pass `-DBUILD_TESTING=OFF` to build only the app.

Both `x86_64` and `aarch64` (64-bit ARM) are supported. On ARM, the same build commands produce a native ARM binary, and `PKGBUILD` declares both architectures, so `makepkg --ignorearch` is not needed.

To install from the AUR on Omarchy:

```sh
omarchy pkg aur add omarchy-screenshot
```

The AUR package downloads the source from the corresponding GitHub release tag, then builds and installs `omarchy-screenshot`.

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

Run `ctest --test-dir build --output-on-failure` to check translation completeness, placeholders, embedded catalog loading, and language precedence and fallback, along with scroll stitching, the resident daemon's socket handling and capture reset, and cursor-icon grabs and logical-pixel sampling at 1, 1.25, 1.5, and 2x scaling. These tests do not require a desktop session; the scaling tests use separate processes and software rendering and leave the desktop scale untouched. The cursor, mosaic, and pen preview checks in `--ui-self-test` also sample grabs at logical size, so it runs directly in a 2x Wayland session.

With at least two monitors connected, run `./build/omarchy-screenshot --self-test` to check cross-monitor selection, undo and redo, and numbered markers in memory, without displaying overlays or saving images.

Run `./build/omarchy-screenshot --ui-self-test --language he` to briefly display the overlays, simulate toolbar and palette interactions, switch light and dark themes, and draw mosaic redactions and pen strokes. It checks saved preferences, selection padding, previews, exported images, text input, and translated layouts on a narrow screen, then exits automatically. The self-test uses a temporary settings directory and does not change your saved color or theme. Set `OMARCHY_SCREENSHOT_TEST_ARTIFACT_DIR` to export cropped UI screenshots in both themes; these screenshots exclude captured desktop pixels.

Run `./build/omarchy-screenshot --scroll-stitch-test` to check frame stitching, and `./build/omarchy-screenshot --scroll-ui-self-test` to check the long-image interface.

To check toolbar interactions offline, including group menus, the palette, theme consistency, and button positions, run `QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME= QT_QUICK_BACKEND=software /usr/lib/qt6/bin/qmltestrunner -input tests`.

To install locally, run `cmake --install build --prefix ~/.local`. The executable is installed at `~/.local/bin/omarchy-screenshot`. To use resident mode from a local install, set the prefix when configuring instead, since the service records the executable's path then: `cmake -B build -DCMAKE_INSTALL_PREFIX=$HOME/.local -DSYSTEMD_USER_UNIT_DIR=$HOME/.local/share/systemd/user`, followed by `cmake --install build`. The app requires a Hyprland Wayland session.

### Resident mode

A normal launch starts Qt, loads the overlay and sets up the GPU before it can show anything. Resident mode keeps that work done between captures: `omarchy-screenshot` hands the request to a running daemon and exits at once. On a 5K display, the overlay then appears in about 60–90 ms instead of about 230 ms.

Resident mode is off by default: installing the package changes nothing, and `omarchy-screenshot` captures on its own as before. To turn it on, enable the systemd user socket the package installs; it starts the daemon on the first capture:

```sh
systemctl --user enable --now omarchy-screenshot.socket
```

The first capture after the daemon starts takes about as long as a normal launch. The daemon then stays ready and exits after 10 minutes without a capture, which returns all of its memory; the next capture starts it again. While it runs, it uses about 100 MB of memory plus about 320 MB of GPU memory on a 5K display, which integrated graphics take from system RAM.

To change how long the daemon stays ready, run `systemctl --user edit omarchy-screenshot.service` and replace the command, keeping the executable path the unit already uses (`/usr/bin` for the package, `~/.local/bin` for a local install); `--idle-timeout 0` keeps it running:

```ini
[Service]
ExecStart=
ExecStart=/usr/bin/omarchy-screenshot --daemon --idle-timeout 1800
```

Without systemd, start `omarchy-screenshot --daemon` from your session's autostart instead. The key binding stays the same in every case: when no daemon is running, `omarchy-screenshot` captures on its own. Only a launch without options goes through the daemon; `--language` and the self-tests always run standalone, and the daemon keeps the language it started with.

To turn resident mode off, run `systemctl --user disable --now omarchy-screenshot.socket`, which also stops a running daemon, or remove `omarchy-screenshot --daemon` from your autostart.

Omarchy fades new overlay layers in by default, which delays the overlay by up to 400 ms in either mode. To show it immediately, add this rule to `~/.config/hypr/hyprland.lua`:

```lua
hl.layer_rule({ match = { namespace = "^omarchy-screenshot$" }, no_anim = true, animation = "none" })
```

### AUR publishing

The root [PKGBUILD](PKGBUILD) defines the AUR package. `.github/workflows/publish-aur.yml` follows the publishing approach used by lazycat-terminal: pushing a `vMAJOR.MINOR.PATCH` tag triggers GitHub Actions to update the package version and source checksum, then commit to the AUR. Before the first release, configure the `AUR_USERNAME`, `AUR_EMAIL`, and `AUR_SSH_PRIVATE_KEY` GitHub Actions secrets, and add the corresponding public key to the AUR account.

## Controls

| Action | Result |
| --- | --- |
| Hover, then click | Select a window or monitor |
| H / scrolling capture button, before annotating | Start a scrolling capture; with several windows, choose one first |
| Click a blank area of the capture region while scrolling | Stop and open the long image; Continue capture can extend it later |
| H / Continue capture button on a long image | Continue capturing downward, keeping annotations |
| = / - on a long image | Zoom in / zoom out |
| Drag inside a zoomed long image with the V tool | Scroll through the image; the border and handles still resize the selection |
| Wheel / Ctrl+wheel / middle-button drag on a long image | Scroll / zoom / pan |
| Drag | Select a rectangle; move or resize an existing selection |
| Double-click a blank area inside the selection | Copy the selection and its annotations to the clipboard and close the app; long images export at full resolution regardless of zoom or scroll position |
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
| Esc / right-click | Exit |

Window detection selects a geometric region on the screen; the screenshot contains the pixels visible there at capture time. If another window obscures part of the selected window, the screenshot includes that overlapping window. This is not the same as exporting an individual window's buffer.

## License

Copyright (C) 2026 Andy Stewart. The source code is licensed under the GNU General Public License, version 3 only (`GPL-3.0-only`). See [LICENSE](LICENSE) for the full text. The bundled [virtual pointer protocol definition](protocols/wlr-virtual-pointer-unstable-v1.xml) is licensed under the MIT License, whose text is included in that file.
