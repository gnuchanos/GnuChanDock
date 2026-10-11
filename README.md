# GnuChanDock

GnuChanDock is the desktop dock: a bar along an edge of the screen that holds
the program icons, the running windows and a menu. It is written in C against
Xlib, Xft, freetype, Xrender, Xext and Imlib2 — Imlib2 for the icons, which it
loads and scales. It is Debian-only, on purpose.

## Building

    git clone <repository-url> GnuChanDock
    cd GnuChanDock/src
    python3 makefile.py

The makefile installs the build dependencies with apt-get, compiles the sources,
and re-runs itself through sudo to write into `/usr/local/bin`. The binary goes
to `/usr/local/bin/GnuChanDock`; a desktop entry is written to
`/usr/share/applications/gnuchandock.desktop`; the settings script, `logo.png`
and `settings.png` go under `~/.config/GnuChanDock/`.

The makefile takes an action as its first argument: `install` (the default),
`build`, `run` and `uninstall`.

## Dependencies

The makefile probes for these headers and installs the matching package when one
is missing:

- `build-essential` (gcc)
- `pkg-config`
- `libx11-dev` — `X11/Xlib.h`
- `libxft-dev` — `X11/Xft/Xft.h`
- `libfreetype-dev` — `ft2build.h`
- `libxrender-dev` — `X11/extensions/Xrender.h`
- `libxext-dev` — `X11/extensions/shape.h`
- `libimlib2-dev` — `Imlib2.h`

The linking is driven by pkg-config for the modules `x11`, `xft`, `xrender`,
`xext`, `fontconfig`, `freetype2` and `imlib2`.

## X11 only

This program is built directly against X11 (Xlib, Xft and the rest of the X
stack). It is designed for an X11 session and does not support Wayland: it was
never tested there, its window management assumes an X server, and no work has
been done to port it. On a Wayland session it may run through XWayland for basic
use, but that is not a supported configuration and behaviour there is undefined.
Run it on X11.

## License

GPL3.
