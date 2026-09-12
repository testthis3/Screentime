
# screentime

Minimal X11 screen-time tracker written in C.

Dependencies:
- base-devel
- libX11-devel
- gcc
- make


# 1. Installation

Clone the project, and run

    $ make
    $ doas make install clean

This installs screentime to: ~/.local/bin/screentime

Make sure ~/.local/bin is in your PATH.

    echo $PATH | grep -q "$HOME/.local/bin" && echo "Yes, it is in PATH" || echo "No, it is NOT in PATH"

If its not in PATH, go to `$HOME/.bashrc` and add

    export PATH="$PATH:$HOME/.local/bin"

# 2. Usage
go to `$HOME/.xinitrc` and add

    screentime &


Logs are saved automatically to:

~/.local/share/screentime/YYYY-MM-DD.log
