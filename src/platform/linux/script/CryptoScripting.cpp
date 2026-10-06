#include "platform/linux/script/CryptoScripting.h"

#include <climits>

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>

#include "berry.h"

namespace awtrix::linux_script {
namespace {

constexpr const char* kSource = R"BERRY(
import global
var m = module('crypto')
var found = nil

global._crypto_mine_dispatch = def (nonce, hash, unused)
  if found == nil return end
  if nonce == '' found(nil, 'done') else found(nonce, bytes(hash)) end
end
global._crypto_mine_drop = def (unused1, unused2) found = nil end

m.md5 = _crypto_md5
m.sha1 = _crypto_sha1
m.sha256 = _crypto_sha256
m.sha256d = _crypto_sha256d
m.hmac_sha1 = _crypto_hmac_sha1
m.hmac_sha256 = _crypto_hmac_sha256
m.pbkdf2_hmac_sha256 = _crypto_pbkdf2
m.random = _crypto_random
m.mine = def (header, target, fn, opts)
  if type(fn) != 'function' raise 'value_error', 'mine callback must be a function' end
  if !isinstance(header, bytes) || header.size() != 80 raise 'value_error', 'mine header must be 80 bytes' end
  if !isinstance(target, bytes) || target.size() != 32 raise 'value_error', 'mine target must be 32 bytes' end
  var threads = opts != nil ? int(opts.find('threads', 0)) : 0
  if !_crypto_mine(header, target, threads)
    log('crypto: mining busy')
    return false
  end
  found = fn
  return true
end
m.mine_stop = def () if _crypto_mine_stop() found = nil end end
m.mine_rate = _crypto_mine_rate
m.mine_hashes = _crypto_mine_hashes
m.mine_best = _crypto_mine_best
m.mine_threads = _crypto_mine_threads
m.target = _crypto_target
m.difficulty = _crypto_difficulty
return m
)BERRY";

constexpr bint kMaxPbkdf2Work = 1000000;
constexpr bint kMaxLength = 1024;

const unsigned char kEmpty[1] = {};

struct Input {
  const unsigned char* data = kEmpty;
  size_t size = 0;
};

Input input(bvm* vm, int index) {
  Input in;
  if (be_top(vm) >= index && be_isstring(vm, index)) {
    in.data = reinterpret_cast<const unsigned char*>(be_tostring(vm, index));
    in.size = static_cast<size_t>(be_strlen(vm, index));
  } else if (be_top(vm) >= index && be_isbytes(vm, index)) {
    in.data = static_cast<const unsigned char*>(be_tobytes(vm, index, &in.size));
  } else {
    be_raise(vm, "value_error", "crypto takes bytes or a string");
  }
  if (!in.data) in.data = kEmpty;
  if (in.size > INT_MAX) be_raise(vm, "value_error", "crypto input too long");
  return in;
}

bint length(bvm* vm, int index, bint fallback) {
  if (be_top(vm) < index || be_isnil(vm, index)) return fallback;
  const bint n = be_isint(vm, index) ? be_toint(vm, index) : 0;
  if (n < 1 || n > kMaxLength) be_raise(vm, "value_error", "crypto length out of range");
  return n;
}

int push(bvm* vm, const unsigned char* out, size_t size) {
  be_pushbytes(vm, out, size);
  be_return(vm);
}

void fail(bvm* vm) { be_raise(vm, "runtime_error", "crypto failed"); }

int digest(bvm* vm, const EVP_MD* md) {
  const Input in = input(vm, 1);
  unsigned char out[EVP_MAX_MD_SIZE];
  unsigned int size = 0;
  if (!EVP_Digest(in.data, in.size, out, &size, md, nullptr)) fail(vm);
  return push(vm, out, size);
}

int hmac(bvm* vm, const EVP_MD* md) {
  const Input key = input(vm, 1);
  const Input msg = input(vm, 2);
  unsigned char out[EVP_MAX_MD_SIZE];
  unsigned int size = 0;
  if (!HMAC(md, key.data, static_cast<int>(key.size), msg.data, msg.size, out, &size)) fail(vm);
  return push(vm, out, size);
}

int md5(bvm* vm) { return digest(vm, EVP_md5()); }

int sha1(bvm* vm) { return digest(vm, EVP_sha1()); }

int sha256(bvm* vm) { return digest(vm, EVP_sha256()); }

int sha256d(bvm* vm) {
  const Input in = input(vm, 1);
  unsigned char out[32];
  if (!EVP_Digest(in.data, in.size, out, nullptr, EVP_sha256(), nullptr) ||
      !EVP_Digest(out, sizeof out, out, nullptr, EVP_sha256(), nullptr))
    fail(vm);
  return push(vm, out, sizeof out);
}

int hmacSha1(bvm* vm) { return hmac(vm, EVP_sha1()); }

int hmacSha256(bvm* vm) { return hmac(vm, EVP_sha256()); }

int pbkdf2(bvm* vm) {
  const Input password = input(vm, 1);
  const Input salt = input(vm, 2);
  const bint iterations = be_top(vm) >= 3 && be_isint(vm, 3) ? be_toint(vm, 3) : 0;
  const bint size = length(vm, 4, 32);
  if (iterations < 1 || iterations > kMaxPbkdf2Work / ((size + 31) / 32))
    be_raise(vm, "value_error", "pbkdf2 iterations out of range");
  unsigned char out[kMaxLength];
  if (!PKCS5_PBKDF2_HMAC(reinterpret_cast<const char*>(password.data), static_cast<int>(password.size), salt.data,
                         static_cast<int>(salt.size), static_cast<int>(iterations), EVP_sha256(),
                         static_cast<int>(size), out))
    fail(vm);
  return push(vm, out, static_cast<size_t>(size));
}

int randomBytes(bvm* vm) {
  const bint size = length(vm, 1, 0);
  if (!size) be_raise(vm, "value_error", "crypto length out of range");
  unsigned char out[kMaxLength];
  if (RAND_bytes(out, static_cast<int>(size)) != 1) fail(vm);
  return push(vm, out, static_cast<size_t>(size));
}

}

void CryptoScripting::install(script::ScriptExtensionHost& host) {
  host.defineNative("_crypto_md5", &md5, this);
  host.defineNative("_crypto_sha1", &sha1, this);
  host.defineNative("_crypto_sha256", &sha256, this);
  host.defineNative("_crypto_sha256d", &sha256d, this);
  host.defineNative("_crypto_hmac_sha1", &hmacSha1, this);
  host.defineNative("_crypto_hmac_sha256", &hmacSha256, this);
  host.defineNative("_crypto_pbkdf2", &pbkdf2, this);
  host.defineNative("_crypto_random", &randomBytes, this);
  miner_.install(host);
  host.defineModule("crypto", kSource);
}

}
