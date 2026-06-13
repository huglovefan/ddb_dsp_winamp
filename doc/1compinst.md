# Compile and install - ddb\_dsp\_winamp

## Requirements

-   git
-   make
-   gcc, g++
-   32-bit mingw-w64 cross-compilers for C and C++
-   wine with 32-bit support

### Debian 13 (Trixie)

To install everything:

    sudo apt install \
        g++ g++-mingw-w64-i686-win32 gcc gcc-mingw-w64-i686-win32 \
        git make wine32:i386

32-bit wine requires the i386 [multiarch][ma] repository to be enabled.
If you didn't already do so, run:

    sudo dpkg --add-architecture i386
    sudo apt update

[ma]: https://wiki.debian.org/Multiarch/HOWTO

## Obtaining the source code

Clone the repository, then enter the directory:

    git clone https://github.com/huglovefan/ddb_dsp_winamp
    cd ddb_dsp_winamp

Fetch the ImGui submodule:

    git submodule update --init

Tip: Place `ddb_dsp_winamp` in a directory where it's not in the way.
[Installing](#Installing) and other steps might assume it's not moved
later.

## Compiling

This compiles both the DeaDBeeF plugin and the "host" executable that
runs the Winamp plugins.

    make

If your DeaDBeeF is installed in a non-standard way (like using the
official deb package or a source checkout), you might have to manually
specify the path to its `include` directory. Distro packages should work
without this.

    # if "make" by itself didn't work, try this
    # this example is for the official deb package
    make DEADBEEF_INCLUDE=/opt/deadbeef/include

## Installing

This copies the plugin to a location in your home directory that
DeaDBeeF will pick it up from. The host executable is copied to
`~/.local/bin/`.

    make install

## Updating

(needs testing)

Most of the time, this should be enough:

    git fetch origin
    git merge --ff-only origin/master

Then re-read this document from the beginning, skipping the `git clone`
step. Beware that other documentation could have changed too.

## Uninstalling

    make uninstall

Restart DeaDBeeF and the plugin should be automatically removed.

Note that some traces of the program will have to be removed manually:

- Entries in DeaDBeeF's config file
- The `ddb_dsp_winamp` directory you cloned
- Any packages you installed as dependencies
