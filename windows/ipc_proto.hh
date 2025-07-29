#pragma once
#include <Windows.h>
#include <vector>
#include <string_view>
#include <stdexcept>
#include "utils/bit_cast.h"

namespace ipc_proto
{
    enum button_action_type
    {
        ACTION_PRESS,
        ACTION_RELEASE,
        ACTION_REPEAT,
    };

    enum key_button_type
    {
        KEY_UNKNOWN,

        KEY_A, KEY_B, KEY_C, KEY_D, KEY_E,
        KEY_F, KEY_G, KEY_H, KEY_I, KEY_J,
        KEY_K, KEY_L, KEY_M, KEY_N, KEY_O,
        KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T,
        KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y,
        KEY_Z,

        KEY_0, KEY_1, KEY_2, KEY_3, KEY_4,
        KEY_5, KEY_6, KEY_7, KEY_8, KEY_9,

        KEY_F1, KEY_F2, KEY_F3, KEY_F4,
        KEY_F5, KEY_F6, KEY_F7, KEY_F8,
        KEY_F9, KEY_F10, KEY_F11, KEY_F12,

        KEY_ESCAPE,
        KEY_BACK,
        KEY_RETURN,
        KEY_SPACE,
        KEY_LEFT,
        KEY_UP,
        KEY_RIGHT,
        KEY_DOWN,
        KEY_MULTIPLY,
        KEY_ADD,
        KEY_SUBTRACT,
        KEY_DIVIDE,
        // ...
    };

    enum mouse_button_type
    {
        MOUSEBTN_L,
        MOUSEBTN_R,
        MOUSEBTN_M,
    };

    enum modkey_button_type
    {
        MODKEY_SHIFT = 1 << 0,
        MODKEY_CTRL = 1 << 1,
        MODKEY_ALT = 1 << 2,
        MODKEY_CAPSLOCK = 1 << 3,
        MODKEY_NUMLOCK = 1 << 4,
    };

    // Add bitwise operators for modkey_button_type  
    inline modkey_button_type operator|(modkey_button_type lhs, modkey_button_type rhs)
    {
        return static_cast<modkey_button_type>(static_cast<int>(lhs) | static_cast<int>(rhs));
    }

    inline modkey_button_type& operator|=(modkey_button_type& lhs, modkey_button_type rhs)
    {
        lhs = lhs | rhs;
        return lhs;
    }

    static key_button_type translate_vkcode(DWORD vkcode)
    {
        if (vkcode >= 'A' && vkcode <= 'Z') {
            return static_cast<key_button_type>(KEY_A + (vkcode - 'A'));
        }

        if (vkcode >= '0' && vkcode <= '9') {
            return static_cast<key_button_type>(KEY_0 + (vkcode - '0'));
        }

        if (vkcode >= VK_F1 && vkcode <= VK_F12) {
            return static_cast<key_button_type>(KEY_F1 + (vkcode - VK_F1));
        }

        switch (vkcode) {
        case VK_ESCAPE: return KEY_ESCAPE;
        case VK_BACK: return KEY_BACK;
        case VK_RETURN: return KEY_RETURN;
        case VK_SPACE: return KEY_SPACE;
        case VK_LEFT: return KEY_LEFT;
        case VK_UP: return KEY_UP;
        case VK_RIGHT: return KEY_RIGHT;
        case VK_DOWN: return KEY_DOWN;
        case VK_MULTIPLY: return KEY_MULTIPLY;
        case VK_ADD:  return KEY_ADD;
        case VK_SUBTRACT: return KEY_SUBTRACT;
        case VK_DIVIDE: return KEY_DIVIDE;
        default: break;
        }

        return KEY_UNKNOWN;
    }

    enum class packet_type
    {
        invalid = 0,
        shared_render_context,
        frame_resize_request,
        frame_resize_response,
        mouse_move_event,
        mouse_scroll_event,
        mouse_button_event,
    };

    struct packet_header_t
    {
        packet_type type;
        size_t body_size;
    };

    namespace packets
    {
        // packet_type::render_context
        struct shared_render_context_t
        {
            DWORD renderer_process_id;
            LUID target_adapter_luid;
            HANDLE shared_texture_handle;
            int32_t shared_texture_width;
            int32_t shared_texture_height;
        };

        struct frame_resize_request_t
        {
            int32_t width;
            int32_t height;
        };

        struct frame_resize_response_t
        {
            HANDLE shared_texture_handle;
        };

        // packet_type::mouse_move_event
        struct mouse_move_event_t
        {
            int32_t x;
            int32_t y;
            struct {
                bool ctrl_pressed : 1;
                bool shift_pressed : 1;
                bool l_btn_pressed : 1;
                bool r_btn_pressed : 1;
                bool m_btn_pressed : 1;
            } mods;
        };

        struct mouse_scroll_event_t
        {
            float yoffset;
        };

        // packet_type::mouse_click_event
        struct mouse_button_event_t
        {
            int32_t x;
            int32_t y;
            mouse_button_type button;
            button_action_type action;
            modkey_button_type mods;
        };

    } // namespace

} // namespace

template <typename _PckBody>
class packet_builder
{
    static_assert(std::is_standard_layout_v<_PckBody> && std::is_trivial_v<_PckBody>);

public:
    packet_builder(ipc_proto::packet_type type) {
        auto* const hdr = this->header();
        hdr->type = type;
        hdr->body_size = sizeof(_PckBody);
    }

    const ipc_proto::packet_header_t* header() const noexcept {
        return utils::bit_cast<ipc_proto::packet_header_t*>(_pck_buff.data());
    }

    ipc_proto::packet_header_t* header() noexcept {
        return utils::bit_cast<ipc_proto::packet_header_t*>(_pck_buff.data());
    }

    const _PckBody* body() const noexcept {
        return utils::bit_cast<_PckBody*>(_pck_buff.data() + sizeof(ipc_proto::packet_header_t));
    }

    _PckBody* body() noexcept {
        return utils::bit_cast<_PckBody*>(_pck_buff.data() + sizeof(ipc_proto::packet_header_t));
    }

    const uint8_t* data() const noexcept {
        return _pck_buff.data();
    }

    size_t size() const noexcept {
        return _pck_buff.size();
    }

private:
    std::array<uint8_t, sizeof(ipc_proto::packet_header_t) + sizeof(_PckBody)> _pck_buff;
};

class packet_view
{
public:
    packet_view() = default;
    packet_view(const void* data, size_t size)
        : _data_view{ static_cast<const char*>(data), size }
    {
        if (!this->_validate_format()) {
            throw std::invalid_argument{ "Invalid packet format" };
        }
    }

    bool empty() const noexcept {
        return !_data_view.empty();
    }

    ipc_proto::packet_type type() const noexcept {
        return this->empty()
            ? this->_header()->type
            : ipc_proto::packet_type::invalid;
    }

    template <typename _PckBody>
    const _PckBody* body() const noexcept
    {
        static_assert(std::is_standard_layout_v<_PckBody> && std::is_trivial_v<_PckBody>);

        if (!this->empty() || this->_header()->body_size < sizeof(_PckBody)) {
            return nullptr;
        }

        return utils::bit_cast<const _PckBody*>(_data_view.data() + sizeof(ipc_proto::packet_header_t));
    }

    const uint8_t* data() const noexcept {
        return utils::bit_cast<const uint8_t*>(_data_view.data());
    }

    size_t size() const noexcept {
        return _data_view.size();
    }

private:
    inline const ipc_proto::packet_header_t* _header() const noexcept {
        return utils::bit_cast<ipc_proto::packet_header_t*>(_data_view.data());
    }

    inline ipc_proto::packet_header_t* _header() noexcept {
        return utils::bit_cast<ipc_proto::packet_header_t*>(_data_view.data());
    }

    inline bool _validate_format() const noexcept
    {
        if (_data_view.empty()) {
            return false;
        }

        if (_data_view.size() < sizeof(ipc_proto::packet_header_t)) {
            return false;
        }

        const auto header = utils::bit_cast<const ipc_proto::packet_header_t*>(_data_view.data());
        if (_data_view.size() != header->body_size + sizeof(ipc_proto::packet_header_t)) {
            return false;
        }

        return true;
    }

private:
    std::string_view _data_view{};
};