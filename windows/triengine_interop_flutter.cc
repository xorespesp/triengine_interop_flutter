#include "triengine_interop_flutter.hh"

// This must be included before many other Windows headers.
#include <windows.h>

// For getPlatformVersion; remove unless needed for your plugin implementation.
#include <VersionHelpers.h>

#include <memory>
#include <sstream>
#include <string>
#include <cstdint>

#include "utils/debug_utils.hh"

// triengine_interop protocol enums are referenced below as `ipc_proto::...`.
namespace ipc_proto = triengine_interop::surface::proto;

namespace triengine_interop_flutter
{
    // static
    void TriengineInteropFlutterPlugin::RegisterWithRegistrar(
        flutter::PluginRegistrarWindows* registrar)
    {
        auto channel =
            std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
                registrar->messenger(), "triengine_interop_flutter/channel",
                &flutter::StandardMethodCodec::GetInstance());

        auto plugin = std::make_unique<TriengineInteropFlutterPlugin>(
            registrar
        );

        channel->SetMethodCallHandler(
            [plugin_pointer = plugin.get()](const auto& call, auto result) {
                plugin_pointer->HandleMethodCall(call, std::move(result));
            });

        registrar->AddPlugin(std::move(plugin));
    }

    TriengineInteropFlutterPlugin::TriengineInteropFlutterPlugin(
        flutter::PluginRegistrarWindows* registrar)
        : registrar_{ registrar }
    {}

    TriengineInteropFlutterPlugin::~TriengineInteropFlutterPlugin()
    {}

    void TriengineInteropFlutterPlugin::HandleMethodCall(
        const flutter::MethodCall<flutter::EncodableValue>& method_call,
        std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result)
    {
        if (method_call.method_name().compare("createSurface") == 0)
        {
            LOG_DEBUG("createSurface called");
            if (surface_manager_) {
                result->Error("ALREADY_INITIALIZED", "Surface manager is already initialized.");
                return;
            }

            const auto* const args = std::get_if<flutter::EncodableMap>(method_call.arguments());
            if (!args) {
                result->Error("INVALID_ARGUMENTS", "Expected a map of arguments.");
                return;
            }

            const auto renderer_ipc_server_name = std::get<std::string>(args->at(flutter::EncodableValue{ "ipcServerName" }));
            const auto width = std::get<int32_t>(args->at(flutter::EncodableValue{ "width" }));
            const auto height = std::get<int32_t>(args->at(flutter::EncodableValue{ "height" }));

            // Initial frame-rate cap applied at connect (0 == uncapped).
            uint32_t max_fps = 0;
            if (const auto it = args->find(flutter::EncodableValue{ "maxFps" }); it != args->end()) {
                if (const auto* const v = std::get_if<int32_t>(&it->second)) {
                    max_fps = static_cast<uint32_t>(*v);
                }
            }

            constexpr FlutterDesktopGpuSurfaceType flutter_gpu_surface_type = 
                kFlutterDesktopGpuSurfaceTypeDxgiSharedHandle;
                //kFlutterDesktopGpuSurfaceTypeD3d11Texture2D; // NOTE: Not supported in flutter 3.32.6

            constexpr FlutterDesktopPixelFormat flutter_gpu_surface_texture_format = 
                kFlutterDesktopPixelFormatBGRA8888;
                //kFlutterDesktopGpuSurfaceFormatRGBA8888; // NOTE: Not supported in flutter 3.32.6

            surface_manager_ = std::make_unique<triengine_surface_manager>();
            if (!surface_manager_->create(
                renderer_ipc_server_name,
                width,
                height,
                flutter_gpu_surface_texture_format,
                max_fps))
            {
                result->Error("SURFACE_INIT_FAILED", "Failed to initialize surface manager.");
                return;
            }
            {
                std::scoped_lock lk{ render_lock_ };
                gpu_surface_desc_ = std::make_unique<FlutterDesktopGpuSurfaceDescriptor>();
                gpu_surface_desc_->struct_size = sizeof(FlutterDesktopGpuSurfaceDescriptor);
                // Setup the surface handle.
                // The expected type depends on the `FlutterDesktopGpuSurfaceType`.
                //
                // Provide a `ID3D11Texture2D*` when using `kFlutterDesktopGpuSurfaceTypeD3d11Texture2D`
                // or a `HANDLE` when using `kFlutterDesktopGpuSurfaceTypeDxgiSharedHandle`.
                //
                // The referenced resource needs to stay valid until it has been opened by Flutter.
                // Consider incrementing the resource's reference count in the
                // `FlutterDesktopGpuSurfaceTextureCallback` and registering a
                // `release_callback` for decrementing the reference count once it has been opened.
                gpu_surface_desc_->handle = surface_manager_->get_surface_handle(); // <-- 핵심
                gpu_surface_desc_->format = flutter_gpu_surface_texture_format;
                gpu_surface_desc_->width = surface_manager_->get_width();
                gpu_surface_desc_->height = surface_manager_->get_height();
                gpu_surface_desc_->visible_width = gpu_surface_desc_->width;
                gpu_surface_desc_->visible_height = gpu_surface_desc_->height;
                gpu_surface_desc_->release_context = this;
                gpu_surface_desc_->release_callback = [](void* release_context) {
                    auto* plugin = static_cast<TriengineInteropFlutterPlugin*>(release_context);
                    plugin->render_lock_.unlock(); // Flutter측에서 전달받은 표면 렌더링이 끝났다면, 걸어둔 렌더링 락을 해제.
                };
            }

            // flutter::TextureVariant (flutter::GpuSurfaceTexture) 생성
            texture_variant_ = std::make_unique<flutter::TextureVariant>(flutter::GpuSurfaceTexture{
                flutter_gpu_surface_type,
                [this](size_t width, size_t height) -> const FlutterDesktopGpuSurfaceDescriptor* {
                    // Flutter가 이 텍스처를 그리려고 할 때마다 이 콜백 함수가 호출된다.
                    // 이 콜백의 목적은 텍스처의 현재 상태(핸들, 크기)를 담은 Descriptor를 반환하는 것이다.
                    // (이 콜백은 HandleMethodCall 함수 스레드와 다른 스레드에서 호출됨에 주의)
                    render_lock_.lock(); // Flutter측에서 전달받은 표면 렌더링이 끝날 때 까지 렌더링 락을 걸어둔다.
                    gpu_surface_desc_->handle = surface_manager_->get_surface_handle(); // Update the handle
                    return gpu_surface_desc_.get();
                }
            });

            registered_texture_id_ = registrar_->texture_registrar()->RegisterTexture(texture_variant_.get());
            result->Success(flutter::EncodableValue(registered_texture_id_));
        }
        else if (method_call.method_name().compare("destroySurface") == 0)
        {
            LOG_DEBUG("destroySurface called");
            
            if (!surface_manager_) {
                result->Error("NotInitialized", "Renderer not initialized.");
                return;
            }

            LOG_DEBUG("Unregistering texture with ID: %d ...", registered_texture_id_);

            // Asynchronously unregisters an existing texture object.
            // Upon completion, the optional |callback| gets invoked.
            registrar_->texture_registrar()->UnregisterTexture(
                registered_texture_id_,
                nullptr // TODO: check texture unregisteration completed
            );

            LOG_DEBUG("Texture unregistered, proceeding to destroy renderer...");
            std::scoped_lock lk{ render_lock_ };
            texture_variant_.reset();
            registered_texture_id_ = -1;
            surface_manager_->destroy();
            surface_manager_.reset();
            gpu_surface_desc_.reset();
            texture_variant_.reset();

            LOG_INFO("Surface manager destroyed!");
            result->Success();
        }
        else if (method_call.method_name().compare("updateSurface") == 0)
        {
            if (!surface_manager_) {
                result->Error("NotInitialized", "Renderer not initialized.");
                return;
            }
            
            {
                std::scoped_lock lk{ render_lock_ };

                if (!surface_manager_->render_frame()) {
                    result->Error("RENDER_FAILED", "Failed to render frame.");
                    return;
                }

                // 새 프레임이 준비되었음을 Flutter 측에 알린다. (주기적으로 호출 필요)
                registrar_->texture_registrar()->MarkTextureFrameAvailable(registered_texture_id_);
            }

            result->Success();
        }
        else if (method_call.method_name().compare("resizeSurface") == 0)
        {
            if (!surface_manager_) {
                result->Error("NotInitialized", "Renderer not initialized.");
                return;
            }

            const auto* const args = std::get_if<flutter::EncodableMap>(method_call.arguments());
            if (!args) {
                result->Error("INVALID_ARGUMENTS", "Expected a map of arguments.");
                return;
            }

            const auto new_width = std::get<int32_t>(args->at(flutter::EncodableValue{ "width" }));
            const auto new_height = std::get<int32_t>(args->at(flutter::EncodableValue{ "height" }));

            // Resize the GPU surface descriptor to match the new size.
            {
                std::scoped_lock lk{ render_lock_ };
                
                if (!surface_manager_->resize_frame(new_width, new_height)) {
                    result->Error("RESIZE_FAILED", "Failed to resize DX11 renderer frame.");
                    return;
                }

                gpu_surface_desc_->width = new_width;
                gpu_surface_desc_->height = new_height;
                gpu_surface_desc_->visible_width = new_width;
                gpu_surface_desc_->visible_height = new_height;
                gpu_surface_desc_->handle = surface_manager_->get_surface_handle(); // Update the handle

                // Notify Flutter that the texture frame is available after resizing.
                registrar_->texture_registrar()->MarkTextureFrameAvailable(registered_texture_id_);
            }
            
            result->Success();
        }
        else if (method_call.method_name().compare("changeMaxFps") == 0)
        {
            if (!surface_manager_) {
                result->Error("NotInitialized", "Surface manager not initialized.");
                return;
            }

            const auto* const args = std::get_if<flutter::EncodableMap>(method_call.arguments());
            if (!args) {
                result->Error("INVALID_ARGUMENTS", "Expected a map of arguments.");
                return;
            }

            // `maxFps` is a concrete cap (0 == uncapped).
            uint32_t max_fps = 0;
            if (const auto* const v = std::get_if<int32_t>(&args->at(flutter::EncodableValue{ "maxFps" }))) {
                max_fps = static_cast<uint32_t>(*v);
            }

            LOG_TRACE("changeMaxFps: {}", max_fps);

            if (surface_manager_->change_max_fps(max_fps)) {
                result->Success();
            } else {
                result->Error("CHANGE_MAX_FPS_FAILED", "Failed to change max fps.");
            }
        }
        else if (method_call.method_name().compare("sendMouseButtonEvent") == 0)
        {
            if (!surface_manager_) {
                result->Error("NotInitialized", "Surface manager not initialized.");
                return;
            }

            const auto* const args = std::get_if<flutter::EncodableMap>(method_call.arguments());
            if (!args) {
                result->Error("INVALID_ARGUMENTS", "Expected a map of arguments.");
                return;
            }

            const auto x = std::get<int32_t>(args->at(flutter::EncodableValue{ "x" }));
            const auto y = std::get<int32_t>(args->at(flutter::EncodableValue{ "y" }));
            const auto button = static_cast<ipc_proto::mouse_button_type>(
                std::get<int32_t>(args->at(flutter::EncodableValue{ "button" }))
            );
            const auto action = static_cast<ipc_proto::button_action_type>(
                std::get<int32_t>(args->at(flutter::EncodableValue{ "action" }))
            );
            const auto mods = static_cast<ipc_proto::modifier_button_type>(
                std::get<int32_t>(args->at(flutter::EncodableValue{ "mods" }))
            );

            LOG_TRACE("sendMouseButtonEvent: x={}, y={}, button={}, action={}, mods=0x{:X}"
                , x, y
                , static_cast<std::underlying_type_t<ipc_proto::mouse_button_type>>(button)
                , static_cast<std::underlying_type_t<ipc_proto::button_action_type>>(action)
                , static_cast<std::underlying_type_t<ipc_proto::modifier_button_type>>(mods)
            );

            // TODO: Validate arguments...

            const bool success = surface_manager_->send_mouse_button_event(
                x, y, 
                button, 
                action, 
                mods
            );

            if (success) {
                result->Success();
            } else {
                result->Error("MOUSE_EVENT_FAILED", "Failed to send mouse button event.");
            }
        }
        else if (method_call.method_name().compare("sendMouseMoveEvent") == 0)
        {
            if (!surface_manager_) {
                result->Error("NotInitialized", "Surface manager not initialized.");
                return;
            }

            const auto* const args = std::get_if<flutter::EncodableMap>(method_call.arguments());
            if (!args) {
                result->Error("INVALID_ARGUMENTS", "Expected a map of arguments.");
                return;
            }

            const auto x = std::get<int32_t>(args->at(flutter::EncodableValue{ "x" }));
            const auto y = std::get<int32_t>(args->at(flutter::EncodableValue{ "y" }));
            const auto mods = static_cast<ipc_proto::modifier_button_type>(
                std::get<int32_t>(args->at(flutter::EncodableValue{ "mods" }))
            );

            // TODO: Validate arguments...

            const bool success = surface_manager_->send_mouse_move_event(
                x, y, 
                mods
            );

            if (success) {
                result->Success();
            } else {
                result->Error("MOUSE_EVENT_FAILED", "Failed to send mouse move event.");
            }
        }
        else if (method_call.method_name().compare("sendMouseScrollEvent") == 0)
        {
            if (!surface_manager_) {
                result->Error("NotInitialized", "Surface manager not initialized.");
                return;
            }

            const auto* const args = std::get_if<flutter::EncodableMap>(method_call.arguments());
            if (!args) {
                result->Error("INVALID_ARGUMENTS", "Expected a map of arguments.");
                return;
            }

            const auto yoffset = std::get<double>(args->at(flutter::EncodableValue{ "yoffset" }));

            const bool success = surface_manager_->send_mouse_scroll_event(static_cast<float>(yoffset));
            if (success) {
                result->Success();
            } else {
                result->Error("MOUSE_EVENT_FAILED", "Failed to send mouse scroll event.");
            }
        }
        else if (method_call.method_name().compare("sendKeyEvent") == 0)
        {
            if (!surface_manager_) {
                result->Error("NotInitialized", "Surface manager not initialized.");
                return;
            }

            const auto* const args = std::get_if<flutter::EncodableMap>(method_call.arguments());
            if (!args) {
                result->Error("INVALID_ARGUMENTS", "Expected a map of arguments.");
                return;
            }

            const auto key = static_cast<ipc_proto::key_button_type>(
                std::get<int32_t>(args->at(flutter::EncodableValue{ "key" }))
            );
            const auto action = static_cast<ipc_proto::button_action_type>(
                std::get<int32_t>(args->at(flutter::EncodableValue{ "action" }))
            );
            const auto mods = static_cast<ipc_proto::modifier_button_type>(
                std::get<int32_t>(args->at(flutter::EncodableValue{ "mods" }))
            );

            LOG_TRACE("sendKeyEvent: key={}, action={}, mods=0x{:X}"
                , static_cast<std::underlying_type_t<ipc_proto::key_button_type>>(key)
                , static_cast<std::underlying_type_t<ipc_proto::button_action_type>>(action)
                , static_cast<std::underlying_type_t<ipc_proto::modifier_button_type>>(mods)
            );

            const bool success = surface_manager_->send_key_event(key, action, mods);
            if (success) {
                result->Success();
            } else {
                result->Error("KEY_EVENT_FAILED", "Failed to send key event.");
            }
        }
        else if (method_call.method_name().compare("getPlatformVersion") == 0)
        {
            std::ostringstream version_stream;
            version_stream << "Windows ";
            if (IsWindows10OrGreater()) {
                version_stream << "10+";
            } else if (IsWindows8OrGreater()) {
                version_stream << "8";
            } else if (IsWindows7OrGreater()) {
                version_stream << "7";
            }
            result->Success(flutter::EncodableValue(version_stream.str()));
        }
        else
        {
            result->NotImplemented();
        }
    }

} // namespace