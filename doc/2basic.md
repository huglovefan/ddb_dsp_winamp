# Basic usage - ddb\_dsp\_winamp

## Configure the plugin

Before you
[add an instance of the plugin](#Add-an-instance-of-the-plugin), go to
**Edit -> Preferences -> Plugins -> Winamp&nbsp;DSP.**

Set **Host command** to a shell command that will run the host
executable. For example, on my system, it is:

    wine ~/.local/bin/wadsp_host.exe

Note: If you're using a sandboxing wrapper like Bubblewrap here, make
sure the command has access to the same `/dev/shm` so that all features
work correctly.

## Add an instance of the plugin

Go to **Edit -> Preferences -> DSP**, add the plugin and configure it.

The only option to set here is **Plugin list**. It takes a
space-separated list of one or more Winamp DSP plugins. They can be
specified by name, like `dsp_pacemaker` (in which case they'll be looked
up in the [search paths](#Plugin-search-paths)), or by a full path to
their DLL file, but this is mildly discouraged.

Note that if you use a full path:
**1.**&nbsp;It should be quoted following shell rules,
**2.**&nbsp;It can't contain the character ':' (colon),
**3.**&nbsp;The path must be accessible by the host process running in Wine,
**4.**&nbsp;Remember that the text box has a length limit.

Once you've set the list of plugins, the DSP host window should appear
when you play some sound.

### Nothing happens?

If you added the plugin but playback doesn't work, the host might be
failing to start for whatever reason. Error reporting is somewhat poor
at the moment. Try running DeaDBeeF from a terminal to see if there are
any hints in the output around when you try to play something.

### Plugin search paths

Plugin names in **Plugin list** without a full path are looked up in the
following directories:

-   `C:\Program Files (x86)\Winamp\Plugins` (depending on installation)
-   `$HOME/.local/lib/winamp`
-   `$HOME/.local/lib`

For example, the name `dsp_pacemaker` could check the following files:

-   `C:\Program Files (x86)\Winamp\Plugins\dsp_pacemaker`
-   `C:\Program Files (x86)\Winamp\Plugins\dsp_pacemaker.dll`
-   `$HOME/.local/lib/winamp/dsp_pacemaker`
-   `$HOME/.local/lib/winamp/dsp_pacemaker.dll`
-   `$HOME/.local/lib/dsp_pacemaker`
-   `$HOME/.local/lib/dsp_pacemaker.dll`

The matching is somewhat similar to how [PATH][pathvar] works. `.dll` is
automatically added.

If the same Wine prefix has Winamp installed in a non-default path, or
if the Wine prefix is a 32-bit one, then the Winamp plugin directory
might be different than shown here.

If the Wine prefix doesn't have access to your home directory, then the
paths starting with `$HOME` are skipped.

[pathvar]: https://en.wikipedia.org/wiki/PATH_(variable)

### Plugin option syntax, common options

There are options that control how plugins are loaded or used. They can
be set by adding them after a plugin's name or path, separated by a
colon (`:`).

Below is a small set of basic options. The full set is documented in
[Advanced usage](3advanced.md).

-   `<number>`: Load a specific module index from the DLL. Numbering
    starts from zero. The name of each module can be found in the UI in
    the double-click menu of the plugin. **Default:** `0`.
-   `off`: Start with the plugin bypassed. It must be activated in the
    UI to use it.
-   `unload`: Start with the plugin unloaded. It must be double-clicked
    in the UI to load it. Note that some errors (e.g. if the DLL doesn't
    exist) aren't detected until the plugin is loaded.

Examples:

-   `dsp_centercut` - Load module index `0` from Center Cut
-   `dsp_centercut:1:off` - Load module index `1` from Center Cut, leave
    it unchecked by default
-   `dsp_centercut:unload` - Add Center Cut with module index `0`, but
    require a double click to actually load it
