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

`meson` - `ninja` - `pkg-config` - `gtk4` - `libadwaita-1` - `gcc`/`clang`/any C compiler

2) Required runtime dependency:

`wl-clipboard` for clipboard support

3) Recommended runtime dependency:

`tesseract` for OCR support

4) Install into `~/.local`:

```bash
meson setup build --buildtype=release --prefix="$HOME/.local" # Override the prefix if needed
meson compile -C build
meson install -C build
```
DONE! Waytator is now installed and is usually detected by launchers instantly.

Result:

- the `waytator` binary: `${HOME}/.local/bin/waytator`
- the desktop entry: `${HOME}/.local/share/applications/dev.faetalize.waytator.desktop`
- the app icon: `${HOME}/.local/share/icons/hicolor/scalable/apps/dev.faetalize.waytator.svg`

The app's identifier is `dev.faetalize.waytator`. If you have an older version of the app installed with a different identifier (e.g. `dev.waytator.Waytator`), make sure to remove it first to avoid conflicts.

### Troubleshooting

If `~/.local/bin` is not already on your `PATH`, add it in your shell profile. For example, add the following line to `~/.bashrc`:

```bash
export PATH="$HOME/.local/bin:$PATH"
```

If launchers fail to start `waytator` after installing from source, make sure your graphical session includes `~/.local/bin` in `PATH`, *not just your shell configuration*.

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

Common app features are exposed as command-line options; run `waytator --help` to list them.

### Writing shortcuts

Options that take a shortcut accept:

- a single key: `s`, `F5`, `escape`
- modifiers (`ctrl`, `shift`, `alt`, `super`) and a key joined with `+`: `ctrl+shift+s`
- `right-click`
- `none` to unassign
- GTK Accelerator-like shortcut schemes also work :p

### Tool shortcuts

Each tool has a key, shown in the corner of its button. `[` and `]` change the size, number keys pick a stroke color from a palette you can edit. All of them can be reassigned in Preferences → Shortcuts.

### Niri users

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
