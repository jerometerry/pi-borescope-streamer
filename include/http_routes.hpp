#pragma once

#include <cstddef>
#include <string_view>

namespace HttpRoutes {
	
	bool requestMatchesRoute(std::string_view request, std::string_view route);
	bool exactMatch(std::string_view request, std::string_view route);

	/** 
     * @brief The route for the web page
     */
    inline constexpr std::string_view ROUTE_DASHBOARD = "GET /dashboard";

    /** 
     * @brief The route for the cameras endpoint
     */
    inline constexpr std::string_view ROUTE_CAMERAS = "GET /api/cameras";

    /** 
     * @brief The route for the snapshot endpoint
     */
    inline constexpr std::string_view ROUTE_SNAPSHOT = "GET /snapshot";

    /** 
     * @brief The route for the favicon endpoint
     */
    inline constexpr std::string_view ROUTE_FAVICON =  "GET /favicon.ico";

    /** 
     * @brief
     */
    inline constexpr std::string_view ROUTE_STREAM =  "GET / HTTP";

    /** 
     * @brief 
     */
    inline constexpr std::string_view ROUTE_STREAM_QUERY =  "GET /?";
}