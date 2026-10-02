# Omarchy Screenshot

English | [Simplified Chinese](README.zh.md)

A Qt 6 screenshot and annotation tool for Hyprland and Omarchy, with frozen overlays and cross-monitor selections.

## Features

- Quickly select a window, a monitor, or any region, across screens, then adjust the selection.
- Annotate with shapes, arrows, a pen, text, and numbered markers, hide private details with mosaic, and undo or redo.
- Capture long pages by scrolling, with automatic stitching; continue capturing and annotate the long image.
- Copy or save in one click, or double-click the selection to copy it.
- Customize the annotation color and the light or dark theme; the app remembers your preferences.
- Use it in 42 language or regional variants, with multiple monitors, mixed scaling, and rotated screens.

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
| S | Save the screenshot to your Pictures directory, or to `$OMARCHY_SCREENSHOT_DIR` if set, and exit |
| Esc / right-click | Exit |

Window captures include the pixels visible in the selection, including overlapping windows.

## Interface languages

Supports 42 language or regional variants and follows the system interface language by default. Keyboard layouts do not affect the interface language. To override it:

```sh
omarchy-screenshot --language zh_CN
OMARCHY_SCREENSHOT_LANGUAGE=ja omarchy-screenshot
```

Priority: `--language` > `OMARCHY_SCREENSHOT_LANGUAGE` > system preferences (`LANGUAGE`, `LC_ALL`, `LC_MESSAGES`, `LANG`). Selection takes effect at startup, accepts tags such as `pt-BR` and `zh-Hant`, and falls back to English when no translation matches. Shortcuts stay the same. See [translations/](translations/) for available languages; `noto-fonts` and `noto-fonts-cjk` are recommended for font coverage.

Qt Linguist `.ts` translations are embedded during the build. After changing interface text, run `cmake --build build --target update_translations` and complete the translations, preserving `%1` placeholders, application names, and shortcuts.

## Build and run

Requires a Hyprland Wayland session; supports `x86_64` and `aarch64`. Install from the AUR on Omarchy:

```sh
omarchy pkg aur add omarchy-screenshot
```

Set a screenshot shortcut in `~/.config/hypr/bindings.lua`:

```lua
hl.unbind("CTRL + ALT + A")
o.bind("CTRL + ALT + A", "Omarchy Screenshot", "omarchy-screenshot")
```

Building from source requires `cmake`, `gcc`, `pkgconf`, `qt6-base`, `qt6-declarative`, `qt6-wayland`, `qt6-tools`, `layer-shell-qt`, `wayland`, `wayland-protocols`, `grim`, `wl-clipboard`, and `hyprland`. Tests also require `python` by default; disable them with `-DBUILD_TESTING=OFF`.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/omarchy-screenshot
```

Install locally, configuring the service path for resident mode:

```sh
cmake -S . -B build -DCMAKE_INSTALL_PREFIX=$HOME/.local -DSYSTEMD_USER_UNIT_DIR=$HOME/.local/share/systemd/user
cmake --build build -j
cmake --install build
```

### Tests

`ctest --test-dir build --output-on-failure` checks translations, scroll stitching, the daemon, and image sampling at different scales without a desktop session. Run these additional checks with `./build/omarchy-screenshot` and the listed options:

| Option | Checks |
| --- | --- |
| `--self-test` | Cross-monitor selection, undo/redo, and markers; requires at least two monitors |
| `--ui-self-test --language he` | Toolbar, themes, annotations, export, and translated layouts; uses temporary settings |
| `--scroll-stitch-test` | Scroll stitching |
| `--scroll-ui-self-test` | Long-image interface |

### Resident mode

Off by default. Reusing Qt and GPU initialization reduces overlay startup on a 5K display from about 230 ms to 60–90 ms; the first capture still needs a normal startup. The daemon exits after 10 idle minutes and uses about 100 MB of RAM plus 320 MB of GPU memory while running.

```sh
systemctl --user enable --now omarchy-screenshot.socket   # Enable
systemctl --user disable --now omarchy-screenshot.socket  # Disable and stop the daemon
```

Without systemd, add `omarchy-screenshot --daemon` to your session's autostart. Keep the same shortcut; only launches without options use the daemon. `--language` and self-tests run independently.

Run `systemctl --user edit omarchy-screenshot.service` to change the idle timeout (seconds; `0` means no timeout), keeping the actual installation path:

```ini
[Service]
ExecStart=
ExecStart=/usr/bin/omarchy-screenshot --daemon --idle-timeout 1800
```

Omarchy's overlay fade-in can add up to 400 ms. For immediate display, add this rule to `~/.config/hypr/hyprland.lua`:

```lua
hl.layer_rule({ match = { namespace = "^omarchy-screenshot$" }, no_anim = true, animation = "none" })
```

### AUR publishing

[PKGBUILD](PKGBUILD) defines the package. Pushing a `vMAJOR.MINOR.PATCH` tag triggers [GitHub Actions](.github/workflows/publish-aur.yml) to update the version and checksum and commit to the AUR. Before the first release, configure the `AUR_USERNAME`, `AUR_EMAIL`, and `AUR_SSH_PRIVATE_KEY` secrets and add the public key to your AUR account.

## License

Copyright (C) 2026 Andy Stewart. Source code: [GPL-3.0-only](LICENSE). Bundled [virtual pointer protocol](protocols/wlr-virtual-pointer-unstable-v1.xml): MIT.
