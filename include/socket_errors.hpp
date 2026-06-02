#pragma once

#include <cstddef>
#include <string_view>

namespace SocketErrors {
	/** 
     * @brief The error message for socket allocation failures
     */
    inline constexpr std::string_view ERR_SOCKET = "[Web Server Error] Failed to allocate base server socket.\n";

    /** 
     * @brief The error message for binding failures
     */
    inline constexpr std::string_view ERR_BIND = "[Web Server Error] Binding network interface to port {} failed.\n";

    /** 
     * @brief The error message for listen failures
     */
    inline constexpr std::string_view ERR_LISTEN = "[Web Server Error] Backlog listener setup failed.\n";

    /** 
     * @brief The error message for non-blocking failures
     */
    inline constexpr std::string_view ERR_NONBLOCK = "[Web Server Error] Failed to set non-blocking on listener.\n";
}