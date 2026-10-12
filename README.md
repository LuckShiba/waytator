!! this is still maintained, i just need issues to work on. if you face issues or have feature requests, don't hesitate to open issues and ping me !!

# waytator

`waytator` is a screenshot annotator and lightweight image editor

`niri` user? Check below on how to integrate niri screenshots with this tool

<p  align="center">
<img width="600" alt="Screenshot from 2026-04-19 03-04-52" src="https://github.com/user-attachments/assets/4bcc1c58-5834-4f35-877b-5f94a914d6ce" />
</p>
<p  align="center">
<img width="600" alt="image" src="https://github.com/user-attachments/assets/91c949f3-f106-433f-a1d6-62742a72831d" />
</p>

## Build & Install

### Arch

Get it from the AUR:

```bash
yay -S waytator
```

### From source

1) Required build dependencies:

- `meson`
- `ninja`
- `pkg-config`
- `gtk4`
- `libadwaita-1`
- a C compiler such as `gcc` or `clang`

2) Required runtime dependency:

- `wl-clipboard`, so a copied image survives the app closing (close on copy) and for niri screenshot clipboard support

3) Recommended runtime dependency:

- `tesseract` for OCR support

4) Install into `~/.local`:

```bash
meson setup build --buildtype=release --prefix="$HOME/.local" # Override the prefix if needed
meson compile -C build
meson install -C build
```

This installs:

- the `waytator` binary to `${HOME}/.local/bin/waytator`
- the desktop entry to `${HOME}/.local/share/applications/dev.faetalize.waytator.desktop`
- the app icon to `${HOME}/.local/share/icons/hicolor/scalable/apps/dev.faetalize.waytator.svg`

The app's identifier is `dev.faetalize.waytator` for concerns such as the desktop entry and icon, so if you have an older version of the app installed with a different identifier (e.g. `dev.waytator.Waytator`), make sure to remove it first to avoid conflicts.


If `~/.local/bin` is not already on your `PATH`, add it in your shell profile. For example, add the following line to `~/.bashrc`:

```bash
export PATH="$HOME/.local/bin:$PATH"
```

If launchers fail to start `waytator` after installing from source, make sure your graphical session includes `~/.local/bin` in `PATH`, not just your shell configuration.

## Usage

Read from stdin automatically when image data is piped in:

```bash
grim -g "$(slurp)" - | waytator
```

Or pass stdin explicitly:

```bash
grim -g "$(slurp)" - | waytator --stdin
```

Set the default display/save-as name for stdin images:

```bash
grim -g "$(slurp)" - | waytator --stdin --name "Screenshot.png"
```

Open an existing image:

```bash
waytator path/to/image.png
```

It can also be run independently:

```bash
waytator
```

### Closing after copy or save

Enable "Close on copy" in the preferences to make the copy button and copy shortcut close the app once the image is on the clipboard. A "Copy without closing" button (default shortcut `Ctrl+Shift+C`) appears next to it for when you want to keep editing. Auto-copy never closes the app.

"Save and close" and "Save, copy and close" have no shortcut by default; assign one in the preferences. Images that did not come from a file open the save dialog first.

Shortcuts can be a key, a key combination, or a right click on the canvas, and each one can be unassigned in the preferences. Closing the window (default `Escape`), copying (default `Ctrl+C`) and saving (default `Ctrl+S`) are assignable the same way.

All of this can be overridden for a single launch, which is handy in screenshot scripts:

```bash
grim -g "$(slurp)" - | waytator --close-on-copy
waytator --no-close-on-copy path/to/image.png
grim -g "$(slurp)" - | waytator --save-copy-and-close-shortcut right-click
```

| Option | Effect |
| --- | --- |
| `--close-on-copy`, `--no-close-on-copy` | Override the "Close on copy" preference |
| `--close-window-shortcut SHORTCUT` | Override the "Close window" shortcut |
| `--copy-shortcut SHORTCUT` | Override the copy shortcut |
| `--copy-without-closing-shortcut SHORTCUT` | Override the "Copy without closing" shortcut |
| `--save-shortcut SHORTCUT` | Override the "Save" shortcut |
| `--save-and-close-shortcut SHORTCUT` | Override the "Save and close" shortcut |
| `--save-copy-and-close-shortcut SHORTCUT` | Override the "Save, copy and close" shortcut |

`SHORTCUT` is a key or key combination such as `s` or `<Control><Shift>s`, `right-click`, or `none`.

If you're on niri, just bind your screenshot keybind to `./scripts/screenshot-to-waytator.sh`, this will automatically open screenshots into `waytator` after they are captured for editing. Saving from niri's screenshot UI opens the saved file; pressing `Ctrl+C` in the screenshot UI opens the copied image directly from the clipboard. Clipboard captures use a timestamped name by default; override it with `WAYTATOR_SCREENSHOT_NAME`.

```kdl
// in your config.kdl, in the binds section
Print { spawn <path-to-screenshot-to-waytator.sh>; }
Mod+Shift+S { spawn <path-to-screenshot-to-waytator.sh>; }
```

I recommend adding a window rule to float `waytator` windows for better experience, for example with `niri`:

```kdl
// in your config.kdl
window-rule {
    match app-id=r#"^dev\.faetalize\.waytator$"#
    open-floating true
}
```

## License

GPL-3.0-or-later.
