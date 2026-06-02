#include <cstddef>
#include <string_view>
#include "http_routes.hpp"

bool HttpRoutes::requestMatchesRoute(std::string_view request, std::string_view route) {
	return request.starts_with(route);
}

bool HttpRoutes::exactMatch(std::string_view request, std::string_view route) {
	if (requestMatchesRoute(request, route)) {
		size_t n = route.size();
		return n == request.size() || request[n] == ' ' || request[n] == '?';
	}
	return false;
}
