#pragma once
#include <dxgi1_2.h> // DXGI 1.2 API header
#include <d3d11_2.h> // DX11.2 API header
#include <wrl/client.h> // Microsoft::WRL::ComPtr
#include <flutter_texture_registrar.h> // FlutterDesktopPixelFormat

#include <string_view>
#include <memory>
#include <atomic>

#include "utils/spin_lock.hh"
#include "utils/win32_utils.hh"
#include "ipc_proto.hh"
#include "ipc_service.hh"

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
        ipc_proto::mouse_button_type button,
        ipc_proto::button_action_type action,
        ipc_proto::modifier_button_type mods
    );

    [[nodiscard]]
    bool send_mouse_move_event(
        int32_t x,
        int32_t y,
        ipc_proto::modifier_button_type mods
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

private:
    // api lock for thread safety
    mutable utils::spin_lock _api_lock;

    // Process/IPC
    std::shared_ptr<ipc_client> _ipc_cli;
    utils::unique_handle _renderer_process_handle;

    // D3D Resources (DX11.2 API base)
    ComPtr<ID3D11Device2> _dx11_device2;
    ComPtr<ID3D11DeviceContext2> _dx11_device_context2;
    ComPtr<ID3D11Texture2D> _dx11_shared_texture; // shared texture from the renderer process
    ComPtr<IDXGIKeyedMutex> _dxgi_shared_texture_mutex; // KeyedMutex for the shared texture
    ComPtr<ID3D11Texture2D> _dx11_shared_texture_copy; // copy of the shared texture (temporary texture)
    ComPtr<ID3D11Texture2D> _dx11_render_texture; // render texture, to be used in Flutter
    utils::unique_handle _dx11_render_texture_handle; // shared handle for the render texture (to be used in Flutter)

    // D3D Pipeline Resources
    ComPtr<ID3D11VertexShader> _dx11_vertex_shader;
    ComPtr<ID3D11PixelShader> _dx11_pixel_shader;
    ComPtr<ID3D11SamplerState> _dx11_sampler_state;
    ComPtr<ID3D11ShaderResourceView> _dx11_srv;
    ComPtr<ID3D11RenderTargetView> _dx11_rtv;

    int32_t _frame_width{ 0 };
    int32_t _frame_height{ 0 };

    std::atomic_bool _fl_created{ false };
};