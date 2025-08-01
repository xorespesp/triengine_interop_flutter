#include "triengine_surface_manager.h"
#include "utils/debug_utils.h"

#include <array>
namespace
{
    ComPtr<ID3D11Texture2D> open_shared_texture_from_native_handle(
        ComPtr<ID3D11Device2> dx11_device,
        HANDLE target_shared_texture_handle,
        HANDLE owner_process_handle)
    {
        ComPtr<ID3D11Texture2D> dx11_shared_texture;

        // OpenSharedResource1(혹은 OpenSharedResourceByName)를 사용하여 client측에서 생성한 NT 핸들 획득 & 공유 텍스처 생성
        // (OpenSharedResource1 함수를 사용하는 경우, DuplicateHandle을 사용하여 전달받은 공유 텍스처 핸들을 현재 프로세스에서 유효한 핸들로 복제해야 함)
        HANDLE duplicated_handle{};
        if (!::DuplicateHandle(
            owner_process_handle, // 원본 핸들을 소유하고 있는 소스 프로세스 핸들
            target_shared_texture_handle, // IPC로 수신한 원본 핸들 값
            ::GetCurrentProcess(), // 핸들을 복제해 올 타겟 프로세스 핸들 (현재 프로세스)
            &duplicated_handle, // 복제된 핸들을 저장할 포인터
            0, // 접근 권한 (0은 원본과 동일한 권한임을 의미)
            FALSE, // 핸들 상속 여부
            DUPLICATE_SAME_ACCESS // 원본과 동일한 접근 권한으로 복제
        )) {
            LOG_ERROR("Failed to duplicate shared texture handle. (error: {})", ::GetLastError());
            return nullptr;
        }

        utils::unique_handle duplicated_handle_guard{ duplicated_handle }; // 핸들의 자동 해제를 위한 RAII 핸들 래퍼
        if (HRESULT hr = dx11_device->OpenSharedResource1(
            duplicated_handle_guard.get(),
            IID_PPV_ARGS(&dx11_shared_texture));
            FAILED(hr))
        {
            LOG_ERROR("Failed to open shared texture resource. (HRESULT: {:08X})", static_cast<uint32_t>(hr));
            return nullptr;
        }

        return dx11_shared_texture;
    }

} // namespace

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
    const DXGI_FORMAT frame_format)
{
    std::scoped_lock lk{ _api_lock };
    
    if (this->is_created()) {
        LOG_WARN("Surface manager is already created.");
        return false;
    }

    if (!this->_initialize(
        renderer_ipc_server_name,
        frame_width,
        frame_height,
        frame_format))
    {
        LOG_ERROR("Failed to initialize surface manager");
        return false;
    }

    _fl_created = true;
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

    if (_ipc_cli->is_connected()) {
        _ipc_cli->disconnect();
    }

    // 안전한 리소스 정리를 위해, 먼저 파이프라인에서 관련 리소스들을 분리

    _dx11_device_context2->OMSetRenderTargets(0, nullptr, nullptr);
    _dx11_device_context2->VSSetShader(nullptr, nullptr, 0);
    _dx11_device_context2->PSSetShader(nullptr, nullptr, 0);
    ID3D11ShaderResourceView* nullSRV{ nullptr };
    _dx11_device_context2->PSSetShaderResources(0, 1, &nullSRV);
    ID3D11SamplerState* nullSampler{ nullptr };
    _dx11_device_context2->PSSetSamplers(0, 1, &nullSampler);
    _dx11_device_context2->IASetInputLayout(nullptr);
    _dx11_device_context2->Flush(); // 파이프라인 명령 완료 대기

    // 리소스 시작 (해제 순서에 유의)

    _dx11_rtv.Reset();
    _dx11_srv.Reset();
    _dx11_sampler_state.Reset();
    _dx11_pixel_shader.Reset();
    _dx11_vertex_shader.Reset();

    _dx11_render_texture_handle.reset();
    _dx11_render_texture.Reset();
    _dx11_shared_texture_copy.Reset();
    _dxgi_keyed_mutex.Reset();
    _dx11_shared_texture.Reset();
    _dx11_device_context2.Reset();
    _dx11_device2.Reset();

    _renderer_process_handle.reset();
    _ipc_cli.reset();
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

    packet_builder<ipc_proto::packets::mouse_button_event_t> pck{ ipc_proto::packet_type::mouse_button_event };
    pck.body()->x = x;
    pck.body()->y = y;
    pck.body()->button = button;
    pck.body()->action = action;
    pck.body()->mods = mods;
    if (std::errc{} != _ipc_cli->send_notify(
        pck.data(),
        pck.size())) {
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

    packet_builder<ipc_proto::packets::mouse_move_event_t> pck{ ipc_proto::packet_type::mouse_move_event };
    pck.body()->x = x;
    pck.body()->y = y;
    pck.body()->mods = mods;
    if (std::errc{} != _ipc_cli->send_notify(
        pck.data(),
        pck.size())) {
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

    packet_builder<ipc_proto::packets::mouse_scroll_event_t> pck{ ipc_proto::packet_type::mouse_scroll_event };
    pck.body()->yoffset = yoffset;
    if (std::errc{} != _ipc_cli->send_notify(
        pck.data(),
        pck.size())) {
        LOG_ERROR("Failed to send mouse scroll event.");
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
    
    // 획득해둔 KeyedMutex를 사용하여 렌더링 타이밍 동기화 수행
    // (매 프레임 렌더링 전, GL 렌더러 측이 텍스처 쓰기를 완료할 때까지 대기)
    // -> 뮤텍스를 즉시 얻지 못한 경우, GL 렌더러 측에서 아직 작업 중이거나 렌더링된 프레임이 없음을 의미
    // 
    // NOTE: AcquireSync 함수 사용 시 단순 성공 여부 판단을 위해 SUCCEEDED 매크로만 사용할 경우, 
    //       WAIT_OBJECT_0 반환값을 제외한 나머지 반환 상태값을 제대로 감지하지 못할 수 있음에 유의.
    //       AcquireSync 함수는 다음과 같은 DWORD 상수들을 반환할 수 있다:
    //         - WAIT_OBJECT_0  : KeyedMutex를 성공적으로 획득 -> 렌더링 작업을 계속 진행할 수 있음. (S_OK와 동일한 값)
    //         - WAIT_TIMEOUT   : 지정된 키가 해제되기 전에 타임아웃 간격이 경과했음을 의미.
    //         - WAIT_ABANDONED : SharedSurface와 KeyedMutex가 더 이상 일관된 상태가 아님. 
    //                            이 경우, KeyedMutex와 SharedSurface 둘 다 해제한 후 재생성해야 함.
    //       Ref: https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgikeyedmutex-acquiresync
    switch (
        const HRESULT sync_hr = _dxgi_keyed_mutex->AcquireSync(0/* Key */, 10/* Wait Timeout */);
    sync_hr
        ) {
    case WAIT_OBJECT_0: // KeyedMutex를 성공적으로 획득했으므로 렌더링 작업 진행 가능
        // Shared 텍스처를 Screen 텍스처로 복사 (락 점유 시간을 최소화하기 위해 별도 텍스처로 데이터를 복사한 뒤 렌더링 수행)
        _dx11_device_context2->CopyResource(
            _dx11_shared_texture_copy.Get(), 
            _dx11_shared_texture.Get()
        );
        if (FAILED(_dxgi_keyed_mutex->ReleaseSync(0/* Key */))) { // 텍스처 사용이 끝났으므로 KeyedMutex 잠금 해제
            LOG_ERROR("Failed to release keyed mutex.");
            return false;
        }
        break;
    case WAIT_TIMEOUT: // KeyedMutex를 획득하지 못했으므로 렌더링 작업을 건너뜀
        return true;
    case WAIT_ABANDONED: // KeyedMutex가 더 이상 일관된 상태가 아님. 이 경우, KeyedMutex와 SharedSurface 둘 다 해제한 후 재생성해야 함.
        LOG_ERROR("Failed to acquire keyed mutex. (keyed mutex is no longer in a consistent state)");
        return false;
    }

    // static float t = 0.0f; // 시간에 따라 변하는 색상 계산
    // t += 0.1f;
    // float r = 0.5f + 0.5f * cos(t);
    // float g = 0.5f + 0.5f * sin(t);
    // float b = 0.5f + 0.5f * cos(t + 3.14f);
    // float color[4] = { r, g, b, 1.0f };
    // _dx11_device_context2->ClearRenderTargetView(_dx11_rtv.Get(), color);
    
    D3D11_VIEWPORT viewport;
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width = static_cast<float>(_frame_width);
    viewport.Height = static_cast<float>(_frame_height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    _dx11_device_context2->RSSetViewports(1, &viewport);
    _dx11_device_context2->OMSetRenderTargets(1, _dx11_rtv.GetAddressOf(), nullptr);

    _dx11_device_context2->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    _dx11_device_context2->IASetInputLayout(nullptr); // No input buffer needed for this VS trick

    _dx11_device_context2->VSSetShader(_dx11_vertex_shader.Get(), nullptr, 0);
    _dx11_device_context2->PSSetShader(_dx11_pixel_shader.Get(), nullptr, 0);
    _dx11_device_context2->PSSetShaderResources(0, 1, _dx11_srv.GetAddressOf());
    _dx11_device_context2->PSSetSamplers(0, 1, _dx11_sampler_state.GetAddressOf());

    _dx11_device_context2->Draw(3, 0); // full-screen quad 렌더링 시작 (3 vertices)
    _dx11_device_context2->Flush();

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

    LOG_DEBUG("Resizing frame to {}x{}...", frame_width, frame_height);

    packet_builder<ipc_proto::packets::frame_resize_request_t> req{ ipc_proto::packet_type::frame_resize_request };
    req.body()->width = frame_width;
    req.body()->height = frame_height;

    std::vector<uint8_t> rep_bytes;
    if (std::errc{} != _ipc_cli->send_request_sync(
        req.data(),
        req.size(),
        rep_bytes))
    {
        LOG_ERROR("frame resize request failed.");
        return false;
    }

    packet_view pck_view{ rep_bytes.data(), rep_bytes.size() };
    const auto resize_rep = pck_view.body<ipc_proto::packets::frame_resize_response_t>();
    LOG_TRACE("frame resize response -> resource handle: {}", resize_rep->shared_texture_handle);

    //
    // 새로운 리소스들을 임시 로컬 변수로 생성
    //

    ComPtr<ID3D11Texture2D> new_dx11_shared_texture;
    ComPtr<IDXGIKeyedMutex> new_dxgi_keyed_mutex;
    ComPtr<ID3D11Texture2D> new_dx11_shared_texture_copy;
    ComPtr<ID3D11Texture2D> new_dx11_render_texture;
    utils::unique_handle new_dx11_render_texture_handle;
    ComPtr<ID3D11ShaderResourceView> new_dx11_srv;
    ComPtr<ID3D11RenderTargetView> new_dx11_rtv;

    // 새 공유 텍스처 생성
    new_dx11_shared_texture = open_shared_texture_from_native_handle(
        _dx11_device2,
        resize_rep->shared_texture_handle, // IPC로 받은 새로운 핸들
        _renderer_process_handle.get()
    );
    if (!new_dx11_shared_texture) {
        LOG_ERROR("Failed to open shared texture from native handle");
        return false;
    }
    LOG_DEBUG("Successfully opened shared texture from native handle. (handle: {})", resize_rep->shared_texture_handle);

    // 새 KeyedMutex 생성
    if (FAILED(new_dx11_shared_texture.As(&new_dxgi_keyed_mutex))) {
        LOG_ERROR("Failed to get KeyedMutex from shared texture");
        return false;
    }

    // 새 공유 텍스처 복사본 생성
    D3D11_TEXTURE2D_DESC sharedTexCopyDesc{};
    new_dx11_shared_texture->GetDesc(&sharedTexCopyDesc); // 공유 텍스처의 현재 속성을 가져옴
    ASSERT(sharedTexCopyDesc.Width == static_cast<UINT>(frame_width));
    ASSERT(sharedTexCopyDesc.Height == static_cast<UINT>(frame_height));
    sharedTexCopyDesc.Usage = D3D11_USAGE_DEFAULT; // 일반적인 GPU 읽기/쓰기 용도
    sharedTexCopyDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE; // 셰이더에서 읽을 수 있도록 설정
    sharedTexCopyDesc.CPUAccessFlags = 0; // CPU 접근 없음
    sharedTexCopyDesc.MiscFlags = 0; // 공유 플래그 제거
    if (FAILED(_dx11_device2->CreateTexture2D(
        &sharedTexCopyDesc,
        nullptr,
        &new_dx11_shared_texture_copy))) {
        LOG_ERROR("Failed to create shared texture copy");
        return false;
    }

    // 새 렌더 텍스처 생성
    D3D11_TEXTURE2D_DESC renderTexDesc{};
    _dx11_render_texture->GetDesc(&renderTexDesc);
    renderTexDesc.Width = static_cast<UINT>(frame_width);
    renderTexDesc.Height = static_cast<UINT>(frame_height);
    if (FAILED(_dx11_device2->CreateTexture2D(
        &renderTexDesc, 
        nullptr, 
        &new_dx11_render_texture))) {
        LOG_ERROR("Failed to create render texture");
        return false;
    }
    
    // 새 렌더 텍스처의 공유 핸들 획득 (Flutter 엔진에 전달할 핸들)
    {
        ComPtr<IDXGIResource> dxgi_resource;
        if (FAILED(new_dx11_render_texture.As(&dxgi_resource))) {
            LOG_ERROR("Failed to get IDXGIResource from render target texture");
            return false;
        }

        if (HANDLE shared_handle{ nullptr };
            SUCCEEDED(dxgi_resource->GetSharedHandle(&shared_handle))) {
            new_dx11_render_texture_handle.reset(shared_handle);
        } else {
            LOG_ERROR("Failed to get shared handle from IDXGIResource");
            if (shared_handle) { ::CloseHandle(shared_handle); }
            return false;
        }

        LOG_DEBUG("Created shared texture handle: {:p}", new_dx11_render_texture_handle.get());
    }

    // 새 SRV 생성 (공유 텍스처 복사본에서 생성)
    if (FAILED(_dx11_device2->CreateShaderResourceView(
        new_dx11_shared_texture_copy.Get(), 
        nullptr, 
        &new_dx11_srv))) {
        LOG_ERROR("Failed to create shader resource view");
        return false;
    }

    // 새 RTV 생성
    if (FAILED(_dx11_device2->CreateRenderTargetView(
        new_dx11_render_texture.Get(), 
        nullptr, 
        &new_dx11_rtv))) {
        LOG_ERROR("Failed to create render target view");
        return false;
    }

    //
    // 모든 리소스 생성에 성공했으므로 기존 리소스와 스왑
    //

    // 먼저 렌더 타겟을 파이프라인에서 분리해야 함
    _dx11_device_context2->OMSetRenderTargets(0, nullptr, nullptr);

    // 기존 리소스들을 새 리소스들로 교체
    _dx11_rtv.Swap(new_dx11_rtv);
    _dx11_srv.Swap(new_dx11_srv);
    _dx11_shared_texture.Swap(new_dx11_shared_texture);
    _dxgi_keyed_mutex.Swap(new_dxgi_keyed_mutex);
    _dx11_shared_texture_copy.Swap(new_dx11_shared_texture_copy);
    _dx11_render_texture.Swap(new_dx11_render_texture);
    _dx11_render_texture_handle.swap(new_dx11_render_texture_handle);

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
    const DXGI_FORMAT frame_format)
{
    //
    // 임시 변수들로 모든 리소스를 먼저 생성
    //

    ////////////////////////////////////////////////////////////////////////////////////////////////////
    LOG_DEBUG("Connecting to IPC server '{}' ...", renderer_ipc_server_name);

    std::shared_ptr<ipc_client> new_ipc_cli;
    new_ipc_cli = std::make_shared<ipc_client>();
    new_ipc_cli->set_disconnect_callback(
        [this]()
        {
            LOG_WARN("Disconnected from IPC server!");
            ASSERT(_fl_created.load() == false);
        });

    if (!new_ipc_cli->connect(renderer_ipc_server_name)) {
        LOG_ERROR("Failed to connect to IPC server");
        return false;
    }
    LOG_DEBUG("Connected!");

    // 초기화 요청 전송
    packet_builder<ipc_proto::packets::init_request_t> init_req{ ipc_proto::packet_type::init_request };
    init_req.body()->frame_width = frame_width;
    init_req.body()->frame_height = frame_height;
    init_req.body()->frame_format = frame_format;

    LOG_DEBUG("Sending init request to renderer server... (frame size: {}x{}, format: {})"
        , frame_width, frame_height, static_cast<int32_t>(frame_format)
    );
    std::vector<uint8_t> init_rep_bytes;
    if (std::errc{} != new_ipc_cli->send_request_sync(
        init_req.data(),
        init_req.size(),
        init_rep_bytes))
    {
        LOG_ERROR("Failed to send init request");
        return false;
    }

    packet_view init_rep_pck_view{ init_rep_bytes.data(), init_rep_bytes.size() };
    const auto init_rep = init_rep_pck_view.body<ipc_proto::packets::init_response_t>();
    LOG_DEBUG("Received init response. (pid: {}, adapter: {:x}-{:x}, resource handle: {})"
        , init_rep->renderer_process_id
        , init_rep->target_adapter_luid.HighPart
        , init_rep->target_adapter_luid.LowPart
        , init_rep->shared_texture_handle
    );

    LOG_DEBUG("Opening renderer process handle...");
    utils::unique_handle new_renderer_process_handle;
    new_renderer_process_handle.reset(::OpenProcess(
        PROCESS_DUP_HANDLE | SYNCHRONIZE,
        FALSE,
        init_rep->renderer_process_id
    ));
    if (!new_renderer_process_handle.get()) {
        LOG_ERROR("Failed to open renderer process handle");
        return false;
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////
    LOG_DEBUG("Initializing D3D resources...");

    // NOTE: 반드시 CreateDXGIFactory2 함수를 사용해서 DXGI 1.2 버전 이상의 DXGI 팩토리(`IDXGIFactory`)를 생성해줘야 함.
    // (`ID3D11Device::CreateTexture2D: D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX is only available for devices created off of Dxgi1.1 factories or later.` D3D11 오류 방지)
    ComPtr<IDXGIFactory2> dxgi_factory2;
    if (FAILED(::CreateDXGIFactory2(0, IID_PPV_ARGS(&dxgi_factory2)))) {
        LOG_ERROR("Failed to create DXGI factory");
        return false;
    }

    ComPtr<IDXGIAdapter> target_dxgi_adapter;
    ComPtr<IDXGIAdapter> dxgi_adapter0;
    for (UINT i = 0; dxgi_factory2->EnumAdapters(i, &dxgi_adapter0) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC desc;
        dxgi_adapter0->GetDesc(&desc);
        if (0 == std::memcmp(&desc.AdapterLuid, &init_rep->target_adapter_luid, sizeof(LUID))) {
            target_dxgi_adapter = dxgi_adapter0;
            break;
        }
    }
    
    if (!target_dxgi_adapter) {
        LOG_ERROR("Matching adapter not found");
        return false;
    }

    UINT deviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
    deviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    ComPtr<ID3D11Device> dx11_device0;
    ComPtr<ID3D11DeviceContext> dx11_device_context0;
    if (FAILED(::D3D11CreateDevice(        
        target_dxgi_adapter.Get(),
        D3D_DRIVER_TYPE_UNKNOWN,
        nullptr,
        deviceFlags,
        nullptr,
        0,
        D3D11_SDK_VERSION,
        &dx11_device0,
        nullptr,
        &dx11_device_context0
    ))) {
        LOG_ERROR("Failed to create D3D11 device");
        return false;
    }

    // Convert `ID3D11Device` -> `ID3D11Device2` (Higher version object)
    ComPtr<ID3D11Device2> new_dx11_device2;
    if (FAILED(dx11_device0.As(&new_dx11_device2))) {
        LOG_ERROR("Failed to convert to ID3D11Device2");
        return false;
    }

    // Convert `ID3D11DeviceContext` -> `ID3D11DeviceContext2` (Higher version object)
    ComPtr<ID3D11DeviceContext2> new_dx11_device_context2;
    if (FAILED(dx11_device_context0.As(&new_dx11_device_context2))) {
        LOG_ERROR("Failed to convert to ID3D11DeviceContext2");
        return false;
    }

    // 공유 텍스처 핸들 Open
    ComPtr<ID3D11Texture2D> new_dx11_shared_texture = open_shared_texture_from_native_handle(
        new_dx11_device2,
        init_rep->shared_texture_handle, // IPC로 받은 새로운 핸들
        new_renderer_process_handle.get()
    );
    if (new_dx11_shared_texture) {
        LOG_DEBUG("Successfully opened shared texture from native handle. (handle: {})", init_rep->shared_texture_handle);
    } else {
        LOG_ERROR("Failed to open shared texture from native handle");
        return false;
    }

    // 획득한 공유 텍스처에서 KeyedMutex 인터페이스 획득
    ComPtr<IDXGIKeyedMutex> new_dxgi_keyed_mutex;
    if (FAILED(new_dx11_shared_texture.As(&new_dxgi_keyed_mutex))) {
        LOG_ERROR("Failed to get KeyedMutex from shared texture");
        return false;
    }

    // 공유 텍스처의 복사본 생성 (공유 텍스처를 y축 flip 시 사용할 임시 복사본 텍스처)
    ComPtr<ID3D11Texture2D> new_dx11_shared_texture_copy;
    {
        D3D11_TEXTURE2D_DESC sharedTexCopyDesc;
        new_dx11_shared_texture->GetDesc(&sharedTexCopyDesc); // 공유 텍스처의 현재 속성을 가져옴
        sharedTexCopyDesc.Usage = D3D11_USAGE_DEFAULT; // 일반적인 GPU 읽기/쓰기 용도
        sharedTexCopyDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE; // 셰이더에서 읽을 수 있도록 설정
        sharedTexCopyDesc.CPUAccessFlags = 0; // CPU 접근 없음
        sharedTexCopyDesc.MiscFlags = 0; // 공유 플래그 제거
        if (FAILED(new_dx11_device2->CreateTexture2D(
            &sharedTexCopyDesc, 
            nullptr, 
            &new_dx11_shared_texture_copy
        ))) {
            LOG_ERROR("Failed to create shared texture copy");
            return false;
        }
    }

    // 화면 렌더링용 렌더링 텍스처 생성
    ComPtr<ID3D11Texture2D> new_dx11_render_texture;
    {
        D3D11_TEXTURE2D_DESC renderTexDesc{};
        renderTexDesc.Width = static_cast<UINT>(frame_width);
        renderTexDesc.Height = static_cast<UINT>(frame_height);
        renderTexDesc.MipLevels = 1;
        renderTexDesc.ArraySize = 1;
        renderTexDesc.Format = frame_format;
        renderTexDesc.SampleDesc.Count = 1;
        renderTexDesc.SampleDesc.Quality = 0;
        renderTexDesc.Usage = D3D11_USAGE_DEFAULT;
        renderTexDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        renderTexDesc.CPUAccessFlags = 0;
        renderTexDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
        if (FAILED(new_dx11_device2->CreateTexture2D(&renderTexDesc, nullptr, &new_dx11_render_texture))) {
            LOG_ERROR("Failed to create render texture");
            return false;
        }
    }

    // 렌더 텍스처의 공유 핸들 획득 (Flutter 엔진에 전달할 핸들)
    utils::unique_handle new_dx11_render_texture_handle;
    {
        ComPtr<IDXGIResource> dxgi_resource;
        if (FAILED(new_dx11_render_texture.As(&dxgi_resource))) {
            LOG_ERROR("Failed to get IDXGIResource from render target texture");
            return false;
        }

        if (HANDLE shared_handle{ nullptr };
            SUCCEEDED(dxgi_resource->GetSharedHandle(&shared_handle))) {
            new_dx11_render_texture_handle.reset(shared_handle);
        } else {
            LOG_ERROR("Failed to get shared handle from IDXGIResource");
            if (shared_handle) { ::CloseHandle(shared_handle); }
            return false;
        }

        LOG_DEBUG("Created shared texture handle: {:p}", new_dx11_render_texture_handle.get());
    }

    // Full-screen Quad 렌더링을 위한 셰이더 및 리소스 생성
    // OpenGL의 공유 텍스처를 y축 기준으로 뒤집어서 렌더링하기 위한 screen quad 셰이더 코드
    static const std::string shaderCode = R"hlsl(
        Texture2D tx : register(t0);
        SamplerState smp : register(s0);

        struct VS_OUT {
            float4 pos : SV_POSITION;
            float2 uv : TEXCOORD;
        };

        VS_OUT VS(uint id : SV_VertexID) {
            VS_OUT output;
            // Full-screen triangle UVs: (0,0), (2,0), (0,2)
            output.uv = float2((id << 1) & 2, id & 2); 
            // Full-screen triangle positions: (-1,1), (3,1), (-1,-3)
            output.pos = float4(output.uv * 2.0f - 1.0f, 0.0f, 1.0f);
            // Flip Y for correct rendering
            output.pos.y = -output.pos.y;
            return output;
        }

        float4 PS(VS_OUT input) : SV_TARGET {
            return tx.Sample(smp, float2(input.uv.x, 1.0 - input.uv.y)); // flip Y-axis
            // return tx.Sample(smp, input.uv);
        }
    )hlsl";

    ComPtr<ID3DBlob> vsBlob, psBlob, errBlob;
    HRESULT hr = ::D3DCompile(
        shaderCode.c_str(), shaderCode.size(),
        nullptr, nullptr, nullptr,
        "VS",
        "vs_5_0",
        0, 0,
        &vsBlob, &errBlob
    );
    if (FAILED(hr)) {
        LOG_ERROR("Failed to compile vertex shader({}): {}"
            , hr
            , errBlob ? static_cast<const char*>(errBlob->GetBufferPointer()) : "???"
        );
        return false;
    }

    hr = ::D3DCompile(
        shaderCode.c_str(), shaderCode.size(),
        nullptr, nullptr, nullptr,
        "PS",
        "ps_5_0",
        0, 0,
        &psBlob,
        &errBlob
    );
    if (FAILED(hr)) {
        LOG_ERROR("Failed to compile pixel shader({}): {}"
            , hr
            , errBlob ? static_cast<const char*>(errBlob->GetBufferPointer()) : "???"
        );
        return false;
    }

    // 버텍스 셰이더 생성
    ComPtr<ID3D11VertexShader> new_dx11_vertex_shader;
    if (FAILED(new_dx11_device2->CreateVertexShader(
        vsBlob->GetBufferPointer(), 
        vsBlob->GetBufferSize(), 
        nullptr, 
        &new_dx11_vertex_shader))) {
        LOG_ERROR("Failed to create vertex shader");
        return false;
    }

    // 픽셀 셰이더 생성
    ComPtr<ID3D11PixelShader> new_dx11_pixel_shader;
    if (FAILED(new_dx11_device2->CreatePixelShader(
        psBlob->GetBufferPointer(), 
        psBlob->GetBufferSize(), 
        nullptr, 
        &new_dx11_pixel_shader))) {
        LOG_ERROR("Failed to create pixel shader");
        return false;
    }

    // D3D11 샘플러 상태 생성 (OpenGL의 sampler2D와 유사한 개념)
    // (텍스처 필터링 및 주소 모드 설정)
    ComPtr<ID3D11SamplerState> new_dx11_sampler_state;
    {
        D3D11_SAMPLER_DESC sampDesc{};
        sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
        sampDesc.MinLOD = 0;
        sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
        if (FAILED(new_dx11_device2->CreateSamplerState(
            &sampDesc, 
            &new_dx11_sampler_state
        ))) {
            LOG_ERROR("Failed to create sampler state");
            return false;
        }
    }

    // 복사본 공유 텍스처에 대한 셰이더 리소스 뷰 (SRV) 생성
    // Equivalent of: `glBindTexture`+ `sampler2D`
    ComPtr<ID3D11ShaderResourceView> new_dx11_srv;
    if (FAILED(new_dx11_device2->CreateShaderResourceView(
        new_dx11_shared_texture_copy.Get(),
        nullptr,
        &new_dx11_srv
    ))) {
        LOG_ERROR("Failed to create shader resource view");
        return false;
    }

    // 렌더 타겟 뷰 (RTV) 생성
    ComPtr<ID3D11RenderTargetView> new_dx11_rtv;
    if (FAILED(new_dx11_device2->CreateRenderTargetView(
        new_dx11_render_texture.Get(),
        nullptr,
        &new_dx11_rtv
    ))) {
        LOG_ERROR("Failed to create render target view");
        return false;
    }

    //
    // 모든 리소스 생성에 성공했으므로 멤버 변수들을 업데이트
    //

    _ipc_cli = std::move(new_ipc_cli);
    _renderer_process_handle = std::move(new_renderer_process_handle);

    _dx11_device2 = std::move(new_dx11_device2);
    _dx11_device_context2 = std::move(new_dx11_device_context2);
    _dx11_shared_texture = std::move(new_dx11_shared_texture);
    _dxgi_keyed_mutex = std::move(new_dxgi_keyed_mutex);
    _dx11_shared_texture_copy = std::move(new_dx11_shared_texture_copy);
    _dx11_render_texture = std::move(new_dx11_render_texture);
    _dx11_render_texture_handle = std::move(new_dx11_render_texture_handle);

    _dx11_vertex_shader = std::move(new_dx11_vertex_shader);
    _dx11_pixel_shader = std::move(new_dx11_pixel_shader);
    _dx11_sampler_state = std::move(new_dx11_sampler_state);
    _dx11_srv = std::move(new_dx11_srv);
    _dx11_rtv = std::move(new_dx11_rtv);

    _frame_width = frame_width;
    _frame_height = frame_height;

    return true;
}