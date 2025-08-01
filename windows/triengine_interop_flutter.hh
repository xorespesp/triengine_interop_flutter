#pragma once
#include <flutter/method_channel.h>
#include <flutter/plugin_registrar_windows.h>
#include <flutter/standard_method_codec.h>
#include <flutter/texture_registrar.h>

#include <memory>
#include <mutex>

#include "triengine_surface_manager.hh"

namespace triengine_interop_flutter
{
    class TriengineInteropFlutterPlugin : public flutter::Plugin {
    public:
        static void RegisterWithRegistrar(flutter::PluginRegistrarWindows* registrar);

        TriengineInteropFlutterPlugin(flutter::PluginRegistrarWindows* registrar);
        virtual ~TriengineInteropFlutterPlugin();

        // Disallow copy and assign.
        TriengineInteropFlutterPlugin(const TriengineInteropFlutterPlugin&) = delete;
        TriengineInteropFlutterPlugin& operator=(const TriengineInteropFlutterPlugin&) = delete;

        // Called when a method is called on this plugin's channel from Dart.
        void HandleMethodCall(
            const flutter::MethodCall<flutter::EncodableValue>& method_call,
            std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result
        );

    private:
        flutter::PluginRegistrarWindows* registrar_{ nullptr };
        std::unique_ptr<FlutterDesktopGpuSurfaceDescriptor> gpu_surface_desc_;
        std::unique_ptr<flutter::TextureVariant> texture_variant_;
        int64_t registered_texture_id_{ -1 };
        std::unique_ptr<triengine_surface_manager> surface_manager_;
        mutable std::mutex render_lock_;
    };

} // namespace