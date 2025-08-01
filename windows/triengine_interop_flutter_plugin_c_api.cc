#include "include/triengine_interop_flutter/triengine_interop_flutter_plugin_c_api.h"

#include <flutter/plugin_registrar_windows.h>

#include "triengine_interop_flutter.hh"
#include "utils/debug_utils.hh"

void TriengineInteropFlutterPluginCApiRegisterWithRegistrar(
    FlutterDesktopPluginRegistrarRef registrar)
{
    LOG_TRACE("{}() ENTER", __func__);
#if defined(_DEBUG)
    LOG_DEBUG("Plugin Build Version: " __DATE__ ", " __TIME__ " (DBG)");
#else
    LOG_DEBUG("Plugin Build Version: " __DATE__ ", " __TIME__ " (REL)");
#endif

    triengine_interop_flutter::TriengineInteropFlutterPlugin::RegisterWithRegistrar(
        flutter::PluginRegistrarManager::GetInstance()
            ->GetRegistrar<flutter::PluginRegistrarWindows>(registrar)
    );
    
    LOG_TRACE("{}() LEAVE", __func__);
}
