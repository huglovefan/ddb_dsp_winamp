# UI explanation - ddb\_dsp\_winamp

Consider the following:

![main window screenshot](/doc/mainwin.png)

![context menu screenshot](/doc/context.png)

## Plugin table, buttons

-   The checkbox next to a plugin name controls whether the plugin is
    active (checked) or bypassed (unchecked). A bypassed plugin has no
    effect on the sound.
-   Clicking a plugin in the table selects it. The **Up**, **Dn** and
    **Config** buttons below the table operate on the selected plugin.
    Selection is indicated by a highlighted row.
-   Right-clicking a plugin shows a context menu with some more options.
    From here, it's also possible to unload or load the plugin and
    change which module is loaded.
-   Double-clicking a plugin shows its configuration window - or if the
    plugin is unloaded, loads it.
-   An unloaded plugin is indicated by a dimmed name in square brackets.

**Actions:**

-   **Up**, **Dn**: Move the plugin up or down in the list.
-   **Config**: Show the plugin's configuration or info window, if it
    has one. Note that depending on the plugin, this might disable some
    of the other actions until the window is closed. (Some poorly behaved
    plugins might even block input to the host window.)
-   **Unload**, **Load**: This can be used to completely unload the
    plugin's DLL from memory.
-   **Remove**: Unload and forget about the plugin. It can be added
    again using **Add**.
-   **Add**: To add a plugin, type its name (and any options) in the
    text field next to this button and click **Add**. The syntax is the
    same as documented elsewhere.

## Info line

-   The **In** and **Out** times are the total durations of input and
    output audio. They are fun to look at. If a plugin speeds up or
    slows down sound, the times can drift out of sync.
-   **Proc** shows how long processing takes, and how long it can take
    (length of input buffer). If the ratio nears 1x, your CPU is too
    slow for the plugins you're using.
