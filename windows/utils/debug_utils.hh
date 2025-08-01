#pragma once
#include <Windows.h>
#include <comdef.h> // for HRESULT error handling (_com_error)
#include <fmt/core.h>
#include <fmt/format.h>
#include <fmt/xchar.h>
#include <cstdlib>
#include "string_codecvt.hh"

// Concat two arguments.
#define __PP_CONCAT(X, Y) X ## Y
#define _PP_CONCAT(X, Y) __PP_CONCAT(X, Y)

// Expand argument.
#define _PP_EXPAND(X) X

// Returns the 100th argument.
#define _PP_ARG100(_,\
   _100,_99,_98,_97,_96,_95,_94,_93,_92,_91,_90,_89,_88,_87,_86,_85,_84,_83,_82,_81, \
   _80,_79,_78,_77,_76,_75,_74,_73,_72,_71,_70,_69,_68,_67,_66,_65,_64,_63,_62,_61, \
   _60,_59,_58,_57,_56,_55,_54,_53,_52,_51,_50,_49,_48,_47,_46,_45,_44,_43,_42,_41, \
   _40,_39,_38,_37,_36,_35,_34,_33,_32,_31,_30,_29,_28,_27,_26,_25,_24,_23,_22,_21, \
   _20,_19,_18,_17,_16,_15,_14,_13,_12,_11,_10,_9,_8,_7,_6,_5,_4,_3,_2,_1,...) _1

// Returns whether __VA_ARGS__ has a comma (up to 100 arguments).
// Note: MSVC does not expand __VA_ARGS__ like most other compilers, so an extra step(expansion) is necessary.
// Ref: https://stackoverflow.com/a/66556553
#define _PP_HAS_COMMA(...) _PP_EXPAND(_PP_ARG100(__VA_ARGS__, \
   1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, \
   1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, \
   1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, \
   1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 ,1, \
   1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0))

#define _CURRENT_SOURCE_LOC()        ::utils::source_loc{ __FILE__, __LINE__, __func__ }
#define _CALL_LOGGER0(LV, STR)       ::utils::internal::log_impl(_CURRENT_SOURCE_LOC(), LV, STR)
#define _CALL_LOGGER1(LV, FSTR, ...) ::utils::internal::logf_impl(_CURRENT_SOURCE_LOC(), LV, FSTR, __VA_ARGS__)

#define LOG_TRACE(...)    _PP_CONCAT(_CALL_LOGGER, _PP_HAS_COMMA(__VA_ARGS__))( ::utils::log_level::trace, __VA_ARGS__)
#define LOG_DEBUG(...)    _PP_CONCAT(_CALL_LOGGER, _PP_HAS_COMMA(__VA_ARGS__))( ::utils::log_level::debug, __VA_ARGS__)
#define LOG_INFO(...)     _PP_CONCAT(_CALL_LOGGER, _PP_HAS_COMMA(__VA_ARGS__))( ::utils::log_level::info, __VA_ARGS__)
#define LOG_WARN(...)     _PP_CONCAT(_CALL_LOGGER, _PP_HAS_COMMA(__VA_ARGS__))( ::utils::log_level::warn, __VA_ARGS__)
#define LOG_ERROR(...)    _PP_CONCAT(_CALL_LOGGER, _PP_HAS_COMMA(__VA_ARGS__))( ::utils::log_level::error, __VA_ARGS__)
#define LOG_CRITICAL(...) _PP_CONCAT(_CALL_LOGGER, _PP_HAS_COMMA(__VA_ARGS__))( ::utils::log_level::critical, __VA_ARGS__)

#define ASSERT(COND, ...)  ::utils::internal::assert_impl((COND), #COND, _CURRENT_SOURCE_LOC(), __VA_ARGS__)
#define ASSERT_HR(HR, ...) ::utils::internal::assert_hresult_impl((HR), #HR, _CURRENT_SOURCE_LOC(), __VA_ARGS__)

namespace utils
{
    // `std::source_location`(since C++20) like object
    class source_loc
    {
    public:
        std::string_view filepath;
        int line{};
        std::string_view funcname;

    public:
        constexpr source_loc() = default;

        template <std::size_t N, std::size_t M>
        constexpr source_loc(const char(&filepath_)[N], int line_, const char(&funcname_)[M])
            : filepath{ filepath_, N - 1 }
            , line{ line_ }
            , funcname{ funcname_, M - 1 }
        {}

        constexpr bool empty() const noexcept {
            return filepath.empty();
        }

        constexpr std::string_view filename() const {
            return filepath.substr(filepath.find_last_of("/\\") + 1); // split filename
        }
    };

	enum class log_level {
		trace = 0,
		debug,
		info,
		warn,
		error,
		critical
	};

    namespace internal
    {
        constexpr std::string_view log_level_to_string(log_level lv) {
            switch (lv) {
                case log_level::trace:    return "TRACE";
                case log_level::debug:    return "DEBUG";
                case log_level::info:     return "INFO";
                case log_level::warn:     return "WARN";
                case log_level::error:    return "ERROR";
                case log_level::critical: return "CRITICAL";
                default:                  return "???";
            }
        }

        // Ansi
        static inline void log_impl(
            const source_loc& src_loc, 
            const log_level lv, 
            const std::string_view message)
        {
            const uint32_t thread_id = static_cast<uint32_t>(::GetCurrentThreadId());
            auto output = fmt::format("(plugin) | {} | TID {} | {}:{} | {}\n"
                , log_level_to_string(lv)
                , thread_id
                , src_loc.filename()
                , src_loc.line
                , message
            );
            ::OutputDebugStringA(output.c_str());
        }

        // Wide
        static inline void log_impl(
            const source_loc& src_loc, 
            const log_level lv, 
            const std::wstring_view message)
        {
            const uint32_t thread_id = static_cast<uint32_t>(::GetCurrentThreadId());
            auto output = fmt::format(L"(plugin) | {} | TID {} | {}:{} | {}\n"
                , string::utf8_to_utf16(log_level_to_string(lv))
                , thread_id
                , string::utf8_to_utf16(src_loc.filename())
                , src_loc.line
                , message
            );
            ::OutputDebugStringW(output.c_str());
        }

        // Ansi
        template<typename... Args>
        static inline void logf_impl(
            const source_loc& src_loc,
            const log_level lv,
            const fmt::format_string<Args...> fmt,
            Args&&... args)
        {
            log_impl(src_loc, lv, fmt::format(fmt, std::forward<Args>(args)...));
        }

        // Wide
        template<typename... Args>
        static inline void logf_impl(
            const source_loc& src_loc, 
            const log_level lv, 
            const fmt::wformat_string<Args...> fmt, 
            Args&&... args)
        {
            log_impl(src_loc, lv, fmt::format(fmt, std::forward<Args>(args)...));
        }

        // Assert condition and log error message.
        static inline void assert_impl(
            const bool condition, 
            const std::string_view expression, 
            const source_loc& src_loc,
            const std::string_view message = "")
        {
            if (condition) { return; }

            const uint32_t thread_id = static_cast<uint32_t>(::GetCurrentThreadId());
            const std::wstring 
                src_filename_utf16 = string::utf8_to_utf16(src_loc.filename()),
                expression_utf16 = string::utf8_to_utf16(expression),
                message_utf16 = string::utf8_to_utf16(message);

            logf_impl(src_loc, log_level::critical,
                L"ASSERT FAILED -> Expression: {}, Message: {}"
                , expression_utf16
                , message_utf16
            );

            const std::wstring message_text = fmt::format(
                L"Debug Assertion Failed!\n\n"
                L"Thread ID: {}\n"
                L"File: {}\n"
                L"Line: {}\n"
                L"Expression: {}\n"
                L"Message: {}\n\n"
                L"(Press Retry to debug the application)"
                , thread_id
                , src_filename_utf16
                , src_loc.line
                , expression_utf16
                , message_utf16
            );

            switch (const auto result = ::MessageBoxW(
                        nullptr,
                        message_text.c_str(),
                        L"Assertion Failed",
                        MB_ABORTRETRYIGNORE | MB_ICONERROR | MB_SETFOREGROUND
                    ); result)
            {
            case IDABORT:
                std::abort();
                break;
            case IDRETRY:
                __debugbreak();
                break;
            case IDIGNORE:
            default:
                break;
            }
        }

        // Assert HRESULT and log error message.
        static inline void assert_hresult_impl(
            const HRESULT hr, 
            const std::string_view expression, 
            const source_loc& src_loc,
            const std::string_view message = "")
        {
            if (SUCCEEDED(hr)) { return; }
            _com_error com_err{ hr };

            const uint32_t thread_id = static_cast<uint32_t>(::GetCurrentThreadId());
            const std::wstring_view com_err_msg_utf16{ com_err.ErrorMessage() };
            const std::wstring 
                src_filename_utf16 = string::utf8_to_utf16(src_loc.filename()),
                expression_utf16 = string::utf8_to_utf16(expression),
                message_utf16 = string::utf8_to_utf16(message);

            logf_impl(src_loc, log_level::critical,
                L"ASSERT_HR FAILED -> Expression: {}, Message: {}, HRESULT: {:#08x} ({})"
                , expression_utf16
                , message_utf16
                , static_cast<uint32_t>(hr)
                , com_err_msg_utf16
            );

            const std::wstring message_text = fmt::format(
                L"Debug Assertion Failed!\n\n"
                L"Thread ID: {}\n"
                L"File: {}\n"
                L"Line: {}\n"
                L"Expression: {}\n"
                L"Message: {}\n\n"
                L"HRESULT: 0x{:#08x}\n"
                L"{}\n\n"
                L"(Press Retry to debug the application)"
                , thread_id
                , src_filename_utf16
                , src_loc.line
                , expression_utf16
                , message_utf16
                , static_cast<uint32_t>(hr)
                , com_err_msg_utf16
            );

            switch (const auto result = ::MessageBoxW(
                        nullptr,
                        message_text.c_str(),
                        L"Assertion Failed",
                        MB_ABORTRETRYIGNORE | MB_ICONERROR | MB_SETFOREGROUND
                    ); result)
            {
            case IDABORT:
                std::abort();
                break;
            case IDRETRY:
                __debugbreak();
                break;
            case IDIGNORE:
            default:
                break;
            }
        }

    } // namespace internal

} // namespace