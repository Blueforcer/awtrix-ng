#include "platform/tc002/voice/VoiceTransfers.h"

#include <algorithm>
#include <iterator>
#include <memory>
#include <utility>

#include "core/net/MediaType.h"
#include "core/script/HttpHeaders.h"
#include "core/script/ScriptServices.h"
#include "platform/linux/net/HttpClient.h"
#include "platform/linux/net/Multipart.h"
#include "platform/posix/Files.h"
#include "platform/posix/Sequence.h"
#include "platform/tc002/audio/Tc002AudioSources.h"
#include "platform/tc002/voice/VoiceStatus.h"

namespace awtrix::tc002::voice {
namespace {

constexpr char kFieldSeparator = '\x1f';
constexpr std::size_t kMaxFormFields = 16;
constexpr std::size_t kTransfersPerApp = 2;
constexpr std::size_t kFetchBytes = 2 * 1024 * 1024;
constexpr std::size_t kBodyBytesAcrossApps = 4 * 1024 * 1024;
constexpr std::uint64_t kCopiesHeld = 2;
constexpr int64_t kMsPerSecond = 1000;
constexpr const char* kDownloadTypes[] = {"audio/mpeg", "audio/mp3",   "audio/wav",
                                          "audio/wave", "audio/x-wav", "application/octet-stream"};
constexpr const char* kUploadName = "speech";

bool downloadableType(const std::string& contentType) {
  const std::string type = net::mediaType(contentType);
  return std::find(std::begin(kDownloadTypes), std::end(kDownloadTypes), type) != std::end(kDownloadTypes);
}

bool verifyClip(const std::string& path, std::string& contentType) {
  const posix::MappedFile file(path, kFetchBytes);
  std::string error;
  const std::optional<ClipKind> kind = file.valid() ? checkClip(file.bytes(), error) : std::nullopt;
  if (kind) contentType = clipFormat(*kind).mediaType;
  return kind.has_value();
}

int failedFetchStatus(const net::TransferResult& result) {
  if (result.noRoom) return kFull;
  return result.status == kOk ? kBad : result.status;
}

bool formFields(const std::string& block, std::vector<net::FormField>& out) {
  for (std::size_t at = 0; !block.empty() && at <= block.size();) {
    const std::size_t end = std::min(block.find(kFieldSeparator, at), block.size());
    const std::size_t equals = block.find('=', at);
    if (equals >= end || out.size() == kMaxFormFields) return false;
    out.push_back({block.substr(at, equals - at), block.substr(equals + 1, end - equals - 1)});
    at = end + 1;
  }
  return true;
}

bool describe(const std::string& app, const VoiceTransfers::Options& options, net::TransferRequest& request) {
  if (!net::validUrl(options.url) || !script::normalizeMethod(options.method, request.method) ||
      !script::parseHeaderBlock(options.headers, request.headers))
    return false;
  request.owner = app;
  request.url = options.url;
  request.timeoutMs = options.timeoutSec * kMsPerSecond;
  return true;
}

int attachClip(const Clip& clip, const VoiceTransfers::Options& options, net::TransferRequest& request) {
  std::vector<net::FormField> parts;
  const bool raw = options.as == "raw";
  const bool typed = script::hasHeader(request.headers, "Content-Type");
  if ((!raw && (options.as != "form" || typed)) || !formFields(options.fields, parts)) return kBad;
  auto audio = std::make_shared<const posix::MappedFile>(clip.path, ClipStore::kBytesPerApp);
  if (!audio->valid()) return kGone;
  const ClipFormat& format = clipFormat(clip.kind);
  if (raw) {
    request.body = net::RequestBody({}, std::move(audio), {});
    if (!typed) request.headers.emplace_back("Content-Type", format.mediaType);
    return kOk;
  }
  std::optional<net::Form> form =
      net::buildMultipart(parts, {options.field, std::string(kUploadName) + "." + format.extension, format.mediaType,
                                  audio->bytes()});
  if (!form) return kBad;
  request.body = net::RequestBody(std::move(form->head), std::move(audio), std::move(form->tail));
  request.headers.emplace_back("Content-Type", std::move(form->contentType));
  return kOk;
}

}

VoiceTransfers::VoiceTransfers(ClipStore& clips, net::FillRequest fill)
    : clips_(clips), queue_(kTransfersPerApp, std::move(fill)) {}

int VoiceTransfers::send(const std::string& app, ClipId id, const Options& options) {
  const Clip* const clip = clips_.find(app, id);
  if (!clip) return kGone;
  net::TransferRequest request;
  if (options.keep < 0 || !describe(app, options, request)) return kBad;
  const int attached = attachClip(*clip, options, request);
  if (attached != kOk) return attached;
  request.filter = {options.find, static_cast<std::size_t>(options.keep),
                    options.cap > 0 ? static_cast<std::size_t>(options.cap) : script::kMaxHttpBody};
  return start(std::move(request), VoiceEvent::Kind::Send, 0);
}

int VoiceTransfers::fetch(const std::string& app, std::string body, const Options& options) {
  net::TransferRequest request;
  if (!describe(app, options, request)) return kBad;
  if (!clips_.ready()) return kBusy;
  const ClipId clip = clips_.reserve(app, kFetchBytes);
  if (!clip) return kFull;
  request.body = std::move(body);
  request.followRedirects = request.method == "GET";
  request.download = net::TransferDownload{clips_.path(clip), kFetchBytes, downloadableType, verifyClip,
                                           [this](std::size_t more) { return clips_.fits(more); }};
  const int number = start(std::move(request), VoiceEvent::Kind::Fetch, clip);
  if (number < 0) clips_.release(app, clip);
  return number;
}

int VoiceTransfers::start(net::TransferRequest request, VoiceEvent::Kind kind, ClipId clip) {
  const std::size_t bodyBytes = request.body.head.size() + request.body.tail.size();
  const std::size_t filledCopy = request.body.fillable() ? bodyBytes + net::kMaxFillGrowth : 0;
  if (bodyBytes > kBodyBytesAcrossApps - bodyBytes_ ||
      !clips_.fits(kCopiesHeld * (bodyBytes + request.filter.cap) + filledCopy))
    return kFull;
  number_ = posix::nextInSequence(number_);
  request.id = number_;
  Transfer transfer{request.owner, kind, clip, bodyBytes};
  if (!queue_.start(std::move(request))) return kBusy;
  bodyBytes_ += bodyBytes;
  transfers_[number_] = std::move(transfer);
  return number_;
}

VoiceTransfers::Transfer VoiceTransfers::take(int number) {
  auto entry = transfers_.extract(number);
  bodyBytes_ -= entry.mapped().bodyBytes;
  return std::move(entry.mapped());
}

std::vector<VoiceEvent> VoiceTransfers::cancel(const std::string& app) {
  std::vector<VoiceEvent> cancelled;
  for (const int number : queue_.cancel(app)) {
    const Transfer transfer = take(number);
    clips_.release(app, transfer.clip);
    cancelled.push_back(VoiceEvent::outcome(app, transfer.kind, number, kCancelled));
  }
  return cancelled;
}

std::optional<VoiceEvent> VoiceTransfers::poll() {
  net::TransferResult result;
  if (transfers_.empty() || !queue_.poll(result)) return std::nullopt;
  return finish(result);
}

VoiceEvent VoiceTransfers::finish(net::TransferResult& result) {
  const Transfer transfer = take(result.id);
  if (transfer.kind == VoiceEvent::Kind::Send) {
    VoiceEvent answer = VoiceEvent::outcome(result.owner, transfer.kind, result.id, result.status);
    if (result.ok) answer.body = std::move(result.body);
    return answer;
  }
  const ClipFormat* const format = result.ok ? clipFormatOf(result.contentType) : nullptr;
  if (format && clips_.commit(transfer.clip, result.fileBytes, format->kind))
    return VoiceEvent::ofClip(result.owner, transfer.kind, result.id, transfer.clip, 0, format->extension);
  clips_.release(result.owner, transfer.clip);
  return VoiceEvent::outcome(result.owner, transfer.kind, result.id, failedFetchStatus(result));
}

bool VoiceTransfers::holds(const std::string& app) const {
  return holdsAny([&app](const std::string& owner) { return owner == app; });
}

bool VoiceTransfers::holdsAny(const std::function<bool(const std::string&)>& app) const {
  return std::any_of(transfers_.begin(), transfers_.end(), [&](const auto& entry) { return app(entry.second.app); });
}

}
