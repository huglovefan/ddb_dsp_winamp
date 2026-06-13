#pragma once

#include <windef.h>

#include "../afmt.h"
#include "plugin.hpp"

bool parse_plugin_options(const std::wstring &, PluginOpts &);
bool load_plugin(Plugin *, HWND);
const char *plugin_supports_format(const Plugin *pl, const AFMT *fmt);
