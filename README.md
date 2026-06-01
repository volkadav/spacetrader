# spacetrader

A simple TUI space trading/adventure game where you take the role of a down on their
luck merchant adventurer trying to raise cash to get their starship out of the impound
lot of the starport on an outlying colony world, Kepler's Reach.

## Build

### Requirements

- a working C99 compiler
- gnu make
- ncurses (library and headers)

```sh
./configure
make
```

`configure` picks a working C99 compiler (preferring `gcc`, then `clang`, then `cc`, unless `CC` is set),
detects a working curses setup (including BSD/Linux differences), and writes `Makefile`.
It also detects GNU make (`make` or `gmake`) and generates a wrapper `Makefile` that delegates to it.

## Install

### Tested platforms

Tested on:

    - amd64
        - Debian 13/Trixie (stable)
        - FreeBSD 15.0
        - OpenBSD 7.9
        - OmniOS r151054 (LTS) (slight ui glitches atm)
    - arm64
        - Debian 13/Trixie (stable)
        - OpenBSD 7.9

(The code here is not that complex, so I suspect that most any POSIX-y platform with a working C99
compiler and ncurses available will work.)

### Directions

```sh
make install
```

`install` places the binary in `/usr/local/games`, then `/usr/games`, and falls back to `/usr/local/bin`.  
The man page is installed to the matching `man6` location (`/usr/local/share/man/man6` or `/usr/share/man/man6`).

You can override paths explicitly:

```sh
make install BINDIR=/custom/bin MAN6DIR=/custom/share/man/man6
```

To remove installed files:

```sh
make uninstall
```

## Authors and License

See the AUTHORS and LICENSE files.
