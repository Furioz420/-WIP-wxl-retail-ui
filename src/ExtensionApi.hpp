// wxl-retail-ui access to the hub ABI and shared FrameScript service.
// Copyright (C) 2026 WarcraftXL. GPLv3.

#pragma once

#include "common/ExtensionConfig.hpp"
#include "wxl/FrameScriptApi.h"
#include "wxl/PluginApi.h"

namespace wxl_retail_ui
{
    extern const WXL_Api* g_api;
    extern const WXL_FrameScriptApi* g_framescript;

    inline const WXL_FrameScriptApi* FrameScript()
    {
        if (!g_framescript)
            g_framescript = static_cast<const WXL_FrameScriptApi*>(
                g_api->GetInterface("wxl.framescript", WXL_FRAME_SCRIPT_API_VERSION));
        return g_framescript;
    }

    inline bool ConfigBool(const char* name, bool fallback)
    {
        char value[16] = {};
        return wxl::ext::config::Raw(name, value, sizeof value,
                                     "Extensions\\wxl-retail-ui\\wxl-retail-ui.cfg")
            ? wxl::ext::config::Truthy(value, fallback)
            : fallback;
    }

    bool InstallRetailUiCompatibility();
}

#define WLOG_INFO(...)  ::wxl_retail_ui::g_api->Log(WXL_LOG_INFO,  "wxl-retail-ui", __VA_ARGS__)
#define WLOG_WARN(...)  ::wxl_retail_ui::g_api->Log(WXL_LOG_WARN,  "wxl-retail-ui", __VA_ARGS__)
#define WLOG_ERROR(...) ::wxl_retail_ui::g_api->Log(WXL_LOG_ERROR, "wxl-retail-ui", __VA_ARGS__)
