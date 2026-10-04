// wxl-retail-ui access to the hub ABI and shared FrameScript service.
// Copyright (C) 2026 WarcraftXL. GPLv3.

#pragma once

#include "common/ExtensionConfig.hpp"
#include "wxl/FrameScriptApi.h"
#include "wxl/FdidApi.h"
#include "wxl/NetworkApi.h"
#include "wxl/PluginApi.h"

namespace wxl_retail_ui
{
    extern const WXL_Api* g_api;
    extern const WXL_FrameScriptApi* g_framescript;
    extern const WXL_NetworkApi* g_network;
    extern const WXL_FdidApi* g_fdid;

    inline const WXL_FrameScriptApi* FrameScript()
    {
        if (!g_framescript)
            g_framescript = static_cast<const WXL_FrameScriptApi*>(
                g_api->GetInterface("wxl.framescript", WXL_FRAME_SCRIPT_API_VERSION));
        return g_framescript;
    }

    inline const WXL_NetworkApi* Network()
    {
        if (!g_network)
            g_network = static_cast<const WXL_NetworkApi*>(
                g_api->GetInterface("wxl.network", WXL_NETWORK_API_VERSION));
        return g_network;
    }

    inline const WXL_FdidApi* Fdid()
    {
        if (!g_fdid)
            g_fdid = static_cast<const WXL_FdidApi*>(
                g_api->GetInterface("wxl.fdid", WXL_FDID_API_VERSION));
        return g_fdid;
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
    bool InstallRetailTransmog();
    bool InstallRetailTransmogEquipment();
    bool InstallRetailTransmogApply();
    bool InstallRetailTransmogOutfits();
    bool InstallRetailTransmogCustomSets();
    bool InstallRetailTransmogSituations();
    bool InstallRetailTransmogPreview();
    bool InstallWeeklyRewards();
}

#define WLOG_INFO(...)  ::wxl_retail_ui::g_api->Log(WXL_LOG_INFO,  "wxl-retail-ui", __VA_ARGS__)
#define WLOG_WARN(...)  ::wxl_retail_ui::g_api->Log(WXL_LOG_WARN,  "wxl-retail-ui", __VA_ARGS__)
#define WLOG_ERROR(...) ::wxl_retail_ui::g_api->Log(WXL_LOG_ERROR, "wxl-retail-ui", __VA_ARGS__)
