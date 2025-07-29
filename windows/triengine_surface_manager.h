#pragma once
#include <d3d11.h>
#include <dxgi1_2.h> // DXGI 1.2 API header
#include <d3d11_2.h> // DX11.2 API header
#include <d3dcompiler.h>
#include <wrl/client.h> // Microsoft::WRL::ComPtr

#include "utils/spin_lock.h"
#include "utils/win32_utils.h"
#include "ipc_proto.hh"
#include "ipc_service.hh"

#include <condition_variable>
#include <optional>
#include <thread>
#include <chrono>

using Microsoft::WRL::ComPtr;

namespace Config
{
    constexpr const char* RENDERER_PROCESS_INST_NAME = "Global\\TriengineInterprocRendererServer";
    constexpr const char* RENDERER_SERVER_NAME = "triengine-interproc-renderer-srv";
}

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
    bool create(int32_t frame_width, int32_t frame_height);
    void destroy();
    void render_frame();
    bool resize_frame(int32_t frame_width, int32_t frame_height);

private:
    void _connect_to_renderer_process();
    void _init_d3d_resources(int32_t frame_width, int32_t frame_height);

private:
    // Process/IPC
    std::shared_ptr<ipc_client> _ipc_cli;
    utils::unique_handle _renderer_process_handle;

    std::optional<ipc_proto::packets::shared_render_context_t> _shared_info;
    std::condition_variable _shared_info_cv;
    mutable std::mutex _shared_info_mtx;

    // D3D Resources (DX11.2 API base)
    ComPtr<IDXGIAdapter> _dxgi_adapter;
    ComPtr<ID3D11Device2> _dx11_device2;
    ComPtr<ID3D11DeviceContext2> _dx11_device_context2;
    ComPtr<ID3D11RenderTargetView> _dx11_rtv;
    ComPtr<ID3D11Texture2D> _dx11_shared_texture;
    ComPtr<ID3D11Texture2D> _dx11_shared_texture_copy;
    ComPtr<ID3D11Texture2D> _dx11_render_texture;
    ComPtr<IDXGIKeyedMutex> _dxgi_keyed_mutex;
    utils::unique_handle _shared_texture_handle;

    // Shader Resources
    ComPtr<ID3D11VertexShader> _dx11_vertex_shader;
    ComPtr<ID3D11PixelShader> _dx11_pixel_shader;
    ComPtr<ID3D11ShaderResourceView> _dx11_srv;
    ComPtr<ID3D11SamplerState> _dx11_sampler_state;

    std::thread _render_thread;
    std::atomic_bool _render_thread_running{ false };
    mutable utils::spin_lock _render_lock;

    int32_t _frame_width{ 0 };
    int32_t _frame_height{ 0 };

    bool _fl_created{ false };
};