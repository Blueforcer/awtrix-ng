#pragma once

#include <cstdint>
#include <string>

#include "platform/linux/host/vendor/httplib.h"

namespace awtrix::host_http {

void sendJson(httplib::Response& res, int status, const std::string& body);
void sendError(httplib::Response& res, int status, const char* code, const char* message);
bool readsAlone(const httplib::Request& req);
bool readBody(const httplib::ContentReader& content, httplib::Request& req, httplib::Response& res,
              uint64_t maxBodyBytes);
void refuseBody(httplib::Response& res, uint64_t maxBodyBytes);
void discardBody(const httplib::Request& req, const httplib::ContentReader& content);

}
