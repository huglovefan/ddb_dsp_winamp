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

-   [UI explanation](doc/ui.md) - what's in the screenshots

## Credit and copyright

**Code:**

-   [Dear ImGui by Omar&nbsp;Cornut and contributors][imgui] (MIT)
-   `src/crc32.[ch]`: [CRC32 routines by Ayman&nbsp;El&nbsp;Didi][crc]
    (CC0)
-   `src/float.h`: lifted from DeaDBeeF, originally from Xiph.Org
    libvorbis (BSD-3-Clause)
-   `src/host/winamp.hpp` uses definitions from the Winamp&nbsp;SDK by
    Justin&nbsp;Frankel and Nullsoft,&nbsp;Inc. (Zlib)
-   A few functions were entirely copied from LLM output. Use
    <CODE>grep&nbsp;chatgpt</CODE> to find them. There are also others
    where an LLM wrote only part of the code, but listing every instance
    of LLM assistance would be pointless.
-   Likewise: <CODE>grep&nbsp;stackoverflow</CODE>.

**Other mentions:**

-   [Wine](https://www.winehq.org/) is used to run the stuff on Linux.
    None of this would work without it.

[imgui]: https://github.com/ocornut/imgui
[crc]: https://github.com/aeldidi/crc32
