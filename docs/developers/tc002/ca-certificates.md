---
only: [tc002]
---

# TC002 CA certificates

A TC002 release ships the Mozilla CA store in PEM form as `share/ca-certificates.crt`.
`awtrix-linux` loads it once (`--ca-file`) and verifies every HTTPS connection of scripts and
radio streams against it. The recipe lives in
[`tools/tc002/ca/`](https://github.com/Blueforcer/awtrix-ng/tree/main/tools/tc002/ca), and
`build_bundle.sh` runs it (see [Install tools](install-tools.md)).

## How it works

The recipe builds the bundle the way Buildroot's `ca-certificates` package builds
`/etc/ssl/certs/ca-certificates.crt`: Debian's `certdata2pem.py` converts Mozilla's
`certdata.txt`, and the certificates are joined in byte order of their file names.

## Build

```sh
tools/tc002/ca/build.sh WORKDIR [OUTDIR]   # OUTDIR defaults to WORKDIR/out
```

The script needs `python3` with the `cryptography` module. OUTDIR receives `ca-certificates.crt`
and `NOTICE`, the MPL-2.0 notice that goes into `share/licenses.txt.gz` together with the
complete [MPL-2.0 text](https://github.com/Blueforcer/awtrix-ng/blob/main/tools/tc002/ca/MPL-2.0.txt).
That licence text is copied from [Mozilla](https://www.mozilla.org/en-US/MPL/2.0/) and does not
alter the certificate data or its checksum.

## Reference

| Input | Version | SHA-256 |
|---|---|---|
| [Debian ca-certificates](https://snapshot.debian.org/archive/debian/20260223T202245Z/pool/main/c/ca-certificates/) | 20260223 | `2fa2b00d4360f0d14ec51640ae8aea9e563956b95ea786e3c3c01c4eead42b56` (as in Buildroot 2025.02.18 `package/ca-certificates`) |

| Output | Certificates | Size | SHA-256 |
|---|---|---|---|
| `ca-certificates.crt` | 144 | 216590 bytes | `3d664e3fa39e5d3e95f653127b067ef02e5d450112b2f4d65f1ad89b0a62a506` |

The output checksum is pinned too and matches the bundle of the Buildroot system. A new store
means a new tarball: update both checksums together.

## Related

- [OpenSSL](openssl.md)
- [Install tools](install-tools.md)
- [TC002 tools](tools.md)
