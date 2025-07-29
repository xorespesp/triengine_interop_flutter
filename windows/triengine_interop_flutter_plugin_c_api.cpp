#include "include/triengine_interop_flutter/triengine_interop_flutter_plugin_c_api.h"

#include <flutter/plugin_registrar_windows.h>

#include "triengine_interop_flutter.h"

void TriengineInteropFlutterPluginCApiRegisterWithRegistrar(
    FlutterDesktopPluginRegistrarRef registrar) {
  triengine_interop_flutter::TriengineInteropFlutterPlugin::RegisterWithRegistrar(
      flutter::PluginRegistrarManager::GetInstance()
          ->GetRegistrar<flutter::PluginRegistrarWindows>(registrar));
}
