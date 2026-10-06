---
only: [tc002]
---

# TC002 OpenSSL

`bin/awtrix-linux` on the TC002 links a static, trimmed OpenSSL 3.5.8, built with the musl
toolchain `TC002_MUSL_PREFIX` (see
[`tools/tc002/toolchain/env.sh`](https://github.com/Blueforcer/awtrix-ng/blob/main/tools/tc002/toolchain/env.sh)).
The recipe lives in
[`tools/tc002/openssl/`](https://github.com/Blueforcer/awtrix-ng/tree/main/tools/tc002/openssl).
`build_bundle.sh` runs it and points CMake at the result (`OPENSSL_INCLUDE_DIR`,
`OPENSSL_CRYPTO_LIBRARY`, `OPENSSL_SSL_LIBRARY`); the bundle build fails when `awtrix-linux` links
any other `libcrypto.a`.

## Build

```sh
tools/tc002/openssl/build.sh WORKDIR [OUTDIR]   # OUTDIR defaults to WORKDIR/out
```

OUTDIR holds `include/`, `lib/libcrypto.a`, `lib/libssl.a` and `LICENSE.txt`. A second run reuses
it while the recipe, the tarball and the compiler are unchanged.

The tarball comes through `TC002_DL_DIR`, where it is fetched once. The build runs in the source
tree with the compiler called by its bare name and writes deterministic archives, so the
libraries embed no build path or time; the recipe fails if they hold the build path. Two builds
in different directories produce the same bytes.

## Configuration

`Configure linux-armv4 --prefix=/usr --openssldir=/etc/ssl` with `-Os -ffunction-sections
-fdata-sections -mthumb -DOPENSSL_SMALL_FOOTPRINT` and the options in `build.sh`: static only
(`no-shared no-pic no-module`), assembler and ML-KEM kept.

**Kept:** TLS 1.2 and 1.3 client and server; X25519, X25519MLKEM768, P-256 and P-384 key
exchange; RSA, ECDSA and Ed25519 certificates and signatures; AES-GCM, AES-CBC and
ChaCha20-Poly1305; SHA-1, SHA-2 and SHA-3.

**Left out:** DH and FFDHE (servers that offer only DHE-RSA or TLS 1.3 ffdhe groups do not
connect), TLS 1.0/1.1, SSLv3, QUIC, DTLS, engines, the legacy provider, DSA, 3DES, deprecated APIs,
external PSK, SRP, CMS, OCSP, CT, CMP, the HTTP client, `openssl.cnf` autoloading, and ciphers
nobody negotiates today (Camellia, ARIA, SEED, SM2/3/4, IDEA, RC2/4/5, Blowfish, CAST, GOST,
Whirlpool, MD4, MDC2, RIPEMD, BLAKE2).

`awtrix-linux` built this way (with the application flags of `build_bundle.sh`) passes the ARM
contract, security, header-limit and update-verifier suites under `qemu-arm -cpu cortex-a7`.

## Reference

| Component | Version | SHA-256 | Also checked against |
|---|---|---|---|
| [OpenSSL](https://github.com/openssl/openssl/releases) | 3.5.8 | `a8f84a39918ec6415ce765d9b429d313ba97b8143169c172e734b9514464f5b2` | Buildroot 2025.02.18 `package/libopenssl` |

| Output | Size | SHA-256 |
|---|---|---|
| `libcrypto.a` | 5540634 bytes | `13cd4af2ff2ff67fe5fa4025630a881d92de44aff2a0ea9208eedeeec037646d` |
| `libssl.a` | 859400 bytes | `a1fda7886679e104a78e5b5a179f23b2f7000c37087bc388e64442a556838331` |

## Licence

OpenSSL 3.x is licensed under Apache-2.0. `LICENSE.txt` goes into the bundle's
`share/licenses.txt.gz`.

## Related

- [CA certificates](ca-certificates.md)
- [Install tools](install-tools.md)
- [TC002 tools](tools.md)
