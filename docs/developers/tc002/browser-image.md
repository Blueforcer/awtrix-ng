---
only: [tc002]
---

# TC002 browser image tools

The browser and desktop installers build the TC002 `res` image on the user's machine from the
clock's own stock `res` partition. They do this with single-threaded `sqfs2tar` and `tar2sqfs`
WebAssembly programs that run in a dedicated module Worker. The build recipe lives in
[`tools/tc002/browser-image/`](https://github.com/Blueforcer/awtrix-ng/tree/main/tools/tc002/browser-image);
the generated module is served from `docs/assets/tc002/image/`.

## How it works

Files travel through the programs' ordinary TAR input and output. JavaScript checks the archive,
changes the installer entries, then reads the finished SquashFS back and compares every entry
before returning it. No original device image is included, uploaded or saved by this module.

The public entry is `docs/assets/tc002/image/index.js`:

```js
await preload();
const { image, stockSha256, imageSha256, slot } = await buildResImage({
  stockRes, loader, loop, onProgress
});
```

| Name | Meaning |
|---|---|
| `stockRes`, `image` | `Uint8Array`: the stock `res` partition and the resulting image. |
| `loader` | The package's ARM `libawtrix-loader.so`. |
| `loop` | The package's ARM `loop.ko`, a relocatable ELF object that carries `vermagic=`. The caller validates the package and its file hashes. |
| `onProgress` | Called with the stages `reading`, `preparing`, `compressing` and `verifying`. |
| `slot` | The release slot the image leaves in the partition (see below). |
| `preload()` | Fills the browser HTTP cache before USB connects. Optional for correctness. |

The image holds the stock tree plus `lib/libawtrix-loader.so`, which the image's `etc/EasyUI.cfg`
names as the library to start, and `awtrix-ng/loop.ko`, without the vendor Bluetooth setup
programs. This is
the same image `build-res` makes (see [Install tools](install-tools.md#the-res-image)).

`slot.header` is the first 64 KiB erase block behind the squashfs, `slot.image` the next one,
where the release image starts, and `slot.capacity` the bytes from there to the end of the 8 MiB
partition (0 when no image block is left). The caller refuses a release image larger than
`capacity` before it writes anything.

### What is accepted

- Only stock SquashFS 4.0 with XZ and 128 KiB blocks, without compressor options.
- Rejected: extended attributes, device nodes, sparse TAR extensions, escaping paths,
  already installed trees (an existing loader or `awtrix-ng/`), and ambiguous or non-stock startup
  configuration. Unknown compression flags are rejected.
- Unchanged regular files, directories and symbolic links keep data, target, mode, numeric owner
  and second-resolution modification time. Symlink targets are kept as data and never followed.
- Regular-file hard links retain their shared inode. Links must name a regular entry with matching
  metadata. Configuration and removed Bluetooth files must have no hard-link aliases.
- `awtrix-ng/` is root-owned 0755, `loop.ko` root-owned 0644, the loader 0755 with the stock root
  owner. Every added entry carries the stock mkfs time.

### Limits

The output fits the 8 MiB `res` partition. TAR expansion is capped at 128 MiB, entries at 20,000,
WASM linear memory at 512 MiB, and the Worker at ten minutes.

squashfs-tools-ng sets the summary bits for always-fragments, uncompressed fragments and absent
xattrs differently from mksquashfs; the wrapper accepts only those differences and restores the
original bits. `--no-tail-packing` keeps the stock policy. Fragment blocks still carry their own
compression flags, and xattr tables must be absent. Independent native `unsquashfs` readback
verifies this normalisation.

The programs use [squashfs-tools-ng 1.3.2](https://infraroot.at/pub/squashfs/),
[XZ 5.8.4](https://tukaani.org/xz/) and [Emscripten](https://emscripten.org/). Neither library
uses pthreads, and Emscripten's pthread option is off, so standard module Workers need no shared
memory: the module runs with `crossOriginIsolated === false` and no `SharedArrayBuffer`, and needs
no COOP/COEP workaround on a static GitHub Pages site.

## Rebuilding

Use Linux or WSL with Python 3.12+, CMake, make, a C/C++ host toolchain and the usual Autoconf
configure prerequisites. Build caches must be outside the checkout:

```sh
python3 tools/tc002/browser-image/build.py \
  --work /var/tmp/awtrix-browser-image-build \
  --output "$PWD/docs/assets/tc002/image"
```

- Source archives are SHA-256 pinned in `build.py`.
- Emscripten 4.0.21 uses emsdk commit `b2436aafa7351ee1b581f15841f1b45ed716a279` and compiler
  release `d70a5da89b3e673bf6a482724478fc17e81e575e`. The first build downloads the SDK into the
  work directory; `--sdk` reuses that exact SDK for another clean work directory. No installed
  build objects are reused across clean directories.
- `clang-size-t.patch` selects generic checked overflow builtins for Clang's wasm32 `size_t` and
  includes Emscripten's `sys/sysmacros.h`. It does not disable bounds or integer-overflow checks.
  The build applies it and copies the patch next to the source archives.
- The output includes the source archives, build recipe, patch and licence texts. Publish the
  whole image directory together. Generated JS and WASM files change only through this build.

The Docs and installer workflows run this build in CI.

## Synthetic verification

`fixtures/stock.sqfs` contains only generated test text, a symlink and a synthetic EasyUI
configuration; it is not a vendor or device dump. Recreate it with Linux `mksquashfs` (4.6.1)
and run the tests:

```sh
python3 tools/tc002/browser-image/fixture.py tools/tc002/browser-image/fixtures/stock.sqfs
node --test webui/test/tc002-image.test.js
python3 -m unittest discover -s tools/tc002/install -p test_build_res_image.py -v
```

The fixture's SHA-256 is `d084057a9105fc2e7f248cd47c0b37c9def090dc832134fdb634d7a6b95f849c`. The
tests run the real WASM compressors and compare deterministic images, metadata, unchanged input
bytes, the added loader and `loop.ko` entries, the release slot layout and malformed inputs.
The Python adapter tests need Node.js and native squashfs-tools 4.6+ as well as the prepared WASM
tools. They create stock fixtures with the native tools and independently inspect the shared
engine's output, including ownership, modes, hardlinks and private output files. CI runs this
cross-check in the shared image-tool job and fails when its prerequisites are missing.

## Related

- [Desktop installer](desktop-installer.md)
- [Install tools](install-tools.md)
- [TC002 tools](tools.md)
