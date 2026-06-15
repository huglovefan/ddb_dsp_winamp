# ddb\_dsp\_winamp

**ddb\_dsp\_winamp** is a plugin for the the [DeaDBeeF][ddb] music
player that allows using Winamp DSP plugins on Linux.

Now with a clicky GUI:

![main window screenshot](/doc/mainwin.png)

![context menu screenshot](/doc/context.png)

[ddb]: https://deadbeef.sourceforge.io/

## Features

-   Written with wide plugin compatibility in mind
-   GUI to toggle, reorder, add, remove, etc plugins at runtime
-   Automatic bit depth conversion depending on active plugins
-   Supports plugins that use [Winamp IPC messages][ipc]
-   Jank-free seeks and track changes

[ipc]: https://web.archive.org/web/20040215055100/http://www.winamp.com/nsdn/winamp2x/dev/sdk/api.php

## Documentation

**Main:**

-   [Compile and install](doc/1compinst.md) - how to get it onto your
    machine
-   [Basic usage](doc/2basic.md) - get some plugins running
-   [Advanced usage](doc/3advanced.md) - get the most out of it

**Misc:**

-   [Plugins](doc/plugins.md) - list of tested plugins, where to find more
-   [UI explanation](doc/ui.md) - what's in the screenshots

## Credit and copyright

Original code is licensed as `LGPL-3.0-only`, unless a license header
says otherwise. Third-party code used is listed below.

-   `3p/imgui`: [Dear ImGui by Omar&nbsp;Cornut and contributors][imgui]
    (MIT)
-   `src/crc32.[ch]`: [CRC32 routines by Ayman&nbsp;El&nbsp;Didi][crc]
    (CC0-1.0)
-   `src/float.h`: lifted from DeaDBeeF, originally from Xiph.Org
    libvorbis (BSD-3-Clause)
-   `src/host/winamp.hpp`: based on headers from the Winamp 5.55 SDK and
    earlier by Justin&nbsp;Frankel and Nullsoft,&nbsp;Inc. (Zlib)

[imgui]: https://github.com/ocornut/imgui
[crc]: https://github.com/aeldidi/crc32

Functions or files with a comment that mentions "chatgpt" originate from
LLM output.

**Winamp** is a trademark of its respective owners. No affiliation or
endorsement is claimed.
