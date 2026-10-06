---
only: [tc002]
---

# Update packages

AWTRIX NG on Linux updates through `.awup` update **containers**. On the Ulanzi TC002 they carry
the web update: the target is `awtrix-ng:tc002` and the payload is a release image. How the
runtime receives the package and how the supervisor installs it is described in
[Web update](../tc002/index.md#web-update) in the TC002 developer guide. This page covers the parts that are
generic Linux code and can be tested on any Linux host: the container, the verifier, the staging
transaction and the update state policy.

| Part | Where |
|---|---|
| Container format and verifier | [`src/platform/tc002/update/`](https://github.com/Blueforcer/awtrix-ng/tree/main/src/platform/tc002/update) (`PackageFormat`, `PackageVerifier`, `verify_main.cpp` for the `awtrix-update-verify` tool) |
| Packaging tool | [`tools/update/package.py`](https://github.com/Blueforcer/awtrix-ng/blob/main/tools/update/package.py) |
| Update state policy | [`src/platform/tc002/update/UpdateState.h`](https://github.com/Blueforcer/awtrix-ng/blob/main/src/platform/tc002/update/UpdateState.h) |
| Tests | [`tests/update/`](https://github.com/Blueforcer/awtrix-ng/tree/main/tests/update) |

Packages are **not signed**. SHA-256 detects corruption; it does not prove who made the package.
Users choose a download source they trust.

## How it works

1. `package.py` wraps a payload into an `AWUPD003` container with a target, a release name and a
   counter.
2. The verifier checks the container against an expected target and the currently accepted
   counter, and can copy the verified bytes into a private staging directory.
3. The update state policy records the staged candidate and moves it through activation, boot
   and confirmation or rollback. Only confirmation raises the accepted counter.

For targets other than `awtrix-ng:tc002`, nothing is installed: `awtrix-update-verify` only
verifies and stages. Such targets must start with `experimental:`; `experimental:test-fixture`
below is a test identifier.

## Container format

The container, `AWUPD003`, is specified field by field under
[Package](../tc002/index.md#package) in the TC002 developer guide. In short: a big-endian 68-byte header
(`>8sHHIQQHH32s` in Python `struct` notation), then the target and the release name, which
complete the manifest; then the manifest's 32-byte SHA-256; then the payload. The payload's
SHA-256 is inside the manifest.

| Limit | Value |
|---|---|
| Payload | at most 256 MiB; `--max-payload-bytes` lowers it |
| Manifest | at most 260 bytes |
| Hashing | in 64 KiB blocks |
| Payload of `awtrix-ng:tc002` | must be a release image |
| Payload of `experimental:` targets | any bytes |
| `AWUPD001`, `AWUPD002` | rejected |

## Integrity and freshness

The caller supplies the expected target and the currently accepted counter. A package passes when
it matches the target, both hashes are valid, and its counter is greater than the supplied value.
The maximum counter is `2^64 - 1`; there is no wraparound. Anyone can compute these hashes, so
they do not protect against a malicious replacement by whoever controls the download source.

**The verifier checks the counter it is given and stores none.** The accepted counter lives in
the [update state policy](#update-state-policy), which advances it only when a candidate reaches
`confirmed`. Running `awtrix-update-verify` by hand with a counter of 0 admits any older valid
package. There is no hardware-backed anti-rollback, expiry, revocation list or repository
freshness check.

Packages cannot choose output paths, algorithms or extraction commands. The parser checks all
bounds before it allocates payload-sized memory. Payload contents are opaque bytes; even an
archive is never extracted or executed by the verifier.

## Staging transaction

The verifier opens one regular input file with `O_NOFOLLOW`, validates the manifest and its
SHA-256, then hashes and copies the payload in the same pass. It never verifies one path and
reopens it later, so a changing source cannot slip unverified bytes into the staged copy.

The staging directory must exist, belong to the calling user and have mode 0700. It is opened
once with `O_DIRECTORY | O_NOFOLLOW`; all later operations use that descriptor. Use trusted
parent directories and a filesystem that supports hard links and file and directory `fsync`.

1. Create a random `.pending-*` file with `O_EXCL | O_NOFOLLOW`, mode 0600.
2. Copy the manifest, its SHA-256 and exactly the hashed payload.
3. Confirm end of file and the payload digest, then `fsync` the file.
4. Publish a hard link named `<counter>-<payload-sha256>.awup`. `linkat` fails if anything
   already has that name, including a symlink.
5. `fsync` the directory, remove the temporary name, and `fsync` again.

The final name appears only after all checks pass, and success is reported only after the
durability steps succeed. A crash can leave a `.pending-*` file, which is never treated as a
package; remove such files when no updater runs.

A directory-sync failure after publication reports failure and can leave a fully verified file
whose survival through power loss is uncertain. Do not read file existence or a failed request as
a commit; verify again and reconcile.

## Build and test

The verifier needs C++17 and OpenSSL's crypto library. `package.py` needs Python 3 and its
standard library only; packaging needs no key and no `openssl` command.

```sh
cmake -S . -B /var/tmp/awtrix-update-tests -DCMAKE_BUILD_TYPE=Debug
cmake --build /var/tmp/awtrix-update-tests -j2 --target awtrix-update-verify update-state-policy-test awtrix_update_test_faults
ctest --test-dir /var/tmp/awtrix-update-tests --output-on-failure -R 'update-package-contract|update-state-policy'
```

`update-package-contract` covers manifest and payload integrity, corrupted metadata and payloads,
target mismatch, counters, integer bounds, truncation, trailing bytes, unsafe filesystem objects,
write failures, process death, and file and directory `fsync` failures. Its preload library
`tests/update/test_faults.cpp` injects those faults and is only a test fixture.

`update-state-policy` runs `tests/update/test_state.cpp` against `UpdateState`: transitions,
lease rules, fallback authorization, the counter maximum, corrupt documents, storage faults at
the read before a write and at every write step, and stale objects sharing one store.

### Example: create and verify a test package

Use a private directory on a Linux filesystem outside the repository. A Windows-mounted directory
may not provide the required Unix permissions.

```sh
install -d -m 0700 /var/tmp/awtrix-update-example
printf 'example payload\n' > /var/tmp/awtrix-update-example/payload.bin

python3 tools/update/package.py \
  --payload /var/tmp/awtrix-update-example/payload.bin \
  --output /var/tmp/awtrix-update-example/release.awup \
  --target experimental:test-fixture --release 0.1.0-test --counter 1

/var/tmp/awtrix-update-tests/awtrix-update-verify \
  --package /var/tmp/awtrix-update-example/release.awup \
  --target experimental:test-fixture --current-counter 0

install -d -m 0700 /var/tmp/awtrix-update-example/staged
/var/tmp/awtrix-update-tests/awtrix-update-verify \
  --package /var/tmp/awtrix-update-example/release.awup \
  --target experimental:test-fixture --current-counter 0 \
  --stage-dir /var/tmp/awtrix-update-example/staged
```

The verifier prints a JSON result and exits 0 on success, 1 on a verification or storage
failure, and 2 on invalid arguments. Successful staging reports the complete `.awup` file.
Existing output files are never replaced.

For TC002 releases, `package.py` takes `--image FILE` (a release image built before) or
`--release-dir DIR` (it builds the image itself) instead of `--payload`.

## Update state policy

`UpdateState` is a host-tested policy module. It consumes the verifier's `Result` and never
parses packages again. It writes no installation or activation data, decides nothing about boot
health and has no clock: every operation takes the caller's time in seconds. On the TC002 the
supervisor drives it through `UpdateRecord`
([`src/platform/tc002/daemon/update/`](https://github.com/Blueforcer/awtrix-ng/tree/main/src/platform/tc002/daemon/update)),
and a USB `deploy` writes the state file directly.

### States

| State | Meaning |
|---|---|
| `idle` | No candidate: newly initialized, or a staged candidate was discarded. |
| `staged` | A verified candidate is recorded; nothing is activated. |
| `activating` | The installer is applying the candidate. It exists only in the object that called `activate()`; on disk it counts as interrupted. |
| `boot-pending` | Activation finished; the candidate boots next and waits for its boot decision. |
| `confirmed` | The last candidate booted successfully and is the current release. |
| `rolled-back` | The last candidate was abandoned; the previous release stays current. |
| `quarantined` | An activation was found interrupted on load; only `rollback` leaves it. |

`idle`, `confirmed` and `rolled-back` are quiet states: they carry no candidate and differ only
in how the previous cycle ended. A new cycle can start from any of them.

### Operations

| Operation | From | To | Lease | Guards |
|---|---|---|---|---|
| `initialize(counter)` | no state file | `idle` | no | Refused when the file exists (`already-initialized`). Records the accepted counter the operator chose. |
| `acquireLease(owner, now, ttl)` | any | same | – | Owner 1–64 bytes of `A-Z a-z 0-9 . _ - :`; ttl 1–86400 s; `lease-held` while another owner's lease is live; the same owner renews; an expired lease is taken over. |
| `releaseLease(owner, now)` | any | same | – | The holder, or anyone once the lease has expired. |
| `stage(owner, now, result[, authorization])` | `idle`, `confirmed`, `rolled-back` | `staged` | yes | `result.ok`; counter, payload hash, release and target valid; staged file path 1–4096 bytes. Counter equal to the current release: `duplicate`. Without authorization: accepted counter at `2^64 - 1`: `counter-maximum`; counter at or below the accepted counter: `downgrade`. With authorization: see [Fallback authorization](#fallback-authorization). Any other state: `illegal-transition`, including a replay of the same package. |
| `discard(owner, now)` | `staged` | `idle` | yes | Clears the candidate; accepted counter and current release unchanged. |
| `activate(owner, now, reverified)` | `staged` | `activating` | yes | The re-verified result must equal the candidate in counter, payload hash, release and target; otherwise `invalid-input`. |
| `markBootPending(owner, now)` | `activating` | `boot-pending` | yes | – |
| `confirm()` | `boot-pending` | `confirmed` | no | Current release := candidate; accepted counter := max(accepted, candidate); candidate, lease and failure cleared. |
| `rollback(reason)` | `activating`, `boot-pending`, `quarantined` | `rolled-back` | no | Reason 1–256 printable ASCII bytes, recorded as `failure`; current release and accepted counter unchanged; candidate and lease cleared. |
| `load()` | `activating` on disk | `quarantined` in memory | – | `failure` becomes `activation interrupted`; all other states load unchanged; nothing is written. |

Every operation returns an `Outcome` with a stable code: `not-loaded`, `uninitialized`,
`already-initialized`, `invalid-input`, `illegal-transition`, `lease-required`, `lease-held`,
`lease-expired`, `duplicate`, `downgrade`, `counter-maximum`, `authorization-mismatch`, `stale`,
`storage` or `corrupt`.

A changing operation checks its inputs, the lease and the transition against its snapshot and
changes a copy. It then reads the document again and writes the copy only when the store still
holds, byte for byte, the document this object last read or wrote. Anything else, including a
missing file, is refused with `stale` and nothing is written. After `stale`, or after `storage`
from a failed read or write, the object is not loaded: `snapshot()` keeps the earlier view, and
every operation except `load()` and `initialize()` answers `not-loaded` until one of them
succeeds.

### When a counter becomes accepted

Only `confirm()` changes the accepted counter, and only upwards. Staging, activation and
boot-pending leave it alone, so a failure anywhere before confirmation leaves the same candidate
stageable again and a lower one refused, as long as the state file loads. The accepted counter
is a high-water mark: it never goes down, not even after a confirmed fallback.

The TC002 runtime accepts the saved counter only from a complete, valid update-state document,
including its schema, state relationships and unique member names. If that document cannot be
read or validated, it logs the failure and uses the running release counter. The supervisor still
validates the state before installing an uploaded package.

The caller passes `snapshot().acceptedCounter` to the verifier as the current counter (or the
fallback counter minus one for an authorized fallback) and hands the result to `stage()`. On the
TC002 the runtime verifies and the supervisor stages what the runtime handed over.

### Fallback authorization

A package whose counter is at or below the accepted counter stages only with a
`FallbackAuthorization` that names its exact counter and payload hash; it travels with the
`stage()` request. An authorization whose counter is above the accepted counter, whose hash
differs from the package, or that names the current release is refused. The candidate is recorded
with `fallback: true`.

When a fallback is confirmed, the lower release becomes current while the accepted counter keeps
its high-water value. From then on, the higher release and every counter in between stage only
with an authorization naming their exact counter and hash. The policy keeps no blocklist and does
not remember why a fallback happened, so an authorization re-admits that release.

A counter equal to the accepted counter counts as "at or below". After `initialize(counter)`
without a recorded current release, or after a confirmed fallback, the release with that counter
stages with an authorization. A package with the current release's counter is always refused as
`duplicate`, whatever its hash and even with an authorization: a second build under a confirmed
counter cannot be staged, and repairing the running release is outside this policy.

### Lease

`stage`, `discard`, `activate` and `markBootPending` need a lease held by the caller. The lease
records the owner and its expiry in caller-supplied seconds and lasts 1 s to 24 h. The owner
string is not authenticated: before expiry, any caller passing the same owner string can renew or
release it.

`flock` on the state directory admits one store per directory, so processes on one host are
serialized. Several `UpdateState` objects in one process may share that store; they are not
synchronized, so whole operations run one at a time on one thread. The document comparison
refuses writes from an object whose document another object has replaced (`stale`). A forked
child inherits the lock and must not use the parent's store; a process that writes the file
without the store is not excluded.

After a reboot without trusted time, a leftover lease refuses every other owner with
`lease-held`, even past its expiry, until `acquireLease` takes it over once the caller's clock
has passed the expiry. Before then only its owner string, `confirm` or `rollback` clears it.
`confirm` and `rollback` need no lease and clear it, because they run after a boot when the
installer that held it may be gone.

### Reconciliation on load

`load()` reads, parses and reconciles in memory only. A stored `activating` becomes
`quarantined` with the failure `activation interrupted`; the next successful write records that.
`quarantined` leaves only through `rollback(reason)`; lease operations stay possible so an
operator can take over. All other states load unchanged. A missing file is reported as
`uninitialized`, never treated as counter 0: creating the file is an explicit
`initialize(counter)` with a counter the operator chose.

### State file

The state lives in a private directory (mode 0700, owned by the service user) outside `--data`,
because a factory reset empties `--data` and would erase the accepted counter. The document is
`update-state.json`, mode 0600, one line, schema 2:

```json
{"schema":2,"state":"staged","acceptedCounter":5,
 "current":{"counter":5,"payloadSha256":"<64 hex>","release":"1.0.0","target":"experimental:test-fixture"},
 "candidate":{"counter":6,"payloadSha256":"<64 hex>","release":"1.1.0","target":"experimental:test-fixture","stagedFile":"/var/lib/awtrix/stage/6-<sha>.awup","fallback":false},
 "lease":{"owner":"installer","expiresAt":1758000000},
 "failure":""}
```

| Member | Type | Validation |
|---|---|---|
| `schema` | integer | 2 for new writes; schema 1 is read for migration |
| `state` | string | one of the seven state names |
| `acceptedCounter` | unsigned integer | decimal without sign, fraction, exponent or leading zero; at most `2^64 - 1` |
| `current` | object or `null` | `counter` 1 to `2^64 - 1` and at most `acceptedCounter`; `payloadSha256` 64 lowercase hex characters; `release` a valid release name (`validReleaseName` in `src/platform/tc002/contract/ReleaseName.h`); `target` `awtrix-ng:tc002` or an `experimental:` identifier; required in `confirmed` |
| `candidate` | object or `null` | the same fields plus `stagedFile` (1–4096 bytes, no NUL) and `fallback` (boolean); present exactly in `staged`, `activating`, `boot-pending` and `quarantined`; counter above `acceptedCounter` when `fallback` is false, at or below it when true, and different from the current counter |
| `lease` | object or `null` | `owner` as for the operations; `expiresAt` unsigned integer |
| `failure` | string | empty or 1–256 printable ASCII bytes |

Parsing rules:

- Unknown members are ignored at every level; every known member is required.
- Schema 1 files are read for migration; their `keyId` must be 64 lowercase hex characters and
  is then dropped. Other schema versions are refused.
- Member names compare after unescaping, so `"counter"` is `counter`.
- A name repeated in the same object is a violation anywhere in the document, including inside
  unknown members and arrays.
- A value nested inside more than 15 objects or arrays (the root counts as one) makes the
  document malformed.
- Any violation makes `load()` fail with `corrupt`; nothing is rewritten.

**Writing.** A write removes a leftover `.update-state.tmp` (so a stale symlink cannot redirect
it), creates that name with `O_EXCL` and mode 0600, writes, syncs, closes, renames it over
`update-state.json` and syncs the directory. A failure before the rename leaves the previous
document. A failed directory sync returns `storage` with an error containing "published but
durability is uncertain" and leaves the new document visible.

**Reading.** Reads refuse symlinks, non-regular files, files with any group or other permission
bit, files owned by someone else and files larger than 64 KiB, and never change them. The store
holds `flock` on the directory for its lifetime; a second instance is refused with "state
directory is in use".

**Directory.** The directory is opened once with `O_DIRECTORY | O_NOFOLLOW` and used through
that descriptor. `O_NOFOLLOW` protects only the last path component, so the store drops trailing
slashes and refuses a last component of `.` or `..`. Symlinks in parent components are followed.
Every ancestor must be owned by root or the service user and not be writable by group or others;
the store does not check this.

**Recovery** from a corrupt file is manual: remove the file and call `initialize(counter)` with
a counter from a trusted record. The policy never touches package files; the caller removes
staged containers. Another process with the same user rights can change the document; protecting
against a compromised updater account is outside this component.

### Failure states

| Situation | Meaning | Way out |
|---|---|---|
| `quarantined` after load | The document says `activating` (the installer stopped between `activate` and `markBootPending`) or `quarantined` (a later write stored the quarantine). What the installer wrote is unknown. | Find out which release runs by other means, then `rollback(reason)`. A new cycle starts from `rolled-back`. |
| `rolled-back` | The last candidate was abandoned with a recorded reason; the previous release stays current and the accepted counter is unchanged. | Stage the next candidate; the same candidate can be staged again. |
| `storage` | Reading or writing the state file failed. A failed read writes nothing. A write failure before the rename leaves the previous document; with "durability is uncertain" the new document is published but may not survive power loss. | Everything except `load()` and `initialize()` answers `not-loaded`; call `load()` and continue from what it reports. |
| `stale` | The file is not the document this object last read or wrote: another object wrote since, or the file was replaced or removed outside the store. Nothing was written. | Call `load()` and decide again; the refused operation may not be legal in the new state. A removed file loads as `uninitialized`. |
| "state directory is in use" | Another instance holds the directory. | Wait for it or stop it; the lock ends with the process. |
| `lease-held` or `lease-expired` | Another owner holds the lease, live or expired, or the caller's own lease ran out. | Wait for expiry and take over with `acquireLease`, renew before expiry, or release as the owner. |
| `uninitialized` | No state file exists. | `initialize(counter)` with an operator-chosen counter; never assume 0. |
| `corrupt` | The document breaks the schema or a consistency rule. | Remove the file and initialize with a counter from a trusted record. |

## Release delivery

Publish the complete `.awup` file from the intended release source and keep checksums next to the
download. Neither a checksum nor the package format proves authorship. There are no signing keys
to provision, back up or rotate.

A TC002 whose updater predates `AWUPD003` needs a one-time USB installation: `build-res` and
`flash-loader` write the loader and the release slot (see
[TC002 install tools](../tc002/install-tools.md)).

The counter prevents accidental downgrades through the web update but cannot force an upgrade.
For a broken release, publish a corrected package with a higher counter. USB recovery and the
fallback authorization above are separate from the normal web update.

## Configuration migration rules

`awtrix-linux` keeps these files under `--data`: `settings.json` (with `schemaVersion` 1),
`device.json`, `apploop.json`, `radio.json`, `SCRIPTS/<name>.ax` with
`SCRIPTS/<name>.store.json`, `identity` and `.lock`.

Current behaviour:

- Unknown members are ignored on load.
- A malformed document is ignored as a whole and defaults apply; missing members take defaults.
- Each file is replaced atomically (temporary file, `fsync`, rename, directory `fsync`). This is
  not a transaction across files.
- Members the running version does not know are not written back.

Rules for any schema change:

- Additive changes only: a member keeps its name, type and meaning; a replacement gets a new
  name.
- A version reads every schema it can be rolled back from and writes its own schema with
  `schemaVersion`.
- The migrating code preserves members it does not understand.
- The migrating code keeps a byte-exact copy of every document it rewrites, outside `--data`,
  until the update reaches `confirmed`, and restores those copies on `rolled-back`.

`test_settings_survive_a_forward_and_backward_version_round_trip` in
[`tests/linux/test_contract.py`](https://github.com/Blueforcer/awtrix-ng/blob/main/tests/linux/test_contract.py)
runs the real binary through this sequence: values written by the current version survive a
forward migration with unknown members, the version's own save keeps only known members, and the
restored copy is byte-identical and loads with the original values.

## Related

- [TC002 web update](../tc002/index.md#web-update)
- [TC002 install tools](../tc002/install-tools.md)
- [AWTRIX on Linux](index.md)
