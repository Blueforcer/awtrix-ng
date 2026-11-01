#include "platform/tc002/voice/VoiceTransfers.h"

#include <unistd.h>

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
constexpr int64_t kMsPerSecond = 1000;
constexpr const char* kTextType = "application/json";
constexpr const char* kDownloadTypes[] = {"audio/mpeg", "audio/wav", "audio/x-wav", "application/octet-stream"};
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

int attachClip(const Clip& clip, const VoiceTransfers::SendOptions& options, net::TransferRequest& request) {
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

VoiceTransfers::VoiceTransfers(ClipStore& clips, MemoryRoom memory, net::FillRequest fill)
    : clips_(clips), memory_(std::move(memory)), queue_(kTransfersPerApp, std::move(fill)) {}

VoiceTransfers::Done VoiceTransfers::report(std::string app, Kind kind, int number, int status) {
  Done out{};
  out.app = std::move(app);
  out.kind = kind;
  out.number = number;
  out.status = status;
  return out;
}

int VoiceTransfers::send(const std::string& app, SendData data, const SendOptions& options) {
  const ClipId* const id = std::get_if<ClipId>(&data);
  const Clip* const clip = id ? clips_.find(app, *id) : nullptr;
  if (id && !clip) return kGone;
  net::TransferRequest request;
  if (options.keep < 0 || !net::splitUrl(options.url, request.origin, request.target) ||
      !script::normalizeMethod(options.method, request.method) ||
      !script::parseHeaderBlock(options.headers, request.headers))
    return kBad;
  if (clip) {
    const int attached = attachClip(*clip, options, request);
    if (attached != kOk) return attached;
  } else {
    request.body = std::move(std::get<std::string>(data));
    if (!script::hasHeader(request.headers, "Content-Type")) request.headers.emplace_back("Content-Type", kTextType);
  }
  request.owner = app;
  request.timeoutMs = options.timeoutSec * kMsPerSecond;
  request.filter = {options.find, static_cast<std::size_t>(options.keep),
                    options.cap > 0 ? static_cast<std::size_t>(options.cap) : script::kMaxHttpBody};
  return start(std::move(request), Kind::Send, 0);
}

int VoiceTransfers::fetch(const std::string& app, const std::string& url, const std::string& method,
                          std::string body, const std::string& headers, uint32_t timeoutSec) {
  net::TransferRequest request;
  if (!net::splitUrl(url, request.origin, request.target) || !script::normalizeMethod(method, request.method) ||
      !script::parseHeaderBlock(headers, request.headers))
    return kBad;
  if (!clips_.ready()) return kBusy;
  const ClipId clip = clips_.reserve(app, kFetchBytes);
  if (!clip) return kFull;
  request.owner = app;
  request.body = std::move(body);
  request.timeoutMs = timeoutSec * kMsPerSecond;
  request.followRedirects = request.method == "GET";
  request.download = net::TransferDownload{clips_.path(clip), kFetchBytes, downloadableType, verifyClip,
                                           [memory = memory_](std::size_t more) { return memory.fits(more); }};
  const int number = start(std::move(request), Kind::Fetch, clip);
  if (number < 0) clips_.release(app, clip);
  return number;
}

int VoiceTransfers::start(net::TransferRequest request, Kind kind, ClipId clip) {
  number_ = posix::nextInSequence(number_);
  request.id = number_;
  Transfer transfer{request.owner, kind, clip};
  if (!queue_.start(std::move(request))) return kBusy;
  transfers_[number_] = std::move(transfer);
  return number_;
}

std::optional<VoiceTransfers::Transfer> VoiceTransfers::take(int number) {
  const auto it = transfers_.find(number);
  if (it == transfers_.end()) return std::nullopt;
  Transfer transfer = std::move(it->second);
  transfers_.erase(it);
  return transfer;
}

std::vector<VoiceTransfers::Done> VoiceTransfers::cancel(const std::string& app) {
  std::vector<Done> cancelled;
  for (const int number : queue_.cancel(app)) {
    const std::optional<Transfer> transfer = take(number);
    if (!transfer) continue;
    clips_.release(app, transfer->clip);
    cancelled.push_back(report(app, transfer->kind, number, kCancelled));
  }
  return cancelled;
}

std::optional<VoiceTransfers::Done> VoiceTransfers::poll() {
  net::TransferResult result;
  while (!transfers_.empty() && queue_.poll(result))
    if (std::optional<Done> finished = finish(result)) return finished;
  return std::nullopt;
}

std::optional<VoiceTransfers::Done> VoiceTransfers::finish(net::TransferResult& result) {
  const std::optional<Transfer> transfer = take(result.id);
  if (transfer && transfer->kind == Kind::Send) {
    Done answer = report(result.owner, Kind::Send, result.id, result.status);
    if (result.ok) answer.text = std::move(result.body);
    return answer;
  }
  const ClipFormat* const format = result.ok ? clipFormatOf(result.contentType) : nullptr;
  if (transfer && format && clips_.commit(transfer->clip, result.fileBytes, 0, format->kind)) {
    Done download = report(result.owner, Kind::Fetch, result.id, kOk);
    download.clip = transfer->clip;
    download.text = format->extension;
    return download;
  }
  if (!result.file.empty()) ::unlink(result.file.c_str());
  if (!transfer) return std::nullopt;
  clips_.release(result.owner, transfer->clip);
  return report(result.owner, Kind::Fetch, result.id, failedFetchStatus(result));
}

bool VoiceTransfers::holds(const std::string& app) const {
  return std::any_of(transfers_.begin(), transfers_.end(), [&](const auto& entry) { return entry.second.app == app; });
}

}
