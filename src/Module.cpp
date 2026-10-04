#include "ExtensionApi.hpp"

const WXL_PluginInfo* __cdecl WXL_Query(void)
{
    static const WXL_PluginInfo info{
        sizeof(WXL_PluginInfo), WXL_API_VERSION, "wxl-retail-ui", 1, WXL_CLIENT_BUILD,
    };
    return &info;
}

int __cdecl WXL_Load(const WXL_Api* api)
{
    if (!api || api->apiVersion != WXL_API_VERSION) return 0;
    wxl_retail_ui::g_api = api;

    if (!wxl_retail_ui::ConfigBool("WXL_RETAIL_UI", true))
    {
        api->Log(WXL_LOG_INFO, "wxl-retail-ui", "extension disabled by configuration");
        return 1;
    }
    if (!wxl_retail_ui::FrameScript())
    {
        api->Log(WXL_LOG_ERROR, "wxl-retail-ui", "required wxl.framescript v1 is unavailable");
        return 0;
    }
    if (!wxl_retail_ui::Network())
    {
        api->Log(WXL_LOG_ERROR, "wxl-retail-ui", "required wxl.network v1 is unavailable");
        return 0;
    }

    bool ok = wxl_retail_ui::InstallRetailUiCompatibility();
    ok &= wxl_retail_ui::InstallRetailTransmog();
    ok &= wxl_retail_ui::InstallRetailTransmogEquipment();
    ok &= wxl_retail_ui::InstallRetailTransmogApply();
    ok &= wxl_retail_ui::InstallRetailTransmogOutfits();
    ok &= wxl_retail_ui::InstallRetailTransmogCustomSets();
    ok &= wxl_retail_ui::InstallRetailTransmogSituations();
    ok &= wxl_retail_ui::InstallRetailTransmogPreview();
    if (wxl_retail_ui::ConfigBool("WXL_WEEKLY_REWARDS", true))
        ok &= wxl_retail_ui::InstallWeeklyRewards();
    return ok ? 1 : 0;
}
