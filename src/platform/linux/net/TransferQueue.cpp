#include "platform/linux/net/TransferQueue.h"

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <utility>

#include "platform/linux/host/vendor/httplib.h"
#include "platform/posix/Files.h"

namespace awtrix::net {
namespace {

template <class Entry, class Drop>
void dropOwned(std::deque<Entry>& entries, const std::string& owner, Drop drop) {
  for (auto it = entries.begin(); it != entries.end();) {
    if (it->owner != owner) {
      ++it;
      continue;
    }
    drop(*it);
    it = entries.erase(it);
  }
}

}

TransferQueue::TransferQueue(std::size_t perOwner, FillRequest fill) : perOwner_(perOwner), fill_(std::move(fill)) {}

TransferQueue::~TransferQueue() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stopping_ = true;
    queue_.clear();
    interrupt_.interrupt();
  }
  ready_.notify_all();
  if (worker_.joinable()) worker_.join();
  for (const TransferResult& result : results_)
    if (!result.file.empty()) ::unlink(result.file.c_str());
}

bool TransferQueue::start(TransferRequest request) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto queued = std::count_if(queue_.begin(), queue_.end(),
                                    [&](const TransferRequest& r) { return r.owner == request.owner; });
  if (stopping_ || static_cast<std::size_t>(queued) + (runs(request.owner) ? 1 : 0) >= perOwner_) return false;
  queue_.push_back(std::move(request));
  if (!worker_.joinable()) worker_ = std::thread([this] { run(); });
  ready_.notify_one();
  return true;
}

std::vector<int> TransferQueue::cancel(const std::string& owner) {
  std::vector<int> dropped;
  std::lock_guard<std::mutex> lock(mutex_);
  if (runs(owner)) {
    dropped.push_back(runningId_);
    cancelled_ = true;
    interrupt_.interrupt();
  }
  dropOwned(queue_, owner, [&](const TransferRequest& request) { dropped.push_back(request.id); });
  dropOwned(results_, owner, [&](const TransferResult& result) {
    if (!result.file.empty()) ::unlink(result.file.c_str());
    dropped.push_back(result.id);
  });
  return dropped;
}

bool TransferQueue::poll(TransferResult& result) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (results_.empty()) return false;
  result = std::move(results_.front());
  results_.pop_front();
  return true;
}

bool TransferQueue::runs(const std::string& owner) const {
  return running_ && !cancelled_ && runningOwner_ == owner;
}

void TransferQueue::run() {
  for (;;) {
    TransferRequest request;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      ready_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
      if (stopping_) return;
      request = std::move(queue_.front());
      queue_.pop_front();
      running_ = true;
      runningOwner_ = request.owner;
      runningId_ = request.id;
      cancelled_ = false;
      interrupt_.rearm();
    }
    TransferResult result = perform(request);
    std::lock_guard<std::mutex> lock(mutex_);
    running_ = false;
    if (cancelled_ || stopping_) {
      if (!result.file.empty()) ::unlink(result.file.c_str());
    } else {
      results_.push_back(std::move(result));
    }
    if (stopping_) return;
  }
}

TransferResult TransferQueue::perform(TransferRequest& request) {
  TransferResult result;
  result.owner = request.owner;
  result.id = request.id;
  std::string url = request.origin + request.target;
  const RequestFill filled = fill_ ? fill_(request.owner, url, request.headers) : RequestFill::Unchanged;
  if (filled == RequestFill::Refused || (filled == RequestFill::Filled && !splitUrl(url, request.origin, request.target)))
    return result;
  httplib::Client client(request.origin);
  ClientLimits limits;
  limits.timeoutMs = request.timeoutMs;
  limits.ioTimeoutSec = static_cast<int>((request.timeoutMs + 999) / 1000);
  limits.followRedirects = request.followRedirects && filled == RequestFill::Unchanged;
  configure(client, limits);
  interrupt_.watch(client);
  if (request.download) {
    download(client, request, result);
  } else {
    FilteredAnswer answer =
        sendFiltered(client, request.method, request.target, std::move(request.body), request.headers, request.filter);
    result.status = answer.status;
    result.ok = answer.ok;
    result.body = std::move(answer.body);
  }
  interrupt_.release();
  return result;
}

void TransferQueue::download(httplib::Client& client, TransferRequest& request, TransferResult& result) {
  const TransferDownload& wanted = *request.download;
  posix::UniqueFd file(::open(wanted.path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0600));
  if (!file.valid()) return;
  bool refused = false;
  ResponseState state;
  const BodyPolicy policy{wanted.maxBytes,
                          [&](int status, const std::string& contentType) {
                            refused = status != httplib::StatusCode::OK_200 ||
                                      (wanted.acceptType && !wanted.acceptType(contentType));
                            return !refused;
                          },
                          [&](std::size_t more) {
                            result.noRoom = wanted.room && !wanted.room(more);
                            return !result.noRoom;
                          }};
  const auto response = exchange(client, request.method, request.target, std::move(request.body), request.headers,
                                 policy, state, [&](const char* bytes, std::size_t count) {
    return posix::writeAll(file.get(), bytes, count);
  });
  if (response) result.status = response->status;
  else if (refused || state.tooLarge || result.noRoom) result.status = state.status;
  const bool written = ::close(file.release()) == 0;
  std::string contentType = state.contentType;
  if (response && result.status == httplib::StatusCode::OK_200 && written &&
      (!wanted.verify || wanted.verify(wanted.path, contentType))) {
    result.ok = true;
    result.file = wanted.path;
    result.contentType = std::move(contentType);
    result.fileBytes = state.receivedBytes;
    return;
  }
  ::unlink(wanted.path.c_str());
}

}
