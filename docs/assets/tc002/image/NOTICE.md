# Browser SquashFS tools: licenses and source

`sqfs2tar.js`, `sqfs2tar.wasm`, `tar2sqfs.js`, and `tar2sqfs.wasm` are built from squashfs-tools-ng 1.3.2. The standalone tools are licensed under **GPL-3.0-or-later**; their libsquashfs component is **LGPL-3.0-or-later**. Copyright (C) 2019 and later David Oberhollenzer and contributors. Upstream per-file copyright notices are retained in the complete source archive.

The exact corresponding utility/library sources, the two-line Emscripten portability patch, and the executable build recipe are available beside these binaries under [sources/build.py](sources/build.py). [build.json](build.json) pins archive URLs, SHA-256 values and generated artifact hashes. The tools are separate command-line programs invoked through virtual files inside a Worker. Their GPL license applies to the distributed tools; AWTRIX application and firmware licensing is documented separately by the project.

Included dependencies and full license texts:

- [GNU GPL v3](licenses/GPLv3.txt), [GNU LGPL v3](licenses/LGPLv3.txt), and [upstream licensing overview](licenses/squashfs-tools-ng.md).
- XZ/liblzma 5.8.4: [0BSD](licenses/0BSD.txt), with [upstream licensing details](licenses/xz.txt). The bundled liblzma is the current 0BSD release; the older squashfs-tools-ng overview's public-domain description refers to older XZ versions.
- Squashfs-tools-ng's [xxHash notice](licenses/xxhash.txt), [hash table notice](licenses/hash_table.txt), and [musl compatibility notice](licenses/musl.txt).
- Emscripten 4.0.21 generated runtime: [MIT / NCSA and Node.js notices](licenses/emscripten.txt), [bundled musl notice](licenses/emscripten-musl.txt), and [compiler-rt license](licenses/compiler-rt.txt).

The archives are unmodified upstream releases. `sources/build.py` reproduces the modifications, disables pthreads and all compression libraries except XZ, and emits the JS/WASM programs with fixed settings. A clean Linux build may reuse the pinned SDK, but not compiled library objects. No manufacturer firmware, original res image, private device data, or signing material is distributed here.
