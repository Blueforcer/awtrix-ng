#pragma once

namespace httplib {
struct Request;
struct Response;
}

namespace awtrix::host {

bool fromOwnPage(const httplib::Request& req, const char* marker);
bool refuseForeign(const httplib::Request& req, httplib::Response& res, const char* marker);

}
