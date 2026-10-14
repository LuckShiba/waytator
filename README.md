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

### Nix

Waytator is in nixpkgs, though it can lag a release or two behind:

```bash
nix run nixpkgs#waytator
```

To get new releases the day they're out, use this repo's flake:

```bash
nix run github:faetalize/waytator/stable
```

To follow `master` instead:

```bash
nix run github:faetalize/waytator
```

On NixOS, add it as a flake input:

```nix
# flake.nix
inputs.waytator = {
  url = "github:faetalize/waytator/stable";
  inputs.nixpkgs.follows = "nixpkgs";
};
```

Then add the package to your system:

```nix
# configuration.nix
environment.systemPackages = [
  inputs.waytator.packages.${pkgs.stdenv.hostPlatform.system}.default
];
```

OCR is included. For a smaller install without it, use `.default.override { withOcr = false; }` instead of `.default`.

### From source

1) Required build dependencies:

`meson` - `ninja` - `pkg-config` - `gtk4` - `libadwaita-1` - `gtk4-layer-shell` - `wayland` - `wayland-protocols` - `gcc`/`clang`/any C compiler

2) Required runtime dependencies:

`wl-clipboard` for clipboard support - `adwaita-icon-theme` for icons (most distributions install it with `gtk4`)

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

Take a screenshot and open it for editing:

```bash
waytator --screenshot          # drag to select an area
waytator --screenshot screen   # a whole screen
```

The screen freezes while you choose, and `Esc` or a right click cancels. With more than one screen, `--screenshot screen` lets you click the one you want, and `--screenshot --screen DP-1` takes that one straight away. The same captures are available inside the app, from the camera button next to Open.

To get a menu or a tooltip on screen first, add `--delay 3` to wait that many seconds before the screen freezes (whole seconds, up to 300). Once the screen is frozen, Delay in the bar at the bottom waits 1, 3, 5 or 10 seconds and freezes it again. In the app, Delay under Capture, or the arrow next to the camera button, sets a delay for the captures started there.

Add `--notify` to take the screenshot without opening the editor:

```bash
waytator --screenshot --notify
```

The screenshot is saved to `~/Pictures/Screenshots`, copied to the clipboard, and a notification offers to open or edit it. A screenshot that opens in the editor is saved and copied the same way as soon as it is taken, and saving your edits updates that file. The folder and the clipboard copy can be changed or turned off in Preferences → Screenshots.

Screenshots are tested on niri and GNOME. Compositors that support `wlr-screencopy` (niri, sway and other wlroots-based ones) are captured directly, and the others go through the desktop's screenshot portal, which may ask for permission the first time.

On GNOME, waytator opens GNOME's own screenshot UI instead of its overlay. GNOME saves and copies the screenshot as usual, and waytator opens it for editing, or with `--notify` offers to. Area, screen or window is picked in GNOME's UI, a delay waits before it opens, and `--screen` doesn't support GNOME.

Set `WAYTATOR_CAPTURE_BACKEND` to `screencopy`, `portal` or `gnome` to choose one.

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

### Compositor keybinds

Bind a key to `waytator --screenshot` to capture and annotate in one step. For example with `niri`:

```kdl
// in your config.kdl, in the binds section
Print { spawn "waytator" "--screenshot"; }
Shift+Print { spawn "waytator" "--screenshot" "screen"; }
// or save, copy and notify without opening the editor
Ctrl+Print { spawn "waytator" "--screenshot" "--notify"; }
```

### Niri users

If you would rather keep niri's own screenshot UI, bind your screenshot keybind to `./scripts/screenshot-to-waytator.sh` instead, this will automatically open screenshots into `waytator` after they are captured for editing. Saving from niri's screenshot UI opens the saved file; pressing `Ctrl+C` in the screenshot UI opens the copied image directly from the clipboard. Clipboard captures use a timestamped name by default; override it with `WAYTATOR_SCREENSHOT_NAME`.

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
