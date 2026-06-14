#include "triengine_surface_manager.hh"
#include "utils/debug_utils.hh"

#include <array>

// Short namespace aliases for the triengine_interop surface protocol and API.
namespace ipc_proto = triengine_interop::surface::proto;
namespace ipc_surface = triengine_interop::surface;

using namespace std::chrono_literals;

triengine_surface_manager::triengine_surface_manager()
{ }

triengine_surface_manager::~triengine_surface_manager()
{
    if (this->is_created()) {
        this->destroy();
    }
}

int32_t triengine_surface_manager::get_width() const
{
    std::scoped_lock lk{ _api_lock };
    return _frame_width;
}

int32_t triengine_surface_manager::get_height() const
{
    std::scoped_lock lk{ _api_lock };
    return _frame_height;
}

void* triengine_surface_manager::get_surface_handle() const
{
    std::scoped_lock lk{ _api_lock };
    return _dx11_render_texture_handle.get();
}

bool triengine_surface_manager::is_created() const
{
    return _fl_created.load();
}

bool triengine_surface_manager::create(
    const std::string_view renderer_ipc_server_name,
    const int32_t frame_width,
    const int32_t frame_height,
    const FlutterDesktopPixelFormat frame_format)
{
    std::scoped_lock lk{ _api_lock };
    
    if (this->is_created()) {
        LOG_WARN("Surface manager is already created.");
        return false;
    }

    const DXGI_FORMAT frame_dxgi_format = [frame_format]() -> DXGI_FORMAT {
        switch (frame_format) {
        case kFlutterDesktopPixelFormatBGRA8888: return DXGI_FORMAT_B8G8R8A8_UNORM;
        case kFlutterDesktopPixelFormatRGBA8888: return DXGI_FORMAT_R8G8B8A8_UNORM;
        default: return DXGI_FORMAT_UNKNOWN;
        }
    }();

    if (frame_dxgi_format == DXGI_FORMAT_UNKNOWN) {
        LOG_ERROR("Unsupported frame format {}", static_cast<int>(frame_format));
        return false;
    }

    if (!this->_initialize(
        renderer_ipc_server_name,
        frame_width,
        frame_height,
        frame_dxgi_format))
    {
        LOG_ERROR("Failed to initialize surface manager");
        return false;
    }

    _fl_created = true;
    LOG_DEBUG("Surface manager created successfully.");
    return true;
}

void triengine_surface_manager::destroy()
{
    std::scoped_lock lk{ _api_lock };

    if (!this->is_created()) {
        return;
    }

    LOG_DEBUG("Destroying surface manager...");

    _fl_created = false;

    // Detach and release the manager-owned present target
    // before the consumer's device is destroyed.
    if (_consumer.get_dx11_context()) {
        _consumer.get_dx11_context()->OMSetRenderTargets(0, nullptr, nullptr);
        _consumer.get_dx11_context()->Flush();
    }

    _dx11_rtv.Reset();
    _dx11_render_texture_handle.reset();
    _dx11_render_texture.Reset();

    // Destroy the surface interop and disconnect from the renderer.
    _consumer.disconnect();

    LOG_DEBUG("Surface manager destroyed successfully.");
}

bool triengine_surface_manager::send_mouse_button_event(
    const int32_t x,
    const int32_t y,
    const ipc_proto::mouse_button_type button,
    const ipc_proto::button_action_type action,
    const ipc_proto::modifier_button_type mods)
{
    std::scoped_lock lk{ _api_lock };
    if (!this->is_created()) {
        LOG_ERROR("Surface manager is not created.");
        return false;
    }

    if (std::errc{} != _consumer.send_mouse_button_event(
        POINT{ x, y }, button, action, mods)) {
        LOG_ERROR("Failed to send mouse button event.");
        return false;
    }
    
    return true;
}

bool triengine_surface_manager::send_mouse_move_event(
    const int32_t x,
    const int32_t y,
    const ipc_proto::modifier_button_type mods)
{
    std::scoped_lock lk{ _api_lock };
    if (!this->is_created()) {
        LOG_ERROR("Surface manager is not created.");
        return false;
    }

    if (std::errc{} != _consumer.send_mouse_move_event(POINT{ x, y }, mods)) {
        LOG_ERROR("Failed to send mouse move event.");
        return false;
    }

    return true;
}

bool triengine_surface_manager::send_mouse_scroll_event(
    const float yoffset)
{
    std::scoped_lock lk{ _api_lock };
    if (!this->is_created()) {
        LOG_ERROR("Surface manager is not created.");
        return false;
    }

    if (std::errc{} != _consumer.send_mouse_scroll_event(yoffset)) {
        LOG_ERROR("Failed to send mouse scroll event.");
        return false;
    }

    return true;
}

bool triengine_surface_manager::send_key_event(
    const ipc_proto::key_button_type key,
    const ipc_proto::button_action_type action,
    const ipc_proto::modifier_button_type mods)
{
    std::scoped_lock lk{ _api_lock };
    if (!this->is_created()) {
        LOG_ERROR("Surface manager is not created.");
        return false;
    }

    if (std::errc{} != _consumer.send_key_event(key, action, mods)) {
        LOG_ERROR("Failed to send key event.");
        return false;
    }

    return true;
}

bool triengine_surface_manager::render_frame()
{
    std::scoped_lock lk{ _api_lock };

    if (!this->is_created()) {
        LOG_ERROR("Surface manager is not created.");
        return false;
    }

    // Pull the latest renderer frame into the consumer's copy texture. A false
    // return means the shared surface became inconsistent (renderer likely crashed).
    if (!_consumer.sync_latest_frame(10 /* ms */)) {
        LOG_ERROR("Failed to synchronize the shared surface frame.");
        return false;
    }

    D3D11_VIEWPORT viewport{};
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width = static_cast<float>(_frame_width);
    viewport.Height = static_cast<float>(_frame_height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    // Blit the copy onto the Flutter-facing render texture, then submit the GPU
    // commands so the Flutter engine (a different device) can sample it.
    _consumer.blit_to_render_target(_dx11_rtv.Get(), viewport);
    _consumer.flush();

    // log fps
    static int frameCount = 0;
    ++frameCount;
    static auto lastTime = std::chrono::steady_clock::now();
    const auto currentTime = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::seconds>(currentTime - lastTime) >= 1s)
    {
        LOG_DEBUG("Render FPS: {}", frameCount);
        lastTime = currentTime;
        frameCount = 0;
    }

    return true;
}

bool triengine_surface_manager::resize_frame(
    const int32_t frame_width, 
    const int32_t frame_height)
{
    std::scoped_lock lk{ _api_lock };

    if (!this->is_created()) {
        LOG_ERROR("Surface manager is not created.");
        return false;
    }

    LOG_DEBUG("Resizing frame to {}x{}...", frame_width, frame_height);

    // Keep the render texture's pixel format across the resize.
    D3D11_TEXTURE2D_DESC cur_desc{};
    _dx11_render_texture->GetDesc(&cur_desc);

    // Build the new render target into locals first (no state change on failure).
    ComPtr<ID3D11Texture2D> new_render_texture;
    utils::unique_handle new_render_texture_handle;
    ComPtr<ID3D11RenderTargetView> new_rtv;
    if (!this->_create_render_target(
        frame_width, frame_height, cur_desc.Format,
        new_render_texture, new_render_texture_handle, new_rtv))
    {
        return false;
    }

    // Resize the shared-surface side (the consumer requests the renderer resize and
    // recreates the shared texture / copy / SRV).
    if (!_consumer.resize_frame(SIZE{ frame_width, frame_height })) {
        LOG_ERROR("Failed to resize the shared surface.");
        return false;
    }

    // Swap in the new present target.
    _consumer.get_dx11_context()->OMSetRenderTargets(0, nullptr, nullptr);
    _dx11_render_texture = std::move(new_render_texture);
    _dx11_render_texture_handle = std::move(new_render_texture_handle);
    _dx11_rtv = std::move(new_rtv);
    
    // 프레임 크기 업데이트
    _frame_width = frame_width;
    _frame_height = frame_height;

    LOG_DEBUG("All DX resources have been successfully synchronized.");
    return true;
}

bool triengine_surface_manager::_initialize(
    const std::string_view renderer_ipc_server_name,
    const int32_t frame_width,
    const int32_t frame_height,
    const DXGI_FORMAT frame_dxgi_format)
{
    LOG_DEBUG("Connecting to IPC server '{}' ...", renderer_ipc_server_name);

    _consumer.set_disconnect_callback(
        [this]()
        {
            LOG_WARN("Disconnected from IPC server!");
            ASSERT(_fl_created.load() == false);
        });

    // The consumer connects, performs the init handshake (magic/version), picks the
    // renderer's adapter, opens the shared surface and builds the blit pipeline. The
    // renderer outputs RGBA; the GPU swizzles RGBA<->BGRA automatically on load/store,
    // so only a Y-flip is requested (no manual channel conversion).
    ipc_surface::surface_render_options cfg;
    cfg.flip_y = true;
    cfg.convert_rgba_to_bgra = false;
    if (!_consumer.connect(
        renderer_ipc_server_name, 
        SIZE{ frame_width, frame_height }, 
        cfg))
    {
        LOG_ERROR("Failed to connect / create surface consumer");
        return false;
    }
    LOG_DEBUG("Connected!");

    // Create the Flutter-facing render target on the consumer's device.
    ComPtr<ID3D11Texture2D> new_render_texture;
    utils::unique_handle new_render_texture_handle;
    ComPtr<ID3D11RenderTargetView> new_rtv;
    if (!this->_create_render_target(
        frame_width, frame_height, 
        frame_dxgi_format,
        new_render_texture, 
        new_render_texture_handle, 
        new_rtv))
    {
        _consumer.disconnect();
        return false;
    }

    // Commit
    _dx11_render_texture = std::move(new_render_texture);
    _dx11_render_texture_handle = std::move(new_render_texture_handle);
    _dx11_rtv = std::move(new_rtv);
    _frame_width = frame_width;
    _frame_height = frame_height;

    return true;
}

bool triengine_surface_manager::_create_render_target(
    const int32_t frame_width,
    const int32_t frame_height,
    const DXGI_FORMAT frame_dxgi_format,
    ComPtr<ID3D11Texture2D>& out_texture,
    utils::unique_handle& out_handle,
    ComPtr<ID3D11RenderTargetView>& out_rtv)
{
    ID3D11Device2* const device = _consumer.get_dx11_device();
    if (!device) {
        LOG_ERROR("Consumer device is not available");
        return false;
    }

    // Flutter 엔진에 전달할 렌더 텍스처 생성
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = static_cast<UINT>(frame_width);
    desc.Height = static_cast<UINT>(frame_height);
    desc.Format = frame_dxgi_format;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;
    desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED; // <- for `kFlutterDesktopGpuSurfaceTypeDxgiSharedHandle`

    ComPtr<ID3D11Texture2D> texture;
    if (FAILED(device->CreateTexture2D(&desc, nullptr, &texture))) {
        LOG_ERROR("Failed to create render texture");
        return false;
    }

    // 렌더 텍스처의 공유 핸들 획득 (Flutter 엔진에 전달할 핸들)
    utils::unique_handle handle;
    {
        ComPtr<IDXGIResource> dxgi_resource;
        if (FAILED(texture.As(&dxgi_resource))) {
            LOG_ERROR("Failed to get IDXGIResource from render texture");
            return false;
        }

        HANDLE shared_handle{ nullptr };
        if (SUCCEEDED(dxgi_resource->GetSharedHandle(&shared_handle))) {
            handle.reset(shared_handle);
        } else {
            LOG_ERROR("Failed to get shared handle from render texture");
            if (shared_handle) { ::CloseHandle(shared_handle); }
            return false;
        }

        LOG_DEBUG("Created shared render texture handle: {:p}", handle.get());
    }

    ComPtr<ID3D11RenderTargetView> rtv;
    if (FAILED(device->CreateRenderTargetView(texture.Get(), nullptr, &rtv))) {
        LOG_ERROR("Failed to create render target view");
        return false;
    }

    out_texture = std::move(texture);
    out_handle = std::move(handle);
    out_rtv = std::move(rtv);
    return true;
}