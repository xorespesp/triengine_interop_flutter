# triengine_interop_flutter

A Flutter Windows plugin that embeds a [triengine](https://github.com/xorespesp/triengine) 3D scene — rendered offscreen by a separate native process — directly into a Flutter widget tree as a live texture.

> **triengine** is an OpenGL-based 3D graphics engine.  
> Because Flutter on Windows uses Direct3D 11 internally, bridging an OpenGL renderer into Flutter requires a cross-API GPU texture sharing pipeline.  
> This plugin implements that bridge.

---

## Architecture

```mermaid
graph TB
    subgraph flutter_proc["Flutter app process"]
        widget["TriengineSurface widget"]
        tex["Flutter GpuSurfaceTexture\n(kFlutterDesktopGpuSurfaceTypeDxgiSharedHandle)"]
        render_tex["D3D11 render texture\n(D3D11_RESOURCE_MISC_SHARED)\nlegacy DXGI shared handle"]
        copy_tex["D3D11 shared texture copy"]
        plugin_dev["Plugin D3D11 device\n(adapter matched by LUID)"]

        widget --> tex
        tex -->|"GetSharedHandle()"| render_tex
        plugin_dev -->|"full-screen quad blit"| render_tex
        plugin_dev -->|"CopyResource"| copy_tex
    end

    subgraph ipc["Boost.Interprocess IPC\n(shared memory + message queues)"]
        direction LR
        req_rep["Request / Response\n(init, resize)"]
        notify["Notify\n(mouse events)"]
    end

    subgraph renderer_proc["triengine renderer process"]
        gl_ctx["OpenGL context\n(GLFW hidden window)"]
        d3d_dev["D3D11 device\n(adapter matched by GL_RENDERER name)"]
        interop_tex["D3D11 interop texture\n(SHARED_KEYEDMUTEX | SHARED_NTHANDLE)\nGL memory object via EXT_memory_object_win32"]
        fbo["OpenGL FBO\n(color attachment = interop texture)"]
        scn["triengine scene renderer"]

        gl_ctx --> d3d_dev
        d3d_dev --> interop_tex
        interop_tex -->|"glImportMemoryWin32HandleEXT"| fbo
        scn -->|"render"| fbo
    end

    interop_tex -->|"NT shared handle\nDuplicateHandle + OpenSharedResource1"| copy_tex
    flutter_proc <-->|"IPC"| ipc
    ipc <-->|"IPC"| renderer_proc
```

### Key design points

- The D3D11 interop texture lives in the renderer process. It is the **shared render target between OpenGL and D3D11**, backed by the same physical GPU memory. OpenGL renders into it via an FBO; the plugin reads it via `OpenSharedResource1`.
- The plugin cannot hand the interop texture directly to Flutter because it uses an NT handle (`SHARED_NTHANDLE`), and the Flutter engine expects a legacy DXGI handle (`SHARED`). The plugin therefore blits the interop texture into a separate render texture that carries a legacy DXGI handle, and passes that handle to Flutter.
- Frame-level synchronization between the OpenGL writer and the D3D11 reader is done via `IDXGIKeyedMutex` / `GL_EXT_win32_keyed_mutex`, both operating on key `0`.

---

## Initialization

```mermaid
sequenceDiagram
    participant Dart as Dart (TriengineSurface)
    participant Plugin as Plugin (C++)
    participant IPC as Boost.Interprocess IPC
    participant Backend as triengine renderer backend

    Dart->>Plugin: createSurface(ipcServerName, width, height)

    Plugin->>IPC: connect (handshake via message queue)
    IPC->>Backend: handshake request
    Backend->>IPC: handshake response (session name)
    IPC->>Plugin: connected

    Plugin->>IPC: init_request { frame_width, frame_height }
    IPC->>Backend: init_request

    Note over Backend: submit to main render thread
    Backend->>Backend: create OpenGL context (GLFW hidden window)
    Backend->>Backend: find D3D11 adapter matching GL_RENDERER name
    Backend->>Backend: create D3D11 device on that adapter
    Backend->>Backend: create interop texture (SHARED_KEYEDMUTEX | SHARED_NTHANDLE)
    Backend->>Backend: import into OpenGL via glImportMemoryWin32HandleEXT
    Backend->>Backend: attach to FBO as GL_COLOR_ATTACHMENT0
    Backend->>Backend: create triengine scene

    Backend->>IPC: init_response { renderer_pid, adapter_luid, surface_handle (NT) }
    IPC->>Plugin: init_response

    Plugin->>Plugin: DuplicateHandle(renderer_pid, surface_handle) → local NT handle
    Plugin->>Plugin: EnumAdapters → find adapter matching LUID
    Plugin->>Plugin: D3D11CreateDevice(target adapter)
    Plugin->>Plugin: OpenSharedResource1(local NT handle) → shared texture + KeyedMutex
    Plugin->>Plugin: CreateTexture2D(SHARED) → render texture for Flutter
    Plugin->>Plugin: IDXGIResource::GetSharedHandle() → legacy DXGI handle
    Plugin->>Plugin: RegisterTexture(GpuSurfaceTexture) → texture_id

    Plugin->>Dart: Success(texture_id)
    Dart->>Dart: display Texture(texture_id) widget
```

---

## Per-frame rendering

```mermaid
sequenceDiagram
    participant Dart as Dart (frame ticker)
    participant Plugin as Plugin (D3D11)
    participant Mutex as IDXGIKeyedMutex
    participant GL as OpenGL (renderer backend)
    participant Flutter as Flutter engine

    loop render loop (renderer backend)
        GL->>Mutex: glAcquireKeyedMutexWin32EXT(key=0, timeout=INF)
        GL->>GL: render triengine scene into FBO (interop texture)
        GL->>Mutex: glReleaseKeyedMutexWin32EXT(key=0)
    end

    loop updateSurface tick (Dart)
        Dart->>Plugin: updateSurface()
        Plugin->>Mutex: AcquireSync(key=0, timeout=10ms)
        alt acquired
            Plugin->>Plugin: CopyResource(shared_texture_copy <- interop texture)
            Plugin->>Mutex: ReleaseSync(key=0)
            Plugin->>Plugin: full-screen quad draw\nshared_texture_copy -> render_texture\n(Y-flip in pixel shader)
            Plugin->>Flutter: MarkTextureFrameAvailable(texture_id)
        else timeout (renderer still busy)
            Plugin->>Dart: Success (skip frame silently)
        end
        Plugin->>Dart: Success
    end

    Flutter->>Flutter: composite Texture widget using render_texture DXGI handle
```

### Y-axis flip

OpenGL uses a bottom-left coordinate origin; Direct3D uses top-left. The plugin's pixel shader flips the Y axis when blitting `shared_texture_copy` into `render_texture`, so the image appears correctly in Flutter.

---

## Resize

```mermaid
sequenceDiagram
    participant Dart as Dart (LayoutBuilder)
    participant Plugin as Plugin (D3D11)
    participant IPC as IPC
    participant Backend as triengine renderer backend

    Dart->>Plugin: resizeSurface(new_width, new_height)
    Plugin->>IPC: frame_resize_request { width, height }
    IPC->>Backend: frame_resize_request

    Note over Backend: submit to main render thread
    Backend->>Backend: glfwSetWindowSize() triggers GLFW resize callback
    Backend->>Backend: recreate D3D11 interop texture at new size
    Backend->>Backend: glFinish() + delete old GL memory object
    Backend->>Backend: glImportMemoryWin32HandleEXT (new NT handle)
    Backend->>Backend: reattach FBO

    Backend->>IPC: frame_resize_response { surface_handle (NT) }
    IPC->>Plugin: frame_resize_response

    Plugin->>Plugin: open new shared texture (DuplicateHandle + OpenSharedResource1)
    Plugin->>Plugin: recreate KeyedMutex, texture copy, render texture, SRV, RTV
    Plugin->>Plugin: atomic swap of all D3D resources
    Plugin->>Plugin: MarkTextureFrameAvailable(texture_id)
    Plugin->>Dart: Success
```

---

## Mouse event forwarding

Mouse input from the Flutter widget is forwarded to the renderer as fire-and-forget IPC notify packets. The renderer uses them to drive the triengine scene camera.

| Dart method | Packet type | Payload |
|---|---|---|
| `sendMouseButtonEvent` | `mouse_button_event` | x, y, button, action, mods |
| `sendMouseMoveEvent` | `mouse_move_event` | x, y, mods |
| `sendMouseScrollEvent` | `mouse_scroll_event` | yoffset |

Camera behavior driven by these events: left-drag = orbit, middle-drag = pan, scroll = zoom.

---

## IPC transport details

The plugin and renderer backend communicate via **Boost.Interprocess**: a named shared memory segment plus per-session message queues. No network sockets or named pipes are involved.

```mermaid
graph LR
    subgraph handshake["Connection handshake"]
        c2s_hq["c2s handshake MQ"]
        s2c_hq["s2c handshake MQ"]
    end

    subgraph session["Per-session (after handshake)"]
        c2s_mq["c2s message queue\n(requests + notifies)"]
        s2c_mq["s2c message queue\n(responses + heartbeats)"]
        shm["Shared memory segment\n(packet payloads as shm strings)"]
    end

    Client -->|"handshake_req"| c2s_hq
    s2c_hq -->|"handshake_rep (session name)"| Client
    Client --> c2s_mq
    s2c_mq --> Client
    Client <--> shm

    Server --> s2c_hq
    c2s_hq --> Server
    Server --> s2c_mq
    c2s_mq --> Server
    Server <--> shm
```

Packet types at the session layer:

| Type | Direction | Purpose |
|---|---|---|
| `request` | client -> server | `init`, `resize` (expects a `response`) |
| `response` | server -> client | reply to a `request`, matched by packet ID |
| `notify` | client -> server | mouse events (no reply expected) |
| `heartbeat` | both | liveness; sent every 5 s, timeout after 20 s |
| `disconnect` | both | graceful teardown |

Packet payloads at the application layer (`ipc_proto`):

```
init_request        { frame_width, frame_height }
init_response       { renderer_process_id, target_adapter_luid, surface_handle }
frame_resize_request  { width, height }
frame_resize_response { surface_handle }
mouse_button_event  { x, y, button, action, mods }
mouse_move_event    { x, y, mods }
mouse_scroll_event  { yoffset }
```

---

## GPU adapter requirement (critical)

The Flutter app's `windows/runner/main.cpp` **must** export `NvOptimusEnablement` and `AmdPowerXpressRequestHighPerformance`. Without this, the Flutter engine may run on the iGPU while the renderer uses the dGPU, and texture sharing will silently fail.

### Why this matters

```mermaid
graph TB
    subgraph renderer_proc["Renderer process"]
        gl["OpenGL device\n(picks dGPU — typically the primary render adapter)"]
        d3d_r["D3D11 device\n(adapter matched to GL by name string)"]
        interop["Interop texture\nD3D11_RESOURCE_MISC_SHARED_NTHANDLE"]
        gl --> d3d_r --> interop
    end

    subgraph flutter_proc["Flutter app process (without dllexport)"]
        d3d_p["Plugin D3D11 device\n(adapter matched by LUID from init_response)\ndGPU"]
        d3d_fl["Flutter engine D3D11 device\n(OS default — may be iGPU)"]
        rt["Render texture\nD3D11_RESOURCE_MISC_SHARED\nlegacy DXGI handle"]
        d3d_p -->|blit| rt
        d3d_fl -->|"OpenSharedResource() FAILS\n(cross-adapter not supported)"| rt
    end

    interop -->|"NT handle\nOpenSharedResource1\n(same adapter: OK)"| d3d_p
```

The legacy DXGI shared handle (`D3D11_RESOURCE_MISC_SHARED`, obtained via `IDXGIResource::GetSharedHandle()`) can only be opened by a D3D11 device on the **same GPU adapter** as the device that created it. If Flutter's internal D3D11 device is on a different adapter, `OpenSharedResource()` fails and Flutter logs:

```
[ERROR:flutter/shell/platform/windows/external_texture_d3d.cc(107)] Binding D3D surface failed.
```

The widget shows nothing. No Dart-level exception is raised.

Note: the interop texture between the renderer and the plugin uses `D3D11_RESOURCE_MISC_SHARED_NTHANDLE` and is opened via `OpenSharedResource1` — this supports cross-process sharing but still requires both devices to be on the same adapter.

### Fix

Add the following to `windows/runner/main.cpp` at file scope, before `wWinMain`:

```cpp
// Force the high-performance GPU (dGPU).
//
// triengine's offscreen_renderer_dx selects the GPU adapter by matching
// the OpenGL renderer name string against DXGI adapter descriptions.
// On a system with dGPU + iGPU this will be the dGPU. The plugin creates
// its D3D11 device on the same adapter (identified by LUID from the IPC
// init_response). The Flutter-facing render texture uses a legacy DXGI
// shared handle (D3D11_RESOURCE_MISC_SHARED), which only works when
// Flutter's own D3D11 device is on the same adapter.
// These symbols instruct the NVIDIA/AMD driver to assign this process to
// the high-performance GPU at startup.
extern "C" {
__declspec(dllexport) DWORD NvOptimusEnablement = 1;
__declspec(dllexport) DWORD AmdPowerXpressRequestHighPerformance = 1;
}
```

---

## Required OpenGL extensions (renderer backend)

The following extensions must be supported by the GPU driver used by the triengine backend:

| Extension | Purpose |
|---|---|
| `GL_EXT_memory_object` | import external GPU memory objects into OpenGL |
| `GL_EXT_memory_object_win32` | import via Win32 NT handles (`GL_HANDLE_TYPE_D3D11_IMAGE_EXT`) |
| `GL_EXT_win32_keyed_mutex` | synchronize with D3D11 via `IDXGIKeyedMutex` |

`offscreen_renderer_dx` panics at startup if any of these extensions are missing.

---

## Platform support

Windows only. Requires Direct3D 11 and the OpenGL extensions listed above.

| Platform | Supported |
|---|---|
| Windows | yes |
| macOS / Linux / Android / iOS | no |

---

## Usage

```dart
import 'package:triengine_interop_flutter/triengine_surface_widget.dart';

TriengineSurface(
  rendererIpcServerName: 'your-ipc-server-name',
  size: Size(width, height),
  devicePixelRatio: View.of(context).devicePixelRatio,
)
```

`TriengineSurface` manages the full surface lifecycle:
- `createSurface` on widget mount
- `updateSurface` each frame tick
- `resizeSurface` on layout change
- `destroySurface` on widget unmount

For a complete renderer backend implementation, see `examples/example-interprocess` in the triengine repository.
