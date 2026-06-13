# Advanced usage - ddb\_dsp\_winamp

This is the dumping ground for more esoteric info about the program.

## Option documentation

Options with `<int>` take a single integer value; for example,
`procmin=576`. Options with `<list>` take one or more integers separated
by commas; for example, `bits=16,24`. Options without an equals sign
are boolean flags - the name itself toggles the option on.

Some plugins have different default values set in the source code.

&#x26a0;&#xfe0f; **Take care when** changing the buffering and audio
format options. Deviating from the safe defaults can expose bugs in
poorly coded plugins, possibly leading to strangeness like noise being
played. You don't want to shock your ears with it.

**Plugin loading:**

-   `<number>`: Load a specific module index from the DLL. If present,
    must be the first option. Numbering starts from zero. The name of
    each module can be found in the UI in the right-click menu of the
    plugin. **Default:** `0`.

-   `off`: Start with the plugin bypassed. It must be activated in the
    UI to use it.

-   `slowipc`: Disable the fast IPC hack for this plugin.

-   `unload`: Start with the plugin unloaded. It must be double-clicked
    in the UI to load it. Note that some errors (e.g. if the DLL doesn't
    exist) aren't detected until the plugin is loaded.

**Buffering:**

-   `nostretch`: Allow processing code to assume that the plugin doesn't
    stretch sound. This is just a minor optimization. Violations are
    detected and will cause the program to exit.

-   `procmin=<int>`, `procmax=<int>`, `procmult=<int>`

    These options impose limits on the size of the audio buffer passed
    to `ModifySamples()`.

    <P>
    -   `procmin`: Must have **at least** this many audio frames.
    -   `procmax`: Must have **at most** this many audio frames.
    -   `procmult`: The number of frames must be a multiple of this
        value. **Note:** The other two options must also be multiples of
        this value.

    A frame is defined as a sample for each channel.

    If there's not enough audio data available to satisfy `procmin` and
    `procmult`, then the current data will be buffered until we get more
    from DeaDBeeF. This will add a slight latency.

    **Default:** `procmin=576`, `procmax=576`, `procmult=576`.

    The defaults are conservative to allow for poorly coded plugins to
    work out of the box without crashes or corruption. The `576` value
    comes from documentation in the Winamp SDK.

    For a capable plugin that's known to work with any buffer size, the
    buffer management can be disabled with
    `procmin=1:procmax=0:procmult=1`. Beware that this could expose bugs
    in poorly coded plugins.

-   `randbuf`: Randomize input buffer size among supported values. This
    might be removed in the future.

**Audio format:**

-   `bits=<list>`, `ch=<list>`, `rate=<list>`

    Set the **bit depths**, **channel counts** and **sample rates**
    supported by the plugin.

    If we get input in an unsupported format, it may be **converted**
    (where implemented). Otherwise, the plugin will be **skipped**,
    unless `required` is set, in which case the program will **exit**.

    The options are considered separately - all possible combinations
    are assumed to be supported.

    **Format:** List of one or more integers separated by commas. An
    empty list means any value is supported.

    **Default:** `bits=16`, `ch=2`, `rate=`.

    Bit depth conversion is implemented. Conversions that lose precision
    use triangle (TPDF) dithering, which is [supposedly][whytri] the
    right type to use when doing further processing on the signal.

    Channel conversion is implemented in a somewhat crude way. No mixing
    is done. Typically, channels will be dropped or silent ones added.
    If adding channels to mono audio, then the second channel will be a
    duplicate of the first. If the destination is mono, then only the
    first channel will be used.

    Sample rate conversion is **unimplemented**. I expect this has low
    impact on plugin compatibility. A mismatch might cause poorly
    behaved plugins that hardcode a different sample rate to apply their
    effects at the wrong "speed".

-   `required`: Do not skip the plugin on unsupported formats (that
    can't be converted) - instead, exit the program. This can be useful
    to avoid surprises if the plugin lowers the volume. Note that this
    option will only do anything if `rate=` is used to limit the
    supported sample rates. (It was more useful in the past before
    conversion was implemented for `bits` and `ch`.)

[whytri]: https://en.wikipedia.org/wiki/Dither#Which_noise_distribution_to_use
