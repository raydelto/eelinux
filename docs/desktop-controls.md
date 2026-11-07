# Desktop controls and startup

The TWM title buttons use the console's four symbols:

| Button | Action |
| --- | --- |
| Triangle | Shade the window to its title bar; click again to reveal its body |
| Square | Minimize; click its entry in the window list to restore it |
| Circle | Maximize; click again to restore its previous size and position |
| X | Ask the application to close; older clients without the X11 close protocol have their X connection closed |

Drag the title bar to move a window. Drag a window border to resize it, or
hold **Alt** and drag with the **right mouse button** inside the window.
The title bar's right-click menu also provides window operations. Resizing
or maximizing a shaded window first reveals its body. Shading keeps the
application running and preserves its full client size.

**F1** opens the main menu, **Alt+Enter** opens a terminal, **Alt+Tab** cycles
windows, and **Alt+F4** closes the selected window. **Alt+F9** minimizes,
**Alt+F10** toggles maximize, and **Alt+F11** toggles shade. The main menu can
show the window list again if it has been hidden.

Left-click the desktop for the main menu or right-click it for the window menu.
Release the button: the menu stays open. Move to an entry and click to select it;
click a submenu label or hover over its arrow to open it. Click outside the menu
or press **Escape** to dismiss it. The title-bar and window-list menus use the
same behavior.

## Startup

The session loads X resources before opening applications, then starts TWM
without waiting for wallpaper drawing or display preference commands.
The clock, terminal, wallpaper, screen-saver preferences, keyboard selection,
and application menu remain available. Exiting TWM stops session children
and returns to the console.

The application menu has an optional cache under
`${XDG_CACHE_HOME:-$HOME/.cache}/eelinux`. It checks file metadata before reuse,
and regenerates when desktop entries, application availability, locale or XDG
paths change. Missing or unusable caches fall back to normal menu generation.
DVDs retain their prepared system-menu path. A 70-entry host fixture opened all
70 desktop files on the first parse and none on a cache hit, with identical
output. This measures file reads, not physical PS2 startup time.

The framebuffer preparation helper also keeps an existing RGB555 mode in place,
avoiding a redundant display reset on subsequent launches. Changing from the
console's 32-bit format still selects and verifies RGB555 without changing its
display timing or resolution.

The previous unmodified stock `.twmrc` automatically uses the updated controls.
Custom `.twmrc` files retain their settings; merge the button and border bindings
from `/etc/skel/.twmrc` to adopt the controls. The new `f.shade` and `f.close`
functions require the patched eeLinux TWM binary.

## Building the changes

Use a fresh build workspace so that earlier release artifacts and source patch
stamps are preserved:

```sh
make userland PROFILE=full WORK="$HOME/.cache/eelinux-desktop-update" JOBS=2
```

This builds the window manager, desktop helpers and expanded tools together.
The existing image and media targets package the resulting staging tree. A source
update does not modify previously generated USB images or DVD ISOs.

The new window and session checks run on a host X server and with mocked session
commands. The cross-built R5900 TWM also passes the window checks under the
project's Linux-user emulator connected to Xvfb. These checks preserve shaped
windows, client resize requests, window-list anchors and minimized title updates.
Actual PS2 startup time and physical mouse behaviour need a console check.
