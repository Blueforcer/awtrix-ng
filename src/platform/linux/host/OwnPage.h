#pragma once

namespace httplib {
struct Request;
}

namespace awtrix::host {

bool fromOwnPage(const httplib::Request& req, const char* marker);

}
