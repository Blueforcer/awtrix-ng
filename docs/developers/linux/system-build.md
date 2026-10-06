---
only: [tc002]
---

# ARM system build

The system-build runner builds AWTRIX on Linux for 32-bit ARM with a pinned
[Buildroot](https://buildroot.org/) tree. One run produces:

- the **musl cross toolchain** (in `output/host/`) that builds every program of a TC002 release –
  the TC002 recipes take it as `TC002_MUSL_PREFIX`;
- an ARM **userspace archive** with the cross-compiled `awtrix-linux`, used for emulated tests
  and license evidence.

It does not flash a device, build a kernel or produce a bootable image. The tools live in
[`tools/system/`](https://github.com/Blueforcer/awtrix-ng/tree/main/tools/system); the Buildroot
configuration they use is described in [Buildroot configuration](buildroot.md).

## How it works

`build.py` works in a dedicated **work directory** and runs in steps:

| Command | What it does |
|---|---|
| `doctor` | Reports missing host tools. It installs nothing. |
| `prepare` | Resolves a local Git commit and archives only the application's build inputs from it (uncommitted edits are not included). Copies the Buildroot external tree from the checkout and records every file's hash, so recipe changes can be tested before committing. Verifies the pinned Buildroot archive before extracting it. |
| `verify` | Checks the application archive, extracted application, external recipes, Buildroot archive and extracted Buildroot tree against the recorded hashes. |
| `configure` | Applies the checked-in ARM profile `awtrix_armv7_defconfig`. |
| `build` | Configures again and builds, with `--jobs` compiler jobs. |
| `toolchain` | Prepares the work directory when it is new (without the application license check, which only the application needs), configures and builds only the cross toolchain (`output/host/`), with `--jobs` compiler jobs. |
| `legal-info` | Collects license and source records. |

Inputs stay frozen across retries; use a new work directory when you change the application
commit or the recipes. The runner refuses non-empty directories without its marker file. A failed
`prepare` is kept for inspection; choose another directory to retry. Existing cache corruption
and partial downloads cause errors instead of silently replacing files.

Buildroot's filesystem skeletons contain absolute links for the target system. Extraction creates
links last and rejects archive entries that would write through them; source-tree link targets
are recorded without following links out of the tree.

Before downloading Buildroot, `prepare` checks every application license file named by the recipe
against the exact exported Git bytes. Hashes are computed from raw bytes without newline
conversion.

## Build

Use Python 3.12 or later on Linux or WSL. On Debian or Ubuntu:

```sh
sudo apt-get install build-essential git python3 rsync patch bc bison flex \
  file unzip cpio wget xz-utils
python3 tools/system/build.py doctor
```

Use a work directory on the Linux filesystem, outside the source checkout. Do not use `/tmp` for
results that must survive a WSL restart.

```sh
python3 tools/system/build.py prepare --work "$HOME/awtrix-armv7-build" --revision HEAD
python3 tools/system/build.py verify --work "$HOME/awtrix-armv7-build"
python3 tools/system/build.py configure --work "$HOME/awtrix-armv7-build"
python3 tools/system/build.py build --work "$HOME/awtrix-armv7-build" --jobs 4
python3 tools/system/build.py legal-info --work "$HOME/awtrix-armv7-build"
```

Building the toolchain takes much longer than an application rebuild. For TC002 work,
`build.py toolchain --work DIR` alone is enough; point the recipes at the result:

```sh
export TC002_MUSL_PREFIX="$HOME/awtrix-armv7-build/output/host/bin/arm-linux-"
```

### Outputs

| File | Content |
|---|---|
| `inputs.json` | Application commit, `SOURCE_DATE_EPOCH`, source lock and frozen file hashes |
| `output/.config` | Resolved Buildroot configuration |
| `output/host/` | The cross toolchain |
| `output/target/usr/bin/awtrix-linux` | Cross-compiled application |
| `output/images/rootfs.tar` | Userspace archive, without kernel or bootloader |
| `build-result.json` | Hashes and sizes, written after the build and the ARM ELF check succeed |
| `output/legal-info/` | License and source records from `legal-info` |

`legal-info` also copies the exact Buildroot archive, application archive, frozen external
recipes and input manifest into `output/legal-info/build-inputs/` with checksums. Buildroot's own
`legal-info` README asks the developer to collect these by hand; the original README is kept.
This is source evidence, not a completed license audit or SBOM (software bill of materials).

`build-result.json` records `bootable_tc002_image: false`, `tc002_hardware_verified: false` and
`bit_reproducibility_tested: false`.

The build, qualification and reproduction tools write each JSON record to `<name>.partial`,
created exclusively, and rename it after `fsync`. An existing `.partial` file or link at that
path stops the command; remove it only after checking that no other run uses the directory.

## Test

### Offline tests

```sh
python3 -m unittest discover -s tools/system -p 'test_*.py' -v
```

The tests use temporary files, a temporary Git repository and mocked downloads. They cover
corruption, archive traversal and links, kept partial files, tampering with frozen inputs,
commit snapshots, ARM result checks, the component manifest, the host package lock, the
build-host recipe and evidence bundles. The committed component manifest is checked against the
checkout, so a vendored change without a manifest update fails. The tests build no toolchain and
contact no device.

### Emulated application contracts

After a build, install `qemu-user`, `openssl` and `mosquitto` on the host and run:

```sh
python3 tools/system/run_arm_contracts.py --work "$HOME/awtrix-armv7-build"
```

The runner checks the frozen inputs and artifact hashes, then runs the real ARM `awtrix-linux`
and `awtrix-update-verify` under QEMU (`--qemu`, default `qemu-arm`). It runs the application,
HTTPS and MQTT security, HTTP header limit and update package tests, including an ARM-built
filesystem fault library, which is preloaded inside the emulated process only. All network
fixtures are required. The emulation sysroot must match the verified rootfs archive before and
after the tests (only Buildroot's target-directory warning file is excluded).

Results go to `arm-contracts.json`, which binds artifact hashes, test-source hashes, emulator,
compiler and fault fixture. A rerun sets the status to running or failed until every suite
passes. This records a local test run; it is not an attestation of the host or emulator, and
emulation is not a hardware test.

## Component manifest

[`tools/system/components.json`](https://github.com/Blueforcer/awtrix-ng/blob/main/tools/system/components.json)
is the reviewed record of every vendored or bundled application component: Berry, PubSubClient,
TJpg_Decoder, TJpgDec, cpp-httplib, densaugeo/base64, the ESP-IDF dynamic mbedTLS buffers and the
Matrix-Fonts glyph tables. Each entry records:

- name, version, origin URL and upstream reference (or a note when the commit is unknown);
- whether it is vendored into the repository or bundled from a package source, and its paths;
- every local patch and the files it touches;
- its license file under `LICENSES/` with SHA-256, and its row in `THIRD-PARTY-NOTICES.md`;
- the targets that compile it (`esp32`, `linux`);
- a dated review decision with a rationale (verifiable facts, not a legal opinion).

Vendored entries carry `files_sha256`, a digest over the raw bytes of every file below their
paths, so any change to a vendored tree needs a new review. An entry may name a `patch_marker`;
every file that contains the marker must be listed as a patch file. An unmarked edit is caught
only by `files_sha256`, and the reviewer who records the new digest must list it as a patch.
Rows of the notices tables outside this scope (framework, toolchain, the ESP32 libraries
PlatformIO resolves, the Unity test framework) are listed as `notice_exclusions` with a reason,
so every row is claimed exactly once.

```sh
python3 tools/system/check_components.py
python3 tools/system/check_components.py --print-files-sha256 berry
```

The check validates the schema, hashes the vendored trees, enforces the patch-marker rule, parses
the notices tables in both directions, checks that `LICENSES/` contains exactly the linked files,
verifies every license hash and compares the license files of Linux-target components with
`AWTRIX_NG_LICENSE_FILES` in the ARM recipe. Every table in `THIRD-PARTY-NOTICES.md` needs a
header separator, and every body line up to the next blank line must be a row starting with the
component link.

Digests are computed from raw bytes, so a checkout that converts line endings gives different
values; when the only difference is CRLF, the check says so. Such a checkout ignores
`.gitattributes`; restore LF files from the repository before checking.

To add or update a component, change together: the vendored tree, `LICENSES/`,
`THIRD-PARTY-NOTICES.md` and, for Linux targets, `awtrix-ng.mk` and `awtrix-ng.hash`. Then
recompute the digest, record the review decision with today's date and run the check. Paste a
printed digest only after reviewing the change it stands for.

## Qualification report

After `build` and `legal-info`, run on the same work directory:

```sh
python3 tools/system/qualify.py --work "$HOME/awtrix-armv7-build"
```

It verifies the frozen sources, build-result identity, resolved configuration, both artifact
hashes and sizes, the ARM ELF header and the checksums of the legal material. It evaluates
Buildroot's `show-info` against the frozen recipes and checks the legal manifests against that
metadata. It also verifies `components.json` inside the frozen application tree. It downloads
nothing and touches no hardware.

Output goes to `output/qualification/` unless `--output` says otherwise:

| File | Content |
|---|---|
| `inventory.cdx.json` | CycloneDX 1.6 inventory: configured source packages, build dependencies, the two verified output files, and the Linux-target application components nested under the `awtrix-ng` entry |
| `show-info.json` | Buildroot's original package metadata |
| `qualification.json` | Integrity results, hashes, application components (`component_manifest_sha256`, `application_components`), vulnerability-triage status, review states and public-release blockers |
| `evidence-template.json` | Review record format bound to the exact inputs and artifact hashes |
| `triage-template.json` | Vulnerability-triage record format bound to the component manifest hash |

The inventory keeps Buildroot's license wording instead of guessing SPDX expressions, marks host
and target package roles, and does not list every installed file. Its Linux-header entry is not
an installed-kernel declaration. See the
[CycloneDX 1.6 specification](https://github.com/CycloneDX/specification/tree/1.6).

### Vulnerability triage

`vulnerability_triage` is `not_performed` unless `--triage FILE` supplies a record: a JSON file
whose `subject` carries the manifest hash, whose `data_source` names the advisory source,
reference and retrieval date, whose `reviewer` is non-empty, and whose `components` map covers
every Linux-target component with a status (`no_known_advisories`, `not_affected`, `affected`,
`under_investigation` or `fixed_locally`) and a note. A triage covers only these application
components, not Buildroot packages. The tool never emits a CycloneDX `vulnerabilities` array.

### Review records

To record a review, copy `evidence-template.json` to a separate directory and pass its path with
`--evidence`. Keep `schema_version` and `subject` unchanged. Every named check needs a `state` of
`unmet` or `passed`, a non-empty `note` and a `files` array. A passed check also needs a
non-empty `reviewer` and at least one file record with a relative `path` and its `sha256`. Paths
are relative to the review file; escaping paths and symlinks are rejected.

```json
{
  "state": "unmet",
  "note": "Physical TC002 recovery testing is still pending.",
  "files": []
}
```

The named checks are `hardware_identity`, `maintained_kernel_boot`, `peripherals`,
`install_without_opening`, `recovery_without_opening`, `power_loss_and_rollback`,
`security_review`, `signed_updates`, `redistribution_review` and `bit_reproducibility`; the
template carries a one-line description of each. Hashed attachments keep their content identity;
they do not authenticate the reviewer.

Exit status: 0 for a valid development inventory, 1 for malformed or inconsistent evidence, 2
with `--public` when public release is blocked. `affected` or `under_investigation` findings and
`pending` component review decisions add blockers. The userspace-only profile always blocks public release
as TC002 firmware, whatever the review records say.

## Reproducible builds

### Build host image

[`tools/system/Dockerfile`](https://github.com/Blueforcer/awtrix-ng/blob/main/tools/system/Dockerfile)
builds the host image in two stages. The Ubuntu 24.04 base is pinned by digest. The final stage
installs the build, emulation and MQTT tools from `tools/system/ubuntu.sources`, whose stanzas
carry a `Snapshot:` timestamp (`20260915T213330Z`), so apt resolves every package from
`snapshot.ubuntu.com` at that point in time.

The base image has no CA bundle and the snapshot service is HTTPS-only. A first stage therefore
installs `ca-certificates` from the live archive (plain HTTP, protected by apt's signature check,
not pinned) and hands over only `/etc/ssl/certs/ca-certificates.crt`; the final stage replaces it
with the snapshot's own `ca-certificates`. The build log shows both hosts, because apt still
fetches the live index files, but every package download comes from the snapshot. The final stage
sets `ubuntu.sources` to mode 0644 before apt runs.

[`tools/system/host-packages.lock`](https://github.com/Blueforcer/awtrix-ng/blob/main/tools/system/host-packages.lock)
is the raw `dpkg-query -W` output of that image (211 entries, amd64). Check an image against it:

```sh
python3 tools/system/reproduce.py --check-host-lock --image awtrix-build-host:snapshot-20260915
```

`--check-host-lock` takes only `--image`. Regenerate the lock only together with a deliberate
change of the snapshot timestamp:

```sh
docker run --rm --network none --read-only --entrypoint /usr/bin/dpkg-query IMAGE -W \
  > tools/system/host-packages.lock
```

The lock proves package identity; the immutable image ID identifies the bytes. If the snapshot
service stops serving the timestamp, the build fails at `apt-get update` instead of taking newer
packages. The lock is amd64-only, so an arm64 Docker host is refused.

### Two clean builds

Build the image once, then reuse its content ID for two clean builds. All package downloads must
already be cached by a preceding successful `build` and `legal-info`:

```sh
docker build -f tools/system/Dockerfile -t awtrix-build-host:snapshot-20260915 tools/system
python3 tools/system/reproduce.py --check-host-lock --image awtrix-build-host:snapshot-20260915
python3 tools/system/reproduce.py --work "$HOME/awtrix-armv7-build" \
  --output "$HOME/awtrix-armv7-reproduction" --image awtrix-build-host:snapshot-20260915 --jobs 4
python3 tools/system/qualify.py --work "$HOME/awtrix-armv7-build"
```

The runner resolves the tag to an immutable image ID and compares the image's package inventory
with the lock before writing anything. It records the inventory, uses the same `/work` path in
both containers, drops capabilities and disables the network during compilation. It copies
source inputs and downloads but no earlier compiler or output. The output directory must be new;
failed attempts are kept. Only two complete, matching builds give a successful
`reproduction.json`, which records the image identity, host packages, runner and verifier hashes,
commands, logs, source identity and artifact hashes. A change to the application or build inputs
needs a new run.

`BR2_REPRODUCIBLE` alone guarantees nothing across different absolute paths; that is why both
builds use `/work`. Keep `bit_reproducibility_tested` false until two complete builds have been
compared.

### Verify a finished run

A completed run can be checked without compiling. Record the original `inputs.json` hash and the
original `runner/build.py` hash when you launch the build, and keep them with the trusted image
ID outside the run directory:

```sh
python3 tools/system/reproduce.py --verify-existing \
  --work "$HOME/awtrix-armv7-build" --output "$HOME/awtrix-armv7-reproduction" \
  --image "$AWTRIX_BUILD_IMAGE_ID" --expected-inputs-sha256 "$AWTRIX_INPUTS_SHA256" \
  --expected-runner-sha256 "$AWTRIX_RUNNER_SHA256"
```

`AWTRIX_BUILD_IMAGE_ID` is the immutable `sha256:...` ID of the build image. The check covers
source identity, both copied input trees, configuration and output hashes, the ARM ELF, image
identity, the frozen runner, the host package inventory and logs. Success writes a separate
`reproduction-verified.json`. Its `host_packages_lock_check` reports whether the recorded
inventory equals the current lock; a difference is reported, not refused, so runs against an
older host stay verifiable. Neither command deletes an earlier failed build directory.

These records trust the local orchestrator and image. A check afterwards verifies retained files
and identities; it cannot prove that an untrusted image really ran the compiler.

## Evidence bundles

`bundle_evidence.py` packs one build's evidence into a single tar with a hashed manifest:

```sh
python3 tools/system/bundle_evidence.py create --work "$HOME/awtrix-armv7-build" \
  --output /var/tmp/awtrix-evidence/<name>.tar [--reproduction DIR] [--logs DIR] [--no-downloads]
python3 tools/system/bundle_evidence.py verify --bundle /var/tmp/awtrix-evidence/<name>.tar
```

**`create`** first verifies the build like `qualify.py` and requires an
`output/qualification/qualification.json` bound to that exact build (inputs hash, commit,
`build_result_sha256`, artifacts and generated file hashes). Every file under
`output/legal-info/` must be covered by that record: its `legal_evidence_sha256` lists the files
in `legal-info.sha256`, which must be the path-sorted rendering of that list, and
`build-inputs/manifest.json` must name exactly `application.tar`, `inputs.json`, the Buildroot
archive and `external.tar` with the build's digests. Any other file is refused as unrecorded.

It then copies `inputs.json`, `build-result.json`, `application.tar`, `output/.config`, both
artifacts, `arm-contracts.json` when present, `output/legal-info/`, `output/qualification/` and,
unless `--no-downloads`, the `downloads/` cache into one uncompressed PAX tar. A `--reproduction`
directory must hold a successful `reproduction.json` for the same inputs; its files go under
`reproduction/`. Files of a `--logs` directory go under `logs/`. Member metadata is fixed and the
order is sorted, so two bundles of the same evidence are byte-identical. If any copied file's
hash differs from what was verified, the `.partial` tar is kept, no manifest is written and
`create` fails with `evidence changed while bundling`. `<name>.tar.manifest.json` records every
member's SHA-256 and size, the bundle hash, missing optional files and the build identity.

Member names must be canonical relative paths, free of forms that Windows resolves to another
file (`:`, trailing `.` or space, 8.3 short names such as `~1`), unique when case is ignored,
and inside the paths `create` writes. `verify` refuses a bundle with any other name, so no member
can extract over another.

**`verify`** streams the tar, rechecks every hash and repeats the checks of `create` that the
bundled files allow: ARM profile, successful build of that commit, ARM 32-bit little-endian ELF,
digests of `application.tar`, `output/.config` and (when included) the Buildroot archive,
qualification and legal-info bindings, and reproduction bindings. A change to any digest-bound
file fails even when the manifest was rehashed. `qualification.json`,
`build-inputs/manifest.json` and `reproduction.json` are checked for their binding fields only,
so a consistent rewrite of these together with the manifest is not detected. The other
downloads, reproduction files and logs are bound only by the manifest.

Outputs under `/tmp`, `/dev/shm`, the interpreter's temporary directory, inside a source
directory or over an existing file (including leftover `.partial` names) are refused, as are
symlinked sources and symlinks inside evidence trees. A bundle records the bytes that were kept;
it does not prove that a process ran.

## CI

The `Experimental ARM userspace` workflow
([`.github/workflows/system-build.yml`](https://github.com/Blueforcer/awtrix-ng/blob/main/.github/workflows/system-build.yml))
runs on pull requests that change `firmware/buildroot/`, `tools/system/` or the workflow, and on
manual start. It runs the offline tests, cross-builds, runs the emulated contracts and uploads a
development bundle named `arm-userspace-development-not-tc002-firmware`: a compressed tar of
`build-result.json`, `inputs.json`, `arm-contracts.json`, `output/.config`,
`output/images/rootfs.tar`, `output/qualification/` and `output/legal-info/`. It publishes no
firmware release.

## Related

- [Buildroot configuration](buildroot.md)
- [AWTRIX on Linux](index.md)
- [AWTRIX NG on the TC002](../tc002/index.md) – toolchains and release bundles
