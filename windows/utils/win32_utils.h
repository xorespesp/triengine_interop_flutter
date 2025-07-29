#pragma once
#include <Windows.h>
#include <memory>

namespace utils
{
    struct handle_deleter {
        void operator()(HANDLE handle) const {
            if (handle) { ::CloseHandle(handle); }
        }
    };
    using unique_handle = std::unique_ptr<std::remove_pointer_t<HANDLE>, handle_deleter>;
    using shared_handle = std::shared_ptr<std::remove_pointer_t<HANDLE>>;

    struct hwnd_deleter {
        void operator()(HWND hwnd) const {
            if (hwnd) { ::DestroyWindow(hwnd); }
        }
    };
    using unique_hwnd = std::unique_ptr<std::remove_pointer_t<HWND>, hwnd_deleter>;
    using shared_hwnd = std::shared_ptr<std::remove_pointer_t<HWND>>;

} // namespace