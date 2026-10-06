#pragma once

#include "core/api/ApiRouter.h"
#include "core/sound/AudioRouter.h"

namespace awtrix::tc002 {

template<class Sink>
DispatchResult playClip(Sink* sink, sound::AudioRouter& router, std::string&& bytes,
                        DispatchDetail& detail) {
  if (!sink || !sink->clips()) {
    detail.message = "no clip playback";
    return DispatchResult::Unavailable;
  }
  const auto result = sink->playClip(std::move(bytes), detail.message);
  if (result == DispatchResult::Unavailable) detail.message = "speaker unavailable";
  if (result == DispatchResult::Ok) {
    sound::Spec clip;
    clip.text = "clip";
    router.adoptPcm(clip);
  }
  return result;
}

template<class Sink>
int routeClip(const std::string& method, const std::string& path, const std::string& request,
              std::string& body, Sink* sink, sound::AudioRouter& router) {
  if (path != "/api/v1/audio/clip") return 0;
  if (method != "POST") {
    body = api::errorJson("methodNotAllowed", "allowed: POST");
    return 405;
  }
  DispatchDetail detail;
  DispatchResult result;
  if (request.empty()) {
    detail.message = "body required";
    result = DispatchResult::ValidationError;
  } else {
    result = playClip(sink, router, std::string(request), detail);
  }
  auto response = api::httpResponse(Command{}, result, detail);
  body = std::move(response.body);
  return response.status;
}

}
