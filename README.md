# labwc-next

This is an unofficial fork of [labwc](https://github.com/labwc/labwc). It is
not affiliated with or endorsed by the upstream project.

## What Is This?

labwc-next is a [wlroots]-based window-stacking compositor for [Wayland]. It
diverges from the upstream labwc philosophy to pursue different design goals.

## What's Different?

- Configuration uses TOML instead of XML — no `rc.xml`, `menu.xml`, or
  `themerc`; all `*.toml` files in the config directory are loaded
- Config and theme format is designed for readability and easy extension

## Build

    meson setup build/
    meson compile -C build/

Dependencies: wlroots, wayland, libinput, xkbcommon, cairo, pango, glib-2.0,
libpng. Optional: librsvg >=2.46, libsfdo, xwayland/xcb.

## Configuration

Config files are located at `~/.config/labwc-next/`. All `*.toml` files in
that directory are loaded.

See [docs/config.toml](docs/config.toml) for a full reference.

Run `labwc-next --reconfigure` to reload configuration and theme.

## Usage

    ./build/labwc-next [-s <command>]

Default key bindings:

| combination              | action
| ------------------------ | ------
| `alt`-`tab`              | activate next window
| `alt`-`shift`-`tab`      | activate previous window
| `super`-`return`         | lab-sensible-terminal
| `alt`-`F4`               | close window
| `super`-`a`              | toggle maximize
| `super`-`mouse-left`     | move window
| `super`-`mouse-right`    | resize window
| `super`-`arrow`          | resize window to fill half the output
| `alt`-`space`            | show the window menu

A root-menu can be opened by clicking on the desktop.

---

Upstream: https://github.com/labwc/labwc

[Wayland]: https://wayland.freedesktop.org/
[wlroots]: https://gitlab.freedesktop.org/wlroots/wlroots
