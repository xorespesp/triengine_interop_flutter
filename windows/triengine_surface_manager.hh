#pragma once
#include <dxgi1_2.h> // DXGI 1.2 API header
#include <d3d11_2.h> // DX11.2 API header
#include <wrl/client.h> // Microsoft::WRL::ComPtr
#include <flutter_texture_registrar.h> // FlutterDesktopPixelFormat

#include <string_view>
#include <memory>
#include <atomic>

#include <triengine_interop/surface/proto/surface_proto.hh>
#include <triengine_interop/surface/surface_consumer.hh>

#include "utils/spin_lock.hh"
#include "utils/win32_utils.hh"

using Microsoft::WRL::ComPtr;

class triengine_surface_manager
{
public:
    triengine_surface_manager();
    ~triengine_surface_manager();

    // Disallow copy and assign.
    triengine_surface_manager(const triengine_surface_manager&) = delete;
    triengine_surface_manager& operator=(const triengine_surface_manager&) = delete;

    int32_t get_width() const;
    int32_t get_height() const;
    void* get_surface_handle() const;

    bool is_created() const;

    [[nodiscard]]
    bool create(
        std::string_view renderer_ipc_server_name,
        int32_t frame_width, 
        int32_t frame_height, 
        FlutterDesktopPixelFormat frame_format
    );

    void destroy();

    [[nodiscard]]
    bool send_mouse_button_event(
        int32_t x,
        int32_t y,
        triengine_interop::surface::proto::mouse_button_type button,
        triengine_interop::surface::proto::button_action_type action,
        triengine_interop::surface::proto::modifier_button_type mods
    );

    [[nodiscard]]
    bool send_mouse_move_event(
        int32_t x,
        int32_t y,
        triengine_interop::surface::proto::modifier_button_type mods
    );

    [[nodiscard]]
    bool send_mouse_scroll_event(
        float yoffset
    );

    [[nodiscard]]
    bool render_frame();
    
    [[nodiscard]]
    bool resize_frame(
        int32_t frame_width, 
        int32_t frame_height
    );

private:
    [[nodiscard]]
    bool _initialize(
        std::string_view renderer_ipc_server_name,
        int32_t frame_width, 
        int32_t frame_height, 
        DXGI_FORMAT frame_dxgi_format
    );

    // Create the Flutter-facing render target (a SHARED render texture, its exported
    // NT handle, and an RTV) on the consumer's device.
    [[nodiscard]]
    bool _create_render_target(
        int32_t frame_width,
        int32_t frame_height,
        DXGI_FORMAT frame_dxgi_format,
        ComPtr<ID3D11Texture2D>& out_texture,
        utils::unique_handle& out_handle,
        ComPtr<ID3D11RenderTargetView>& out_rtv
    );

private:
    // api lock for thread safety
    mutable utils::spin_lock _api_lock;

    // Shared-surface consumer (owns the IPC connection + the D3D device/surface interop)
    triengine_interop::surface::surface_consumer _consumer;

    // Present target owned by the manager: a SHARED render texture whose handle is
    // handed to the Flutter engine. Created on _consumer.get_dx11_device().
    ComPtr<ID3D11Texture2D> _dx11_render_texture; // render texture, to be used in Flutter
    utils::unique_handle _dx11_render_texture_handle; // shared handle for the render texture (to be used in Flutter)
    
    // D3D Pipeline Resources
    ComPtr<ID3D11RenderTargetView> _dx11_rtv;

    int32_t _frame_width{ 0 };
    int32_t _frame_height{ 0 };

    std::atomic_bool _fl_created{ false };
};