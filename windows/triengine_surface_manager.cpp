#include "triengine_surface_manager.h"
#include "utils/debug_utils.h"

#include <array>

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
    return _frame_width;
}

int32_t triengine_surface_manager::get_height() const
{
    return _frame_height;
}

void* triengine_surface_manager::get_surface_handle() const
{
    //return static_cast<void*>(_dx11_render_texture.Get());
    return _shared_texture_handle.get();
}

bool triengine_surface_manager::is_created() const
{
    return _fl_created;
}

bool triengine_surface_manager::create(int32_t frame_width, int32_t frame_height)
{
    try
    {
        if (this->is_created()) {
            LOG_WARN("Surface manager is already created.");
            return false;
        }

        this->_connect_to_renderer_process();
        this->_init_d3d_resources(frame_width, frame_height);

        _fl_created = true;
        return true;
    }
    catch(const std::exception& e)
    {
        LOG_ERROR("Failed to create surface manager: {}", e.what());
    }
    
    return false;
}

void triengine_surface_manager::destroy()
{
    if (!this->is_created()) {
        return;
    }

    LOG_DEBUG("Destroying surface manager...");

    // 렌더링 리소스 해제 (해제 순서가 매우 중요함)
    //    - 렌더 타겟을 파이프라인에서 분리해야 스왑체인 리사이즈 가능
    _dx11_device_context2->OMSetRenderTargets(0, nullptr, nullptr);
    _dxgi_keyed_mutex.Reset(); // 공유 텍스처에 대한 Mutex 해제
    _dx11_shared_texture.Reset(); // 공유 텍스처 자체를 해제
    _dx11_render_texture.Reset(); // 복사 대상이었던 로컬 Screen 텍
    _dx11_device_context2.Reset(); // 디바이스 컨텍스트 해제
    _dx11_device2.Reset(); // 디바이스 해제
    _dxgi_adapter.Reset(); // 어댑터 해제
    _ipc_cli.reset(); // IPC 클라이언트 해제
    _renderer_process_handle.reset(); // 렌더러 프로세스 핸들 해제
    _shared_info.reset(); // 공유 정보 초기화
    _fl_created = false;
}

void triengine_surface_manager::render_frame()
{
    if (!this->is_created()) {
        LOG_ERROR("Surface manager is not created.");
        return;
    }
    
    // 렌더링 전, 획득해둔 KeyedMutex를 사용하여 GL 렌더러가 텍스처 쓰기를 완료할 때까지 대기
    // (뮤텍스를 즉시 얻지 못한 경우, GL 렌더러 측에서 아직 작업 중이거나 렌더링된 프레임이 없음을 의미)
    // 
    // https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgikeyedmutex-acquiresync
    // AcquireSync은 다음과 같은 DWORD 상수들을 반환할 수 있음.
    // (단순 성공 여부 판단을 위해 SUCCEEDED 매크로만 사용할 경우, WAIT_OBJECT_0 반환값을 제외한 나머지 반환 상태값을 제대로 감지하지 못할 수 있음에 유의)
    //     - WAIT_OBJECT_0  : keyed mutex를 성공적으로 획득했음. 이 경우, 렌더링 작업을 계속 진행할 수 있음. (S_OK 와 동일한 값)
    //     - WAIT_TIMEOUT   : 지정된 키가 해제되기 전에 타임아웃 간격이 경과했음을 의미.
    //     - WAIT_ABANDONED : SharedSurface와 KeyedMutex가 더 이상 일관된 상태가 아님. 이 경우, KeyedMutex와 SharedSurface 둘 다 해제한 후 재생성해야 함.
    switch (
        const HRESULT sync_hr = _dxgi_keyed_mutex->AcquireSync(0/* Key */, 10/* Wait Timeout */);
    sync_hr
        ) {
    case WAIT_OBJECT_0: // KeyedMutex를 성공적으로 획득했으므로 렌더링 작업 진행 가능
        // Shared 텍스처를 Screen 텍스처로 복사 (락 점유 시간을 최소화하기 위해 별도 텍스처로 데이터를 복사한 뒤 렌더링 수행)

        // CopyResource는 RGBA -> BGRA 변환을 자동으로 수행해주지 않음
        _dx11_device_context2->CopyResource(_dx11_shared_texture_copy.Get(), _dx11_shared_texture.Get());
        _dxgi_keyed_mutex->ReleaseSync(0/* Key */); // 텍스처 사용이 끝났으므로 KeyedMutex 잠금 해제
        break;
    case WAIT_TIMEOUT: // KeyedMutex를 획득하지 못했으므로 렌더링 작업을 건너뜀
        return;
    case WAIT_ABANDONED: // KeyedMutex가 더 이상 일관된 상태가 아님. 이 경우, KeyedMutex와 SharedSurface 둘 다 해제한 후 재생성해야 함.
        throw std::runtime_error{ "Failed to acquire keyed mutex. (keyed mutex is no longer in a consistent state)" };
        return;
    }

    // 시간에 따라 변하는 색상 계산
    static float t = 0.0f;
    t += 0.1f;
    float r = 0.5f + 0.5f * cos(t);
    float g = 0.5f + 0.5f * sin(t);
    float b = 0.5f + 0.5f * cos(t + 3.14f);
    float color[4] = { r, g, b, 1.0f };

    //LOG_TRACE("Rendering frame with color: {}, {}, {}", r, g, b);
    _dx11_device_context2->ClearRenderTargetView(_dx11_rtv.Get(), color);
    
    D3D11_VIEWPORT viewport;
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width = static_cast<float>(_frame_width);
    viewport.Height = static_cast<float>(_frame_height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    _dx11_device_context2->RSSetViewports(1, &viewport);

    _dx11_device_context2->OMSetRenderTargets(1, _dx11_rtv.GetAddressOf(), NULL);

    _dx11_device_context2->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    _dx11_device_context2->IASetInputLayout(NULL); // No input buffer needed for this VS trick

    _dx11_device_context2->VSSetShader(_dx11_vertex_shader.Get(), NULL, 0);
    _dx11_device_context2->PSSetShader(_dx11_pixel_shader.Get(), NULL, 0);
    _dx11_device_context2->PSSetShaderResources(0, 1, _dx11_srv.GetAddressOf());
    _dx11_device_context2->PSSetSamplers(0, 1, _dx11_sampler_state.GetAddressOf());

    _dx11_device_context2->Draw(3, 0); // 화면을 덮는 하나의 삼각형을 그림
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
}

bool triengine_surface_manager::resize_frame(int32_t frame_width, int32_t frame_height)
{
    LOG_DEBUG("Resizing frame to {}x{}...", frame_width, frame_height);
    LOG_ERROR("Not implemented yet.");
    return false;
}

void triengine_surface_manager::_connect_to_renderer_process()
{
    LOG_DEBUG("Connecting to IPC server...");
    _ipc_cli = std::make_shared<ipc_client>();
    _ipc_cli->set_notify_callback(
        [this](
            [[maybe_unused]] const uint32_t id, 
            const std::string_view data)
        {
            packet_view pck{ data.data(), data.size() };

            switch (pck.type()) {
            case ipc_proto::packet_type::shared_render_context:
            {
                {
                    std::scoped_lock lk{ _shared_info_mtx };
                    _shared_info = *pck.body<ipc_proto::packets::shared_render_context_t>();
                }
                _shared_info_cv.notify_all();
                break;
            }
            default:
                break;
            }
        });

    _ipc_cli->set_disconnect_callback(
        [this]()
        {
            LOG_WARN("Renderer process disconnected! closing viewer window...");
            //::PostMessageA(_viewer_hwnd.get(), WM_CLOSE, 0, 0);
        });

    if (!_ipc_cli->connect(Config::RENDERER_SERVER_NAME)) {
        throw std::runtime_error{ "connect failed" };
    }
    LOG_DEBUG("Connected!");

    // 공유 정보 수신 대기
    LOG_INFO("Waiting for data...");
    std::unique_lock lk{ _shared_info_mtx };
    _shared_info_cv.wait(lk, [this] { return _shared_info.has_value(); });
    LOG_DEBUG("Received data. (pid: {}, adapter: {:x}-{:x}, resource handle: {}, initial size: {}x{})"
        , _shared_info->renderer_process_id
        , _shared_info->target_adapter_luid.HighPart
        , _shared_info->target_adapter_luid.LowPart
        , _shared_info->shared_texture_handle
        , _shared_info->shared_texture_width
        , _shared_info->shared_texture_height
    );

    if (!_renderer_process_handle) {
        LOG_DEBUG("Opening renderer process handle...");
        _renderer_process_handle.reset(::OpenProcess(
            PROCESS_DUP_HANDLE | SYNCHRONIZE,
            FALSE,
            _shared_info->renderer_process_id
        ));
        ASSERT(_renderer_process_handle.get());
    }
}

void triengine_surface_manager::_init_d3d_resources(
    const int32_t frame_width, 
    const int32_t frame_height)
{
    LOG_TRACE("{} ENTER", __func__);

    // IMPORTANT NOTE: 반드시 CreateDXGIFactory2 함수를 사용해서 DXGI 1.2 버전 이상의 DXGI 팩토리(`IDXGIFactory`)를 생성해줘야 함.
    // (안 그러면 `D3D11 ERROR: ID3D11Device::OpenSharedResource1: Returning E_INVALIDARG, meaning invalid parameters were passed` 오류 발생)
    ComPtr<IDXGIFactory2> dxgiFactory2;
    ASSERT_HR(::CreateDXGIFactory2(0, IID_PPV_ARGS(&dxgiFactory2)));

    ComPtr<IDXGIAdapter> dxgiAdapter0;
    for (UINT i = 0; dxgiFactory2->EnumAdapters(i, &dxgiAdapter0) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC desc;
        dxgiAdapter0->GetDesc(&desc);
        if (std::memcmp(&desc.AdapterLuid, &_shared_info->target_adapter_luid, sizeof(LUID)) == 0) {
            _dxgi_adapter = dxgiAdapter0;
            break;
        }
    }
    
    if (!_dxgi_adapter) {
        throw std::runtime_error{ "Matching adapter not found" };
    }

    UINT deviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
    deviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    ComPtr<ID3D11Device> dx11Device0;
    ComPtr<ID3D11DeviceContext> dx11DeviceContext0;

    ASSERT_HR(::D3D11CreateDevice(        
        _dxgi_adapter.Get(),
        D3D_DRIVER_TYPE_UNKNOWN,
        NULL,
        deviceFlags,
        NULL,
        0,
        D3D11_SDK_VERSION,
        &dx11Device0,
        NULL,
        &dx11DeviceContext0
    ));

    // ID3D11Device -> ID3D11Device2 (상위 버전 오브젝트)로 변환
    ASSERT_HR(dx11Device0.As(&_dx11_device2));

    // ID3D11DeviceContext -> ID3D11DeviceContext2 (상위 버전 오브젝트)로 변환
    ASSERT_HR(dx11DeviceContext0.As(&_dx11_device_context2));

    // OpenSharedResource1 함수(혹은 OpenSharedResourceByName 함수)를 사용하여 client측에서 생성한 NT 핸들 획득 & 공유 텍스처 생성
    // OpenSharedResource1 함수를 사용하는 경우, DuplicateHandle을 사용하여 전달받은 공유 텍스처 핸들을 현재 프로세스에서 유효한 핸들로 복제해야 함
    HANDLE duplicatedSharedTextureHandle{};
    if (!::DuplicateHandle(
        _renderer_process_handle.get(), // 원본 핸들이 속한 프로세스 (자식/렌더러)
        _shared_info->shared_texture_handle, // 원본 핸들 값 (IPC로 수신)
        ::GetCurrentProcess(), // 핸들을 복제해 올 대상 프로세스 (부모/뷰어)
        &duplicatedSharedTextureHandle, // 복제된 핸들을 저장할 변수
        0, // 접근 권한 (0은 원본과 동일한 권한)
        FALSE, // 핸들 상속 여부
        DUPLICATE_SAME_ACCESS // 옵션 (원본과 동일한 접근 권한으로 복제)
    )) {
        throw std::runtime_error{ "Failed to duplicate client resource handle" };
    }
    ASSERT_HR(_dx11_device2->OpenSharedResource1(
        duplicatedSharedTextureHandle,
        IID_PPV_ARGS(&_dx11_shared_texture)
    ));
    LOG_DEBUG("Successfully acquired client resource. (handle: {} -> {})"
        , _shared_info->shared_texture_handle
        , duplicatedSharedTextureHandle
    );
    // 리소스를 여는 데 성공했다면, 복제된 핸들은 더이상 필요 없으므로 정리
    ::CloseHandle(duplicatedSharedTextureHandle);

    // 획득한 공유 텍스처에서 KeyedMutex 인터페이스 획득
    ASSERT_HR(_dx11_shared_texture.As(&_dxgi_keyed_mutex));

    // 복사본을 저장할 일반 텍스처 생성
    {
        D3D11_TEXTURE2D_DESC sharedTexCopyDesc;
        _dx11_shared_texture->GetDesc(&sharedTexCopyDesc); // 공유 텍스처의 현재 속성을 가져옴
        sharedTexCopyDesc.Usage = D3D11_USAGE_DEFAULT; // 일반적인 GPU 읽기/쓰기 용도
        sharedTexCopyDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE; // 셰이더에서 읽을 수 있도록 설정
        sharedTexCopyDesc.CPUAccessFlags = 0; // CPU 접근 없음
        sharedTexCopyDesc.MiscFlags = 0; // 공유 플래그 제거

        ASSERT_HR(_dx11_device2->CreateTexture2D(&sharedTexCopyDesc, nullptr, &_dx11_shared_texture_copy));
        LOG_DEBUG("Created a local copy texture for the shared texture.");
    }

    // 화면 렌더링용 screen 텍스처 생성
    D3D11_TEXTURE2D_DESC renderTexDesc{};
    renderTexDesc.Width = _shared_info->shared_texture_width;
    renderTexDesc.Height = _shared_info->shared_texture_height;
    renderTexDesc.MipLevels = 1;
    renderTexDesc.ArraySize = 1;
    renderTexDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    renderTexDesc.SampleDesc.Count = 1;
    renderTexDesc.SampleDesc.Quality = 0;
    renderTexDesc.Usage = D3D11_USAGE_DEFAULT;
    renderTexDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    renderTexDesc.CPUAccessFlags = 0;
    renderTexDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
    ASSERT_HR(_dx11_device2->CreateTexture2D(&renderTexDesc, NULL, &_dx11_render_texture));

    // 공유 핸들 생성

    ComPtr<IDXGIResource> dxgi_resource;
    if (FAILED(_dx11_render_texture.As(&dxgi_resource))) {
        LOG_ERROR("Failed to get IDXGIResource from render targettexture");
        throw std::runtime_error{ "Failed to get IDXGIResource from render target texture" };
    }

    if (HANDLE newHandle{ nullptr };
        SUCCEEDED(dxgi_resource->GetSharedHandle(&newHandle))) {
        _shared_texture_handle.reset(newHandle);
    } else {
        LOG_ERROR("Failed to get shared handle from IDXGIResource");
        if (newHandle) { ::CloseHandle(newHandle); }
        throw std::runtime_error{ "Failed to get shared handle from IDXGIResource" };
    }

    LOG_DEBUG("Created shared texture handle: {:p}", _shared_texture_handle.get());

    // 렌더 타겟 뷰 생성
    D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
    rtvDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; // 구체적인 포맷 지정
    rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    rtvDesc.Texture2D.MipSlice = 0;
    ASSERT_HR(_dx11_device2->CreateRenderTargetView(
        _dx11_render_texture.Get(),
        &rtvDesc,
        &_dx11_rtv
    ));

    if (!_dx11_rtv) {
        LOG_ERROR("Failed to create render target view");
        throw std::runtime_error{ "Failed to create render target view" };
    }

    // Full-screen Quad 렌더링을 위한 셰이더 및 리소스 생성
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
            return tx.Sample(smp, float2(input.uv.x, 1.0 - input.uv.y)); // return tx.Sample(smp, input.uv);
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
        throw std::runtime_error{ fmt::format(
            "Failed to compile vertex shader({}): {}"
            , hr
            , static_cast<char*>(errBlob->GetBufferPointer())
        ) };
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
        throw std::runtime_error{ fmt::format(
            "Failed to compile pixel shader({}): {}"
            , hr
            , static_cast<char*>(errBlob->GetBufferPointer())
        ) };
    }

    ASSERT_HR(_dx11_device2->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), NULL, &_dx11_vertex_shader));
    ASSERT_HR(_dx11_device2->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), NULL, &_dx11_pixel_shader));

    // 원본 공유 텍스처에 대한 SRV 생성 (포맷 명시)
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // 원본 포맷을 RGBA로 명시
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;
    ASSERT_HR(_dx11_device2->CreateShaderResourceView(
        _dx11_shared_texture_copy.Get(), 
        &srvDesc, 
        &_dx11_srv)
    ); // Equivalent of: `glBindTexture`+ `sampler2D`

    D3D11_SAMPLER_DESC sampDesc{};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampDesc.MinLOD = 0;
    sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
    ASSERT_HR(_dx11_device2->CreateSamplerState(&sampDesc, &_dx11_sampler_state));

    _frame_width = frame_width;
    _frame_height = frame_height;
}