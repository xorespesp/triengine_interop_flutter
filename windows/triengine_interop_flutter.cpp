#include "triengine_interop_flutter.h"

// This must be included before many other Windows headers.
#include <windows.h>

// For getPlatformVersion; remove unless needed for your plugin implementation.
#include <VersionHelpers.h>

#include <memory>
#include <sstream>

#include "utils/debug_utils.h"

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

            const auto width = std::get<int32_t>(args->at(flutter::EncodableValue{ "width" }));
            const auto height = std::get<int32_t>(args->at(flutter::EncodableValue{ "height" }));

            surface_manager_ = std::make_unique<triengine_surface_manager>();
            if (!surface_manager_->create(width, height)) {
                result->Error("SURFACE_INIT_FAILED", "Failed to initialize surface manager.");
                return;
            }

            const FlutterDesktopGpuSurfaceType surface_type = 
                kFlutterDesktopGpuSurfaceTypeDxgiSharedHandle;
                //kFlutterDesktopGpuSurfaceTypeD3d11Texture2D;

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
                gpu_surface_desc_->format = kFlutterDesktopPixelFormatBGRA8888;
                gpu_surface_desc_->width = surface_manager_->get_width();
                gpu_surface_desc_->height = surface_manager_->get_height();
                gpu_surface_desc_->visible_width = gpu_surface_desc_->width;
                gpu_surface_desc_->visible_height = gpu_surface_desc_->height;
                gpu_surface_desc_->release_context = this;
                gpu_surface_desc_->release_callback = [](void* release_context) {
                    auto* plugin = static_cast<TriengineInteropFlutterPlugin*>(release_context);
                    plugin->render_lock_.unlock();
                };
            }

            // flutter::TextureVariant (flutter::GpuSurfaceTexture) 생성
            texture_variant_ = std::make_unique<flutter::TextureVariant>(flutter::GpuSurfaceTexture{
                surface_type,
                [this](size_t width, size_t height) -> const FlutterDesktopGpuSurfaceDescriptor* {
                    // Flutter가 이 텍스처를 그리려고 할 때마다 이 콜백 함수가 호출된다.
                    // 이 콜백의 목적은 텍스처의 현재 상태(핸들, 크기)를 담은 Descriptor를 반환하는 것이다.
                    // (이 콜백은 HandleMethodCall 함수 스레드와 다른 스레드에서 호출됨에 주의)
                    render_lock_.lock();
                    gpu_surface_desc_->handle = surface_manager_->get_surface_handle(); // Update the handle
                    return gpu_surface_desc_.get();
                }
            });

            registered_texture_id_ = registrar_->texture_registrar()->RegisterTexture(texture_variant_.get());
            result->Success(flutter::EncodableValue(registered_texture_id_));
        }
        else if (method_call.method_name().compare("destroySurface") == 0)
        {
            result->Error("NOT_IMPLEMENTED", "Not implemented.");
        }
        else if (method_call.method_name().compare("updateSurface") == 0)
        {
            if (!surface_manager_) {
                result->Error("NotInitialized", "Renderer not initialized.");
                return;
            }
            
            {
                std::scoped_lock lk{ render_lock_ };
                surface_manager_->render_frame();
                
                // 새 프레임이 준비되었음을 Flutter 측에 알린다. (주기적으로 호출 필요)
                registrar_->texture_registrar()->MarkTextureFrameAvailable(registered_texture_id_);
            }

            result->Success();
        }
        else if (method_call.method_name().compare("resizeSurface") == 0)
        {
            const auto* const args = std::get_if<flutter::EncodableMap>(method_call.arguments());
            if (!args) {
                result->Error("INVALID_ARGUMENTS", "Expected a map of arguments.");
                return;
            }

            //const auto new_width = std::get<int32_t>(args->at(flutter::EncodableValue{ "width" }));
            //const auto new_height = std::get<int32_t>(args->at(flutter::EncodableValue{ "height" }));
            
            result->Error("NOT_IMPLEMENTED", "Not implemented.");
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