"""Offline contracts for the component manifest checker; no network, compiler or device."""
from __future__ import annotations

import copy
import hashlib
import io
import json
from pathlib import Path
import re
import tempfile
import unittest
from unittest import mock

import build
import check_components as components


REMOVE = object()


class ManifestFixture(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="awtrix-components-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        self.package = self.root / "firmware/buildroot/package/awtrix-ng"
        self.put("lib/fixture/fixture.c", b"int fixture(void) { return 1; }\n")
        self.put("lib/fixture/patched.c", b"/* AWTRIX: local change */\n")
        self.put("lib/fixture/docs/notes.txt", b"documentation\n")
        self.put("lib/esp/esp.c", b"esp only\n")
        self.put("LICENSES/MIT-fixture.txt", b"MIT fixture license\n")
        self.put("LICENSES/MIT-esp.txt", b"MIT esp license\n")
        self.put("LICENSES/MIT-recipe.txt", b"MIT recipe license\n")
        self.rows = [
            "| [fixture](https://example.invalid/fixture) | `lib/fixture/` | MIT | "
            "[MIT-fixture.txt](LICENSES/MIT-fixture.txt) | fixture author |",
            "| [esp-only](https://example.invalid/esp) (via `pio`) | `lib/esp/` | MIT | "
            "[MIT-esp.txt](LICENSES/MIT-esp.txt) | esp author |",
            "| [recipe-lib](https://example.invalid/recipe) | — | MIT | "
            "[MIT-recipe.txt](LICENSES/MIT-recipe.txt) | recipe author |",
        ]
        self.write_notices()
        self.put("firmware/buildroot/package/awtrix-ng/awtrix-ng.mk",
                 b"AWTRIX_NG_LICENSE_FILES = LICENSE.md THIRD-PARTY-NOTICES.md \\\n"
                 b"\tLICENSES/MIT-fixture.txt LICENSES/MIT-recipe.txt\n")
        self.manifest = {
            "schema_version": 1,
            "scope": "fixture components",
            "notice_exclusions": [],
            "components": [
                self.component("fixture", ["lib/fixture"], "MIT-fixture", ["linux"],
                               patches=[{"summary": "local change", "files": ["lib/fixture/patched.c"]}],
                               patch_marker="AWTRIX"),
                self.component("esp-only", ["lib/esp"], "MIT-esp", ["esp32"]),
                self.component("recipe-lib", [], "MIT-recipe", ["esp32", "linux"]),
            ],
        }
        for component in self.manifest["components"]:
            if component["vendored"]:
                component["files_sha256"] = components.files_digest(components.component_files(self.root, component))

    def put(self, name, data):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        return path

    def write_notices(self):
        text = "# Notices\n\n| Component | Location | License | Text | Copyright |\n|---|---|---|---|---|\n"
        text += "\n".join(self.rows) + "\n"
        return self.put("THIRD-PARTY-NOTICES.md", text.encode("utf-8"))

    def component(self, ident, paths, license_name, targets, **extra):
        vendored = bool(paths)
        value = {
            "id": ident, "name": ident.title(), "version": "1.0",
            "origin": {"url": "https://example.invalid/" + ident.split("-")[0], "reference": "v1.0"},
            "vendored": vendored, "paths": paths, "patches": [],
            "license": {"spdx": "MIT", "name": "MIT", "file": f"LICENSES/{license_name}.txt",
                        "sha256": build.digest(self.root / f"LICENSES/{license_name}.txt")},
            "notice": ident, "targets": targets,
            "review": {"decision": "accepted", "date": "2026-09-16", "rationale": "fixture review"},
        }
        if not vendored:
            value["bundled_from"] = "fixture package"
            value["recipe_package"] = "awtrix-recipe"
        value.update(extra)
        return value

    def by_id(self, ident):
        return next(c for c in self.manifest["components"] if c["id"] == ident)

    def write_manifest(self, manifest=None):
        path = self.root / "tools/system/components.json"
        path.parent.mkdir(parents=True, exist_ok=True)
        build.write_json(path, self.manifest if manifest is None else manifest)
        return path

    def check(self, manifest=None):
        return components.check(self.root, self.manifest if manifest is None else manifest, self.package)

    def require_symlinks(self):
        probe = self.root / "symlink-probe"
        try:
            probe.symlink_to("not-present")
        except OSError as error:
            self.skipTest(f"platform does not permit symlinks: {error}")
        probe.unlink()


class SchemaContracts(ManifestFixture):
    def test_synthetic_root_validates_three_components(self):
        validated = self.check()
        self.assertEqual(["esp-only", "fixture", "recipe-lib"], sorted(validated))
        self.assertEqual(self.by_id("fixture"), validated["fixture"])
        loaded = components.load_manifest(self.write_manifest())
        self.assertEqual(self.manifest, loaded)

    def test_duplicate_json_keys_and_symlinked_manifest_are_rejected(self):
        path = self.put("tools/system/components.json", b'{"schema_version":1,"schema_version":1}')
        with self.assertRaisesRegex(build.BuildError, "duplicate JSON key: schema_version"):
            components.load_manifest(path)
        self.require_symlinks()
        self.write_manifest()
        link = self.root / "link.json"
        link.symlink_to(path)
        with self.assertRaisesRegex(build.BuildError, "symlinked component manifest"):
            components.load_manifest(link)

    def test_top_level_schema_refusals(self):
        cases = {
            "unsupported component manifest schema": [("schema_version", 2)],
            "unknown manifest key: extra": [("extra", True)],
            "missing manifest key: scope": [("scope", REMOVE)],
            "invalid manifest scope": [("scope", "")],
            "invalid notice exclusions": [("notice_exclusions", {})],
            "invalid notice exclusion": [("notice_exclusions", [{"notice": "x"}])],
            "exclusion without reason: x": [("notice_exclusions", [{"notice": "x", "reason": " "}])],
            "no components listed": [("components", [])],
            "invalid component entry": [("components", ["text"])],
        }
        for message, changes in cases.items():
            with self.subTest(message=message):
                manifest = copy.deepcopy(self.manifest)
                for key, value in changes:
                    if value is REMOVE:
                        manifest.pop(key)
                    else:
                        manifest[key] = value
                with self.assertRaisesRegex(build.BuildError, message):
                    components.validate_manifest(manifest)

    def test_component_schema_refusals(self):
        cases = {
            "invalid component id: 'Fixture'": {"id": "Fixture"},
            "invalid component id: '-x'": {"id": "-x"},
            "invalid component id: 'a{}'".format("b" * 40): {"id": "a" + "b" * 40},
            "unknown component key: fixture: extra": {"extra": 1},
            "missing component key: fixture: review": {"review": REMOVE},
            "invalid component name: fixture": {"name": ""},
            "invalid component version: fixture": {"version": 1},
            "invalid component targets: fixture": {"targets": []},
            "invalid component targets: fixture (unknown)": {"targets": ["windows"]},
            "invalid component targets: fixture (dup)": {"targets": ["linux", "linux"]},
            "invalid component targets: fixture (type)": {"targets": "linux"},
            "invalid review decision: fixture": {"review": {"decision": "rejected", "date": "2026-09-16", "rationale": "x"}},
            "invalid review date: fixture": {"review": {"decision": "accepted", "date": "16.09.2026", "rationale": "x"}},
            "invalid review date: fixture (day)": {"review": {"decision": "accepted", "date": "2026-02-31", "rationale": "x"}},
            "missing review rationale: fixture": {"review": {"decision": "accepted", "date": "2026-09-16", "rationale": ""}},
            "invalid component review: fixture": {"review": {"decision": "accepted", "date": "2026-09-16", "rationale": "x", "extra": 1}},
            "origin note required when version or reference is null: fixture (version)": {"version": None},
            "origin note required when version or reference is null: fixture (reference)":
                {"origin": {"url": "https://example.invalid/fixture", "reference": None}},
            "invalid origin URL: fixture": {"origin": {"url": "ftp://example.invalid/x", "reference": "v1"}},
            "invalid origin URL: fixture (space)": {"origin": {"url": "https://example.invalid/a b", "reference": "v1"}},
            "invalid component origin: fixture": {"origin": {"url": "https://example.invalid/x"}},
            "invalid component notice: fixture": {"notice": ""},
            "invalid component license: fixture": {"license": {"spdx": "MIT", "name": "MIT"}},
            "invalid license file path: fixture (outside)": {"license": {"spdx": "MIT", "name": "MIT", "file": "LICENSE.md", "sha256": "a" * 64}},
            "invalid license file path: fixture (nested)": {"license": {"spdx": "MIT", "name": "MIT", "file": "LICENSES/sub/x.txt", "sha256": "a" * 64}},
            "invalid license file path: fixture (traversal)": {"license": {"spdx": "MIT", "name": "MIT", "file": "LICENSES/../x.txt", "sha256": "a" * 64}},
            "invalid license sha256: fixture": {"license": {"spdx": "MIT", "name": "MIT", "file": "LICENSES/x.txt", "sha256": "A" * 64}},
            "invalid patch entry: fixture": {"patches": [{"summary": "", "files": ["lib/fixture/patched.c"]}]},
            "invalid patch entry: fixture (files)": {"patches": [{"summary": "x", "files": []}]},
            "invalid patch entry: fixture (path)": {"patches": [{"summary": "x", "files": ["../x"]}]},
            "invalid patch_marker: fixture": {"patch_marker": ""},
            "invalid files_sha256: fixture": {"files_sha256": "z" * 64},
            "invalid vendored flag: fixture": {"vendored": "yes"},
        }
        for message, changes in cases.items():
            with self.subTest(message=message):
                manifest = copy.deepcopy(self.manifest)
                component = next(c for c in manifest["components"] if c["id"] == "fixture")
                for key, value in changes.items():
                    if value is REMOVE:
                        component.pop(key)
                    else:
                        component[key] = value
                with self.assertRaisesRegex(build.BuildError, message.split(" (")[0]):
                    components.validate_manifest(manifest)
        manifest = copy.deepcopy(self.manifest)
        manifest["components"].append(copy.deepcopy(self.by_id("fixture")))
        with self.assertRaisesRegex(build.BuildError, "duplicate component id: fixture"):
            components.validate_manifest(manifest)

    def test_path_syntax_is_rejected_before_any_file_is_read(self):
        for invalid in ("/lib/fixture", "lib/../fixture", "lib\\fixture", "C:lib", "lib/fixture/", "./lib/fixture",
                        "lib//fixture", ".", "", "lib/./fixture"):
            with self.subTest(path=invalid):
                manifest = copy.deepcopy(self.manifest)
                next(c for c in manifest["components"] if c["id"] == "fixture")["paths"] = [invalid]
                with mock.patch.object(components, "component_path", side_effect=AssertionError("file access")), \
                        mock.patch.object(components.build, "digest", side_effect=AssertionError("file access")):
                    with self.assertRaisesRegex(build.BuildError, "invalid component path: fixture"):
                        components.check(self.root, manifest, self.package)

    def test_overlapping_paths_across_components_are_rejected(self):
        for paths in (["lib/fixture"], ["lib"], ["lib/fixture/docs"]):
            with self.subTest(paths=paths):
                manifest = copy.deepcopy(self.manifest)
                next(c for c in manifest["components"] if c["id"] == "esp-only")["paths"] = paths
                with self.assertRaisesRegex(build.BuildError, "overlapping component paths: "):
                    components.validate_manifest(manifest)
        manifest = copy.deepcopy(self.manifest)
        next(c for c in manifest["components"] if c["id"] == "fixture")["paths"] = ["lib/fixture", "lib/fixture/docs"]
        with self.assertRaisesRegex(build.BuildError, "overlapping component paths: fixture fixture"):
            components.validate_manifest(manifest)

    def test_vendored_and_bundled_consistency(self):
        cases = {
            "vendored component needs paths: fixture": ("fixture", {"paths": []}),
            "paths not allowed for non-vendored component: recipe-lib": ("recipe-lib", {"paths": ["lib/esp"]}),
            "bundled_from required for non-vendored component: recipe-lib": ("recipe-lib", {"bundled_from": REMOVE}),
            "bundled_from not allowed for vendored component: fixture": ("fixture", {"bundled_from": "x"}),
            "recipe_package only for non-vendored component: fixture": ("fixture", {"recipe_package": "x"}),
            "files_sha256 only for vendored component: recipe-lib": ("recipe-lib", {"files_sha256": "a" * 64}),
            "vendored component needs files_sha256: fixture": ("fixture", {"files_sha256": REMOVE}),
            "patch_marker only for vendored component: recipe-lib": ("recipe-lib", {"patch_marker": "x"}),
        }
        for message, (ident, changes) in cases.items():
            with self.subTest(message=message):
                manifest = copy.deepcopy(self.manifest)
                component = next(c for c in manifest["components"] if c["id"] == ident)
                for key, value in changes.items():
                    if value is REMOVE:
                        component.pop(key)
                    else:
                        component[key] = value
                with self.assertRaisesRegex(build.BuildError, message):
                    components.validate_manifest(manifest)


class FileContracts(ManifestFixture):
    def test_missing_path_and_path_without_files_are_rejected(self):
        self.by_id("esp-only")["paths"] = ["lib/missing"]
        with self.assertRaisesRegex(build.BuildError, "missing component path: lib/missing"):
            self.check()
        (self.root / "lib/missing").mkdir()
        with self.assertRaisesRegex(build.BuildError, "component path contains no files: esp-only: lib/missing"):
            self.check()

    def test_symlink_anywhere_below_a_component_path_is_rejected(self):
        self.require_symlinks()
        outside = self.put("outside.c", b"outside")
        link = self.root / "lib/fixture/link.c"
        link.symlink_to(outside)
        with self.assertRaisesRegex(build.BuildError, "symlink in component path: lib/fixture/link.c"):
            self.check()
        link.unlink()
        (self.root / "lib/esp").rename(self.root / "lib/esp-real")
        (self.root / "lib/esp").symlink_to(self.root / "lib/esp-real", target_is_directory=True)
        with self.assertRaisesRegex(build.BuildError, "symlink in component path: lib/esp"):
            self.check()

    def test_single_byte_change_fails_without_printing_the_digest(self):
        self.put("lib/fixture/fixture.c", b"int fixture(void) { return 2; }\n")
        with self.assertRaises(build.BuildError) as raised:
            self.check()
        message = str(raised.exception)
        self.assertEqual("vendored files differ from manifest: fixture", message)
        self.assertNotIn(components.files_digest(components.component_files(self.root, self.by_id("fixture"))), message)

    def test_crlf_only_difference_names_the_line_endings(self):
        self.put("lib/fixture/fixture.c", b"int fixture(void) { return 1; }\r\n")
        with self.assertRaises(build.BuildError) as raised:
            self.check()
        self.assertEqual("vendored files differ from manifest only by CRLF line endings: fixture "
                         "(the checkout is not normalized to LF as .gitattributes requests)", str(raised.exception))

    def test_content_change_with_crlf_reports_a_difference(self):
        self.put("lib/fixture/fixture.c", b"int fixture(void) { return 2; }\r\n")
        with self.assertRaises(build.BuildError) as raised:
            self.check()
        self.assertEqual("vendored files differ from manifest: fixture", str(raised.exception))

    def test_files_are_hashed_as_raw_bytes(self):
        data = "// Crème ©\r\nint esp(void);\r\n".encode("utf-8")
        self.put("lib/esp/esp.c", data)
        files = components.component_files(self.root, self.by_id("esp-only"))
        self.assertEqual({"lib/esp/esp.c": hashlib.sha256(data).hexdigest()}, files)
        expected = hashlib.sha256(json.dumps(files, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
        self.assertEqual(expected, components.files_digest(files))
        self.assertEqual(["lib/fixture/docs/notes.txt", "lib/fixture/fixture.c", "lib/fixture/patched.c"],
                         list(components.component_files(self.root, self.by_id("fixture"))))

    def test_single_file_path_is_hashed_like_a_tree(self):
        self.put("src/vendor/header.h", b"header\n")
        component = self.by_id("esp-only")
        component["paths"] = ["src/vendor/header.h"]
        component["files_sha256"] = components.files_digest({"src/vendor/header.h": build.digest(self.root / "src/vendor/header.h")})
        self.assertIn("esp-only", self.check())

    def test_print_files_sha256_prints_the_digest_validation_accepts(self):
        path = self.write_manifest()
        with mock.patch("sys.stdout", new_callable=io.StringIO) as out:
            self.assertEqual(0, components.main(["--root", str(self.root), "--manifest", str(path),
                                                 "--print-files-sha256", "fixture"]))
        self.assertEqual(self.by_id("fixture")["files_sha256"], out.getvalue().strip())
        for ident in ("recipe-lib", "unknown"):
            with self.subTest(ident=ident), mock.patch("sys.stderr", new_callable=io.StringIO) as err:
                self.assertEqual(1, components.main(["--root", str(self.root), "--manifest", str(path),
                                                     "--print-files-sha256", ident]))
                self.assertIn("components: ", err.getvalue())


class PatchContracts(ManifestFixture):
    def test_patch_files_must_exist_inside_component_paths(self):
        component = self.by_id("fixture")
        component["patches"] = [{"summary": "x", "files": ["lib/esp/esp.c"]}]
        with self.assertRaisesRegex(build.BuildError, "patch file outside component paths: fixture: lib/esp/esp.c"):
            self.check()
        component["patches"] = [{"summary": "x", "files": ["lib/fixture/gone.c"]}]
        with self.assertRaisesRegex(build.BuildError, "missing patch file: fixture: lib/fixture/gone.c"):
            self.check()
        recipe = self.by_id("recipe-lib")
        recipe["patches"] = [{"summary": "x", "files": ["lib/fixture/patched.c"]}]
        component["patches"] = [{"summary": "local change", "files": ["lib/fixture/patched.c"]}]
        with self.assertRaisesRegex(build.BuildError, "patch file outside component paths: recipe-lib"):
            self.check()

    def test_marker_in_unlisted_file_is_an_unlisted_local_modification(self):
        self.put("lib/fixture/docs/notes.txt", b"mentions AWTRIX too\n")
        component = self.by_id("fixture")
        component["files_sha256"] = components.files_digest(components.component_files(self.root, component))
        with self.assertRaisesRegex(build.BuildError, "unlisted local modification: fixture: lib/fixture/docs/notes.txt"):
            self.check()
        component["patches"].append({"summary": "documented", "files": ["lib/fixture/docs/notes.txt", "lib/fixture/fixture.c"]})
        self.check()

    def test_listed_file_without_marker_passes_and_marker_is_raw_bytes(self):
        component = self.by_id("fixture")
        component["patches"] = [{"summary": "both", "files": ["lib/fixture/patched.c", "lib/fixture/fixture.c"]}]
        self.check()
        component["patch_marker"] = "awtrix"
        component["patches"] = []
        self.check()


class NoticeContracts(ManifestFixture):
    HEADER = "| Component | Location | License | Text | Copyright |\n|---|---|---|---|---|\n"

    def test_notice_rows_parse_link_text_url_and_license_file(self):
        rows = components.notice_rows((self.root / "THIRD-PARTY-NOTICES.md").read_text(encoding="utf-8"))
        self.assertEqual({"fixture", "esp-only", "recipe-lib"}, set(rows))
        self.assertEqual({"url": "https://example.invalid/esp", "license_file": "LICENSES/MIT-esp.txt"}, rows["esp-only"])
        with self.assertRaisesRegex(build.BuildError, "duplicate notice row: fixture"):
            components.notice_rows(self.HEADER + "\n".join(self.rows + [self.rows[0]]))
        with self.assertRaisesRegex(build.BuildError, "notice row without license link: fixture"):
            components.notice_rows(self.HEADER + "| [fixture](https://example.invalid/fixture) | x | MIT | none | author |")
        with self.assertRaisesRegex(build.BuildError, "malformed notice row"):
            components.notice_rows(self.HEADER + "| [fixture](https://example.invalid/fixture) | x |")
        with self.assertRaisesRegex(build.BuildError, "malformed notice row"):
            components.notice_rows(self.HEADER + "| [fixture | x | MIT | [a](LICENSES/a.txt) | author |")

    def test_every_table_body_row_needs_a_leading_component_link(self):
        license_cells = " | `lib/x/` | MIT | [MIT-fixture.txt](LICENSES/MIT-fixture.txt) | someone |"
        for first_cell in ("| plain-name", "| **Some Library** (https://example.invalid/some)",
                           "| see [linked](https://example.invalid/l)", "|"):
            with self.subTest(row=first_cell):
                self.rows.append(first_cell + license_cells)
                self.write_notices()
                with self.assertRaisesRegex(build.BuildError, "malformed notice row: " + re.escape(first_cell)):
                    self.check()
                self.rows.pop()
        text = self.HEADER + self.rows[0] + "\nplain continuation" + license_cells + "\n"
        with self.assertRaisesRegex(build.BuildError, "malformed notice row: plain continuation"):
            components.notice_rows(text)
        with self.assertRaisesRegex(build.BuildError, "notice table without header separator"):
            components.notice_rows("Intro\n\n" + self.rows[0] + "\n" + self.rows[1] + "\n")
        text = ("# Notices\n\n" + self.HEADER.replace("|---|", "| :-- |", 1) + self.rows[0] + "\n\n"
                "Paragraph with | a pipe inside.\n\n  " + self.HEADER.replace("\n", "\n  ", 1) + "  " + self.rows[1] + "\n")
        self.assertEqual({"fixture", "esp-only"}, set(components.notice_rows(text)))
        self.write_notices()
        self.check()

    def test_component_notice_must_match_one_row(self):
        component = self.by_id("fixture")
        component["notice"] = "absent"
        with self.assertRaisesRegex(build.BuildError, "notice row not found: fixture"):
            self.check()
        component["notice"] = "fixture"
        component["origin"]["url"] = "https://example.invalid/other"
        with self.assertRaisesRegex(build.BuildError, "notice URL differs from origin: fixture"):
            self.check()
        component["origin"]["url"] = "https://example.invalid/fixture"
        self.rows[0] = self.rows[0].replace("(LICENSES/MIT-fixture.txt)", "(LICENSES/MIT-esp.txt)")
        self.write_notices()
        with self.assertRaisesRegex(build.BuildError, "notice license file differs: fixture"):
            self.check()

    def test_every_row_is_claimed_exactly_once(self):
        self.by_id("esp-only")["notice"] = "fixture"
        with self.assertRaisesRegex(build.BuildError, "notice row claimed twice: fixture"):
            self.check()
        self.by_id("esp-only")["notice"] = "esp-only"
        self.rows.append("| [extra](https://example.invalid/extra) | — | MIT | [MIT-esp.txt](LICENSES/MIT-esp.txt) | x |")
        self.write_notices()
        with self.assertRaisesRegex(build.BuildError, "unclaimed notice row: extra"):
            self.check()
        self.manifest["notice_exclusions"] = [{"notice": "extra", "reason": "resolved by the package manager"}]
        self.check()
        for exclusion in ({"notice": "fixture", "reason": "claimed"}, {"notice": "missing", "reason": "absent"}):
            with self.subTest(exclusion=exclusion):
                self.manifest["notice_exclusions"] = [{"notice": "extra", "reason": "x"}, exclusion]
                with self.assertRaisesRegex(build.BuildError, "exclusion names a claimed or missing row: " + exclusion["notice"]):
                    self.check()
        self.manifest["notice_exclusions"] = [{"notice": "extra", "reason": "x"}, {"notice": "extra", "reason": "y"}]
        with self.assertRaisesRegex(build.BuildError, "exclusion names a claimed or missing row: extra"):
            self.check()
        self.manifest["notice_exclusions"] = [{"notice": "extra", "reason": ""}]
        with self.assertRaisesRegex(build.BuildError, "exclusion without reason: extra"):
            self.check()


class LicenseContracts(ManifestFixture):
    def test_license_file_must_exist_unlinked_and_hash_raw_bytes(self):
        component = self.by_id("fixture")
        data = (self.root / "LICENSES/MIT-fixture.txt").read_bytes()
        self.put("LICENSES/MIT-fixture.txt", data.replace(b"\n", b"\r\n"))
        with self.assertRaisesRegex(build.BuildError, "license checksum mismatch: fixture: LICENSES/MIT-fixture.txt"):
            self.check()
        self.put("LICENSES/MIT-fixture.txt", data)
        self.check()
        (self.root / "LICENSES/MIT-fixture.txt").unlink()
        with self.assertRaisesRegex(build.BuildError, "missing license file: fixture: LICENSES/MIT-fixture.txt"):
            self.check()
        self.require_symlinks()
        (self.root / "LICENSES/MIT-fixture.txt").symlink_to(self.root / "LICENSES/MIT-esp.txt")
        component["license"]["sha256"] = build.digest(self.root / "LICENSES/MIT-esp.txt")
        with self.assertRaisesRegex(build.BuildError, "symlink in license path: fixture: LICENSES/MIT-fixture.txt"):
            self.check()

    def test_licenses_directory_must_equal_the_linked_files(self):
        self.put("LICENSES/extra.txt", b"nobody links this\n")
        with self.assertRaisesRegex(build.BuildError, r"LICENSES files differ from notice links: missing \[\], unlinked \[LICENSES/extra.txt\]"):
            self.check()
        (self.root / "LICENSES/extra.txt").unlink()
        self.rows.append("| [extra](https://example.invalid/extra) | — | MIT | [gone.txt](LICENSES/gone.txt) | x |")
        self.write_notices()
        self.manifest["notice_exclusions"] = [{"notice": "extra", "reason": "not vendored"}]
        with self.assertRaisesRegex(build.BuildError, r"missing \[LICENSES/gone.txt\], unlinked \[\]"):
            self.check()

    def test_licenses_directory_holds_only_regular_files(self):
        (self.root / "LICENSES/nested").mkdir()
        with self.assertRaisesRegex(build.BuildError, "unexpected entry in LICENSES: nested"):
            self.check()
        (self.root / "LICENSES/nested").rmdir()
        self.check()
        self.require_symlinks()
        (self.root / "LICENSES/linked.txt").symlink_to(self.root / "LICENSES/MIT-esp.txt")
        with self.assertRaisesRegex(build.BuildError, "unexpected entry in LICENSES: linked.txt"):
            self.check()


class RecipeContracts(ManifestFixture):
    def test_linux_components_must_equal_the_recipe_license_list(self):
        self.by_id("esp-only")["targets"] = ["esp32", "linux"]
        with self.assertRaisesRegex(build.BuildError, r"recipe license files differ from linux components: missing \[LICENSES/MIT-esp.txt\], unexpected \[\]"):
            self.check()
        self.by_id("esp-only")["targets"] = ["esp32"]
        self.by_id("recipe-lib")["targets"] = ["esp32"]
        with self.assertRaisesRegex(build.BuildError, r"missing \[\], unexpected \[LICENSES/MIT-recipe.txt\]"):
            self.check()
        self.by_id("recipe-lib")["targets"] = ["linux"]
        self.check()
        (self.package / "awtrix-ng.mk").write_bytes(b"AWTRIX_NG_LICENSE_FILES += LICENSES/MIT-fixture.txt\n")
        with self.assertRaisesRegex(build.BuildError, "one literal"):
            self.check()


class CommandLine(ManifestFixture):
    def test_cli_verifies_fixture_root_and_reports_broken_manifest(self):
        path = self.write_manifest()
        with mock.patch("sys.stdout", new_callable=io.StringIO) as out:
            self.assertEqual(0, components.main(["--root", str(self.root), "--manifest", str(path)]))
        self.assertEqual("3 application components verified\n", out.getvalue())
        self.put("lib/esp/esp.c", b"changed")
        with mock.patch("sys.stderr", new_callable=io.StringIO) as err:
            self.assertEqual(1, components.main(["--root", str(self.root), "--manifest", str(path)]))
        self.assertEqual("components: vendored files differ from manifest: esp-only\n", err.getvalue())
        path.unlink()
        with mock.patch("sys.stderr", new_callable=io.StringIO) as err:
            self.assertEqual(1, components.main(["--root", str(self.root), "--manifest", str(path)]))
        self.assertTrue(err.getvalue().startswith("components: "))

    def test_committed_manifest_matches_checkout(self):
        manifest = components.load_manifest(components.MANIFEST)
        validated = components.check(build.REPO, manifest, build.REPO / components.PACKAGE)
        self.assertEqual(["base64", "berry", "cmudict-scowl", "cpp-httplib", "matrix-fonts", "matrix-fonts-originals",
                          "pubsubclient", "tjpg-decoder", "tjpgdec", "tlsdyn"], sorted(validated))
        linux = {ident for ident, c in validated.items() if "linux" in c["targets"]}
        self.assertEqual({"base64", "berry", "cmudict-scowl", "cpp-httplib", "matrix-fonts", "matrix-fonts-originals",
                          "pubsubclient", "tjpgdec"}, linux)
        with mock.patch("sys.stdout", new_callable=io.StringIO) as out:
            self.assertEqual(0, components.main([]))
        self.assertEqual("10 application components verified\n", out.getvalue())


if __name__ == "__main__":
    unittest.main(verbosity=2)
