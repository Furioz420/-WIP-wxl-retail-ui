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

    return wxl_retail_ui::InstallRetailUiCompatibility() ? 1 : 0;
}
