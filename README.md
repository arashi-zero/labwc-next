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
- Unix socket IPC for external tools (e.g. Quickshell) to query and control workspaces
- SVG button icon overrides via `~/.config/labwc-next/icons/` — drop a file, it wins over any theme
- Per-button size controls: `icon-size`, `hover-bg-size`, `app-icon-size` — all independently centered; hover background renders as a separate scene node behind the icon
- Extended theming surface, see [docs/config.toml](docs/config.toml) for the full list

## Build

    meson setup build/
    meson compile -C build/

Dependencies: wlroots, wayland, libinput, xkbcommon, cairo, pango, glib-2.0,
libpng, librsvg >=2.46. Optional: libsfdo, xwayland/xcb.

## Configuration

Config files are located at `~/.config/labwc-next/`. All `*.toml` files in
that directory are loaded.

See [docs/config.toml](docs/config.toml) for a full reference.

Run `labwc-next --reconfigure` to reload configuration and theme.

### Button Icons

Place SVG files in `~/.config/labwc-next/icons/` to override the titlebar
buttons. A single file is used for both active and inactive states; it is
recolored by the button color defined in your theme.

| File              | Button     |
| ----------------- | ---------- |
| `close.svg`       | Close      |
| `max.svg`         | Maximize   |
| `max_toggled.svg` | Restore    |
| `iconify.svg`     | Minimize   |
| `menu.svg`        | Window menu |
| `shade.svg`       | Shade      |
| `desk.svg`        | Omnipresent |

SVG support is required and enforced at build time (librsvg). The icons
directory is checked before any Openbox theme files, so dropping an SVG here
always wins.

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

### Integration

labwc-next exposes a Unix socket at `$XDG_RUNTIME_DIR/labwc-next.sock` (also
available as `$LABWC_NEXT_SOCKET`). It pushes workspace state on connect and
on every switch, and accepts commands to switch workspaces.

Example workspace bar using [Quickshell]:

```qml
import Quickshell
import Quickshell.Io
import QtQuick
import QtQuick.Layouts

ShellRoot {
    id: shellRoot
    property string activeWorkspace: ""
    property var workspaces: []

    Socket {
        id: ipc
        path: "/run/user/1000/labwc-next.sock"
        connected: true

        parser: SplitParser {
            onRead: msg => {
                console.log("IPC recv:", msg)
                if (msg.startsWith("workspace>>")) {
                    shellRoot.activeWorkspace = msg.slice(11)
                } else if (msg.startsWith("workspace-list>>")) {
                    shellRoot.workspaces = msg.slice(16).split(",")
                }
            }
        }

        onConnectedChanged: {
            console.log("IPC connected:", connected)
        }

        function switchTo(name) {
            write("workspace switch " + name + "\n")
            flush()
        }

        function reconfigure() {
            write("reconfigure\n")
            flush()
        }
    }

    PanelWindow {
        anchors {
            top: true
            left: true
            right: true
        }
        implicitHeight: 36
        exclusiveZone: implicitHeight
        color: "#1e1e2e"

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            spacing: 4

            Repeater {
                model: shellRoot.workspaces

                Rectangle {
                    required property string modelData
                    readonly property bool active: modelData === shellRoot.activeWorkspace

                    Layout.fillHeight: true
                    Layout.topMargin: 6
                    Layout.bottomMargin: 6
                    implicitWidth: label.implicitWidth + 20
                    radius: 4
                    color: active ? "#cba6f7" : "transparent"

                    Text {
                        id: label
                        anchors.centerIn: parent
                        text: modelData
                        color: parent.active ? "#1e1e2e" : "#cdd6f4"
                        font.pixelSize: 13
                        font.bold: parent.active
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: ipc.switchTo(modelData)
                        cursorShape: Qt.PointingHandCursor
                    }
                }
            }

            Item { Layout.fillWidth: true }

            Rectangle {
                Layout.fillHeight: true
                Layout.topMargin: 6
                Layout.bottomMargin: 6
                implicitWidth: reloadLabel.implicitWidth + 16
                radius: 4
                color: reloadArea.containsMouse ? "#45475a" : "transparent"

                Text {
                    id: reloadLabel
                    anchors.centerIn: parent
                    text: "reload"
                    color: "#6c7086"
                    font.pixelSize: 12
                }

                MouseArea {
                    id: reloadArea
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: ipc.reconfigure()
                    cursorShape: Qt.PointingHandCursor
                }
            }
        }
    }
}
```

**IPC wire format:**
- On connect: compositor sends `workspace>><name>` and `workspace-list>><n1>,<n2>,...`
- On workspace change: compositor sends `workspace>><name>`
- Commands: `workspace switch <name>\n`, `workspace list\n`, `reconfigure\n`

---

Upstream: https://github.com/labwc/labwc

[Wayland]: https://wayland.freedesktop.org/
[wlroots]: https://gitlab.freedesktop.org/wlroots/wlroots

[Quickshell]: https://quickshell.org/
