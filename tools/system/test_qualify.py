"""Offline integrity and release-gate contracts; no compiler, network or device."""
from __future__ import annotations

import copy
import csv
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock


SPEC = importlib.util.spec_from_file_location("awtrix_qualify", Path(__file__).with_name("qualify.py"))
assert SPEC and SPEC.loader
qualify = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(qualify)
build = qualify.build
components = qualify.components


class QualificationContracts(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="awtrix-qualification-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        self.commit = "a" * 40
        for name, data in {
            "application/src/main.cpp": b"application source",
            "external/configs/awtrix_armv7_defconfig": b"frozen configuration",
            "buildroot/Makefile": b"frozen build tool",
            "downloads/buildroot-test.tar.xz": b"buildroot archive",
            "application.tar": b"application archive",
            "output/.config": b"BR2_arm=y\n# BR2_LINUX_KERNEL is not set\n",
            "output/images/rootfs.tar": b"rootfs fixture",
        }.items():
            self.put(name, data)
        header = bytearray(20)
        header[:6] = b"\x7fELF\x01\x01"
        header[18:20] = b"\x28\x00"
        self.put("output/target/usr/bin/awtrix-linux", bytes(header) + b"ARM fixture")
        self.put("application/lib/fixture/fixture.c", b"int fixture(void) { return 1; }\n")
        self.put("application/LICENSES/MIT-fixture.txt", b"MIT fixture license\n")
        self.rows = ["| [fixture](https://example.invalid/fixture) | `lib/fixture/` | MIT | "
                     "[MIT-fixture.txt](LICENSES/MIT-fixture.txt) | fixture author |"]
        self.recipe_licenses = ["LICENSES/MIT-fixture.txt"]
        self.manifest = {"schema_version": 1, "scope": "fixture components", "notice_exclusions": [],
                         "components": [self.component("fixture", ["lib/fixture"], "MIT-fixture", ["linux"])]}
        self.inputs = {
            "schema_version": 1, "profile": "awtrix_armv7_defconfig", "application_commit": self.commit,
            "source_date_epoch": 1234567890,
            "source_lock": {"buildroot": {"version": "test", "url": "https://example.invalid/buildroot-test.tar.xz",
                                           "sha256": build.digest(self.root / "downloads/buildroot-test.tar.xz")}},
            "application_archive_sha256": build.digest(self.root / "application.tar"),
        }
        self.write_manifest()
        self.info = {
            "awtrix-ng": self.package("awtrix-ng", self.commit, "PolyForm-Noncommercial-1.0.0, MIT (bundled), TJpgDec"),
            "linux-headers": self.package("linux-headers", "6.18.52", "GPL-2.0"),
            "host-cmake": self.package("cmake", "3.31", "BSD-3-Clause", host=True),
            "skeleton": {"type": "target", "name": "skeleton", "virtual": True},
            "skeleton-init-none": self.package("skeleton-init-none", "", "GPL-2.0-or-later"),
            "rootfs-tar": {"type": "rootfs", "name": "tar", "virtual": False},
        }
        self.info["linux-headers"]["cpe-id"] = "cpe:2.3:a:linux:linux_kernel:6.18.52:*:*:*:*:*:*:*"
        self.write_manifests()
        self.put("output/legal-info/README", b"WARNING: license review still required\n")
        self.put("output/legal-info/buildroot.config", (self.root / "output/.config").read_bytes())
        self.put("output/legal-info/sources/awtrix-ng-" + self.commit + "/application.tar.gz", b"saved application source")
        self.write_legal_sums()
        metadata = mock.patch.object(qualify, "collect_show_info", side_effect=lambda *_: copy.deepcopy(self.info))
        metadata.start()
        self.addCleanup(metadata.stop)
        network = mock.patch.object(build.urllib.request, "urlopen", side_effect=AssertionError("network access"))
        network.start()
        self.addCleanup(network.stop)
        process = mock.patch.object(qualify.subprocess, "check_output", side_effect=AssertionError("external process"))
        process.start()
        self.addCleanup(process.stop)

    def put(self, name, data):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        return path

    def component(self, ident, paths, license_name, targets, **extra):
        """A manifest entry for the frozen fixture tree; vendored digests are computed."""
        value = {
            "id": ident, "name": ident.title(), "version": "1.0",
            "origin": {"url": "https://example.invalid/" + ident.split("-")[0], "reference": "v1.0"},
            "vendored": bool(paths), "paths": paths, "patches": [],
            "license": {"spdx": "MIT", "name": "MIT", "file": f"LICENSES/{license_name}.txt",
                        "sha256": build.digest(self.root / "application" / f"LICENSES/{license_name}.txt")},
            "notice": ident, "targets": targets,
            "review": {"decision": "accepted", "date": "2026-09-16", "rationale": "fixture review"},
        }
        if not paths:
            value["bundled_from"] = "fixture package"
        value.update(extra)
        if paths:
            value["files_sha256"] = components.files_digest(components.component_files(self.root / "application", value))
        return value

    def write_manifest(self):
        """Freeze manifest, notices and recipe so inputs.json and build-result.json stay consistent."""
        text = "# Notices\n\n| Component | Location | License | Text | Copyright |\n|---|---|---|---|---|\n"
        self.put("application/THIRD-PARTY-NOTICES.md", (text + "\n".join(self.rows) + "\n").encode("utf-8"))
        self.put("application/LICENSE.md", b"application license\n")
        manifest = self.root / "application/tools/system/components.json"
        manifest.parent.mkdir(parents=True, exist_ok=True)
        build.write_json(manifest, self.manifest)
        licenses = ["LICENSE.md", "THIRD-PARTY-NOTICES.md", *self.recipe_licenses]
        self.put("external/package/awtrix-ng/awtrix-ng.mk",
                 ("AWTRIX_NG_LICENSE_FILES = " + " \\\n\t".join(licenses) + "\n").encode())
        self.put("external/package/awtrix-ng/awtrix-ng.hash",
                 "".join(f"sha256  {build.digest(self.root / 'application' / name)}  {name}\n" for name in licenses).encode())
        self.inputs.update(application_files=build.tree_hashes(self.root / "application"),
                           external_files=build.tree_hashes(self.root / "external"),
                           buildroot_files=build.tree_hashes(self.root / "buildroot"))
        build.write_json(self.root / "inputs.json", self.inputs)
        build.record_result(self.root, self.inputs)
        self.result = qualify.json_file(self.root / "build-result.json")

    def by_id(self, ident):
        return next(c for c in self.manifest["components"] if c["id"] == ident)

    def add_recipe_component(self):
        self.put("application/LICENSES/MIT-recipe.txt", b"MIT recipe license\n")
        self.rows.append("| [recipe-lib](https://example.invalid/recipe) | — | MIT | "
                         "[MIT-recipe.txt](LICENSES/MIT-recipe.txt) | recipe author |")
        self.recipe_licenses.append("LICENSES/MIT-recipe.txt")
        self.manifest["components"].append(self.component(
            "recipe-lib", [], "MIT-recipe", ["esp32", "linux"], version="1.4.0",
            recipe_package="awtrix-base64"))
        self.write_manifest()
        self.info["awtrix-base64"] = self.package("awtrix-base64", "1.4.0", "MIT")
        self.write_manifests()
        self.write_legal_sums()

    @staticmethod
    def package(name, version, license_name, host=False):
        return {"type": "host" if host else "target", "name": name, "virtual": False,
                "version": version, "licenses": license_name, "license_files": ["COPYING"],
                "downloads": [{"source": "source.tar.xz", "uris": ["https+https://example.invalid/releases"]}],
                "dependencies": ["skeleton"], "install_target": not host}

    def write_manifests(self):
        header = ["PACKAGE", "VERSION", "LICENSE", "LICENSE FILES", "SOURCE ARCHIVE", "SOURCE SITE"]
        for host, filename in ((False, "manifest.csv"), (True, "host-manifest.csv")):
            out = io.StringIO(newline="")
            writer = csv.writer(out)
            writer.writerow(header)
            if host:
                writer.writerow(["buildroot", "test", "GPL-2.0+", "COPYING", "not saved", "not saved"])
            for package in self.info.values():
                if package["type"] != ("host" if host else "target") or not package.get("version"):
                    continue
                writer.writerow([package["name"], package["version"], package["licenses"], "COPYING",
                                 "source.tar.xz", "https://example.invalid/releases"])
            self.put("output/legal-info/" + filename, out.getvalue().encode())

    def write_legal_sums(self):
        root = self.root / "output/legal-info"
        hashes = build.tree_hashes(root)
        hashes.pop("legal-info.sha256", None)
        self.put("output/legal-info/legal-info.sha256",
                 "".join(h + "  " + p + "\n" for p, h in sorted(hashes.items())).encode())

    def evidence(self, passed=False):
        value = qualify.evidence_template(self.result)
        if passed:
            proof = self.put("evidence/report.txt", b"review fixture, not real hardware evidence")
            for check in value["checks"].values():
                check.update(state="passed", reviewer="test reviewer",
                             files=[{"path": "report.txt", "sha256": build.digest(proof)}])
        path = self.root / "evidence/review.json"
        path.parent.mkdir(exist_ok=True)
        build.write_json(path, value)
        return path, value

    def test_valid_development_inventory_preserves_roles_and_license_wording(self):
        report, sbom, info, template, _ = qualify.qualify(self.root)
        self.assertFalse(report["public_release"]["eligible"])
        self.assertEqual(set(qualify.CHECKS), set(template["checks"]))
        self.assertTrue(all(c["state"] == "unmet" for c in report["checks"].values()))
        by_ref = {c["bom-ref"]: c for c in sbom["components"]}
        app = by_ref["buildroot:awtrix-ng"]
        self.assertEqual(self.info["awtrix-ng"]["licenses"], app["licenses"][0]["license"]["name"])
        header = by_ref["buildroot:linux-headers"]
        self.assertNotIn("cpe", header, "headers must not be represented as a running kernel")
        role = {p["name"]: p["value"] for p in by_ref["buildroot:host-cmake"]["properties"]}
        self.assertEqual("host", role["awtrix:package-role"])
        self.assertNotIn("buildroot:skeleton", by_ref)
        self.assertNotIn("buildroot:rootfs-tar", by_ref)
        self.assertEqual("incomplete", sbom["compositions"][0]["aggregate"])
        self.assertEqual(self.info, info)
        self.assertEqual(qualify.qualify(self.root)[1], sbom, "inventory must be deterministic")

    def test_artifact_tampering_fails_even_with_unchanged_size(self):
        path = self.root / qualify.ARTIFACTS[0]
        path.write_bytes(b"X" * path.stat().st_size)
        with self.assertRaisesRegex(qualify.BuildError, "checksum mismatch"):
            qualify.qualify(self.root)

    def test_frozen_recipe_mutation_is_detected(self):
        self.put("external/configs/awtrix_armv7_defconfig", b"changed recipe")
        with self.assertRaisesRegex(qualify.BuildError, "frozen external inputs changed"):
            qualify.qualify(self.root)

    def test_result_rejects_failed_build_wrong_commit_and_missing_artifact(self):
        changes = ({"status": "failed"}, {"application_commit": "b" * 40}, {"artifacts": {}})
        for changed in changes:
            with self.subTest(changed=changed):
                build.write_json(self.root / "build-result.json", dict(self.result, **changed))
                with self.assertRaises(qualify.BuildError):
                    qualify.qualify(self.root)

    def test_changed_configuration_is_rejected(self):
        self.put("output/.config", b"changed config")
        with self.assertRaisesRegex(qualify.BuildError, "checksum mismatch: output/.config"):
            qualify.qualify(self.root)

    def test_build_result_cannot_add_escaping_artifact(self):
        value = copy.deepcopy(self.result)
        value["artifacts"]["../outside"] = value["artifacts"][qualify.ARTIFACTS[0]]
        build.write_json(self.root / "build-result.json", value)
        with self.assertRaisesRegex(qualify.BuildError, "both expected"):
            qualify.qualify(self.root)

    def test_legal_tampering_fails_before_inventory(self):
        self.put("output/legal-info/README", b"changed legal evidence")
        with self.assertRaisesRegex(qualify.BuildError, "checksum mismatch: README"):
            qualify.qualify(self.root)

    def test_stale_legal_manifest_version_is_rejected_even_with_updated_checksums(self):
        self.info["awtrix-ng"]["version"] = "b" * 40
        self.write_manifests()
        self.write_legal_sums()
        with self.assertRaisesRegex(qualify.BuildError, "built application commit"):
            qualify.qualify(self.root)

    def test_legal_manifest_and_show_info_must_agree(self):
        self.info["linux-headers"]["licenses"] = "different license"
        with self.assertRaisesRegex(qualify.BuildError, "differs from configured package"):
            qualify.qualify(self.root)

    def test_legal_config_cannot_describe_different_build(self):
        self.put("output/legal-info/buildroot.config", b"another build config")
        self.write_legal_sums()
        with self.assertRaisesRegex(qualify.BuildError, "different build configuration"):
            qualify.qualify(self.root)

    def test_missing_required_legal_file_is_rejected(self):
        (self.root / "output/legal-info/host-manifest.csv").unlink()
        self.write_legal_sums()
        with self.assertRaisesRegex(qualify.BuildError, "omits required evidence"):
            qualify.qualify(self.root)

    def test_evidence_for_other_artifacts_is_rejected(self):
        path, value = self.evidence()
        value["subject"]["inputs_sha256"] = "0" * 64
        build.write_json(path, value)
        with self.assertRaisesRegex(qualify.BuildError, "not bound"):
            qualify.qualify(self.root, path)

    def test_passed_check_requires_hashed_proof_and_reviewer(self):
        path, value = self.evidence()
        value["checks"]["peripherals"]["state"] = "passed"
        build.write_json(path, value)
        with self.assertRaisesRegex(qualify.BuildError, "reviewer and hashed evidence"):
            qualify.qualify(self.root, path)

    def test_even_all_passed_review_records_cannot_promote_userspace_to_firmware(self):
        path, _ = self.evidence(passed=True)
        report, *_ = qualify.qualify(self.root, path)
        self.assertFalse(report["public_release"]["eligible"])
        self.assertEqual(["experimental_userspace_profile_has_no_qualified_TC002_boot_image"],
                         report["public_release"]["reasons"])

    def test_proof_traversal_and_tampering_are_rejected(self):
        path, value = self.evidence(passed=True)
        check = value["checks"]["peripherals"]
        for invalid in ("../report.txt", "/report.txt", "C:/report.txt", "dir\\report.txt"):
            with self.subTest(path=invalid):
                check["files"][0]["path"] = invalid
                build.write_json(path, value)
                with self.assertRaisesRegex(qualify.BuildError, "invalid evidence path"):
                    qualify.qualify(self.root, path)
        path, _ = self.evidence(passed=True)
        self.put("evidence/report.txt", b"changed proof")
        with self.assertRaisesRegex(qualify.BuildError, "checksum mismatch: report.txt"):
            qualify.qualify(self.root, path)

    def test_duplicate_json_keys_are_rejected(self):
        path = self.put("duplicate.json", b'{"schema_version":1,"schema_version":2}')
        with self.assertRaisesRegex(qualify.BuildError, "duplicate JSON key"):
            qualify.json_file(path)

    def test_cli_writes_inventory_and_returns_explicit_public_block(self):
        with mock.patch("sys.stdout", new_callable=io.StringIO) as out:
            status = qualify.main(["--work", str(self.root), "--public"])
        self.assertEqual(2, status)
        output = self.root / "output/qualification"
        report = qualify.json_file(output / "qualification.json")
        self.assertEqual({"inventory.cdx.json", "show-info.json", "evidence-template.json", "triage-template.json"},
                         set(report["generated_files_sha256"]))
        for name, digest in report["generated_files_sha256"].items():
            self.assertEqual(digest, build.digest(output / name))
        self.assertFalse(report["public_release"]["eligible"])
        self.assertIn("1 nested application components, vulnerability triage not_performed. Public release: blocked.",
                      out.getvalue())

    def test_inventory_nests_linux_vendored_components_under_application_recipe(self):
        report, sbom, _, _, _ = qualify.qualify(self.root)
        by_ref = {c["bom-ref"]: c for c in sbom["components"]}
        nested = by_ref["buildroot:awtrix-ng"]["components"]
        self.assertEqual(1, len(nested))
        entry = nested[0]
        self.assertEqual("application-component:fixture", entry["bom-ref"])
        self.assertEqual("library", entry["type"])
        self.assertEqual("Fixture", entry["name"])
        self.assertEqual("1.0", entry["version"])
        self.assertEqual([{"license": {"id": "MIT"}}], entry["licenses"])
        self.assertEqual([{"type": "website", "url": "https://example.invalid/fixture"}], entry["externalReferences"])
        values = {p["name"]: p["value"] for p in entry["properties"]}
        fixture = self.by_id("fixture")
        self.assertEqual(fixture["files_sha256"], values["awtrix:files-sha256"])
        self.assertEqual("v1.0", values["awtrix:origin-reference"])
        self.assertEqual("fixture", values["awtrix:manifest-id"])
        self.assertEqual("[]", values["awtrix:patches"])
        self.assertEqual("accepted", values["awtrix:review-decision"])
        self.assertEqual("2026-09-16", values["awtrix:review-date"])
        self.assertEqual("LICENSES/MIT-fixture.txt", values["awtrix:license-file"])
        self.assertEqual(fixture["license"]["sha256"], values["awtrix:license-file-sha256"])
        self.assertEqual('["lib/fixture"]', values["awtrix:paths"])
        self.assertNotIn("application-component:fixture", by_ref, "nested components are not top-level entries")
        self.assertEqual(sbom, qualify.qualify(self.root)[1], "inventory must be deterministic")
        fixture["license"]["spdx"] = None
        fixture["license"]["name"] = "Permissive (notice retained)"
        fixture["origin"]["url"] = "https://github.com/example/fixture"
        self.rows[0] = self.rows[0].replace("https://example.invalid/fixture", "https://github.com/example/fixture")
        fixture["patches"] = [{"summary": "documented change", "files": ["lib/fixture/fixture.c"]}]
        self.write_manifest()
        entry = qualify.qualify(self.root)[1]["components"]
        entry = next(c for c in entry if c["bom-ref"] == "buildroot:awtrix-ng")["components"][0]
        self.assertEqual([{"license": {"name": "Permissive (notice retained)"}}], entry["licenses"])
        self.assertEqual("vcs", entry["externalReferences"][0]["type"])
        self.assertEqual('["documented change"]', {p["name"]: p["value"] for p in entry["properties"]}["awtrix:patches"])

    def test_esp32_only_component_is_validated_but_not_nested(self):
        self.put("application/lib/esp/esp.c", b"esp only\n")
        self.put("application/LICENSES/MIT-esp.txt", b"MIT esp license\n")
        self.rows.append("| [esp-only](https://example.invalid/esp) | `lib/esp/` | MIT | "
                         "[MIT-esp.txt](LICENSES/MIT-esp.txt) | esp author |")
        self.manifest["components"].append(self.component("esp-only", ["lib/esp"], "MIT-esp", ["esp32"]))
        self.write_manifest()
        report, sbom, *_ = qualify.qualify(self.root)
        nested = next(c for c in sbom["components"] if c["bom-ref"] == "buildroot:awtrix-ng")["components"]
        self.assertEqual(["application-component:fixture"], [c["bom-ref"] for c in nested])
        self.assertEqual({"fixture", "esp-only"}, set(report["application_components"]))
        self.assertEqual(["esp32"], report["application_components"]["esp-only"]["targets"])
        self.put("application/lib/esp/esp.c", b"changed without re-review\n")
        self.inputs["application_files"] = build.tree_hashes(self.root / "application")
        build.write_json(self.root / "inputs.json", self.inputs)
        build.record_result(self.root, self.inputs)
        with self.assertRaisesRegex(qualify.BuildError, "vendored files differ from manifest: esp-only"):
            qualify.qualify(self.root)

    def test_recipe_component_is_cross_checked_against_show_info(self):
        self.add_recipe_component()
        report, sbom, *_ = qualify.qualify(self.root)
        by_ref = {c["bom-ref"]: c for c in sbom["components"]}
        values = {p["name"]: p["value"] for p in by_ref["buildroot:awtrix-base64"]["properties"]}
        self.assertEqual("recipe-lib", values["awtrix:component-manifest-id"])
        self.assertNotIn("awtrix:component-manifest-id", {p["name"] for p in by_ref["buildroot:linux-headers"]["properties"]})
        self.assertEqual(["application-component:fixture"],
                         [c["bom-ref"] for c in by_ref["buildroot:awtrix-ng"]["components"]])
        self.assertIsNone(report["application_components"]["recipe-lib"]["files_sha256"])
        self.info["awtrix-base64"]["version"] = "1.4.1"
        self.write_manifests()
        self.write_legal_sums()
        with self.assertRaisesRegex(qualify.BuildError, "recipe package differs from component manifest: recipe-lib"):
            qualify.qualify(self.root)
        del self.info["awtrix-base64"]
        self.write_manifests()
        self.write_legal_sums()
        with self.assertRaisesRegex(qualify.BuildError, "recipe package differs from component manifest: recipe-lib"):
            qualify.qualify(self.root)

    def test_missing_manifest_in_snapshot_fails_before_show_info(self):
        (self.root / "application/tools/system/components.json").unlink()
        self.inputs["application_files"] = build.tree_hashes(self.root / "application")
        build.write_json(self.root / "inputs.json", self.inputs)
        build.record_result(self.root, self.inputs)
        with mock.patch.object(qualify, "collect_show_info", side_effect=AssertionError("show-info collected")):
            with self.assertRaisesRegex(qualify.BuildError, "tools/system/components.json"):
                qualify.qualify(self.root)
        with mock.patch("sys.stderr", new_callable=io.StringIO) as err:
            self.assertEqual(1, qualify.main(["--work", str(self.root)]))
        self.assertIn("tools/system/components.json", err.getvalue())

    def test_manifest_inconsistent_with_frozen_snapshot_is_rejected(self):
        cases = (("application/lib/fixture/fixture.c", b"changed vendored byte", "vendored files differ from manifest: fixture"),
                 ("application/LICENSES/MIT-fixture.txt", b"MIT fixture license\r\n",
                  "license checksum mismatch: fixture: LICENSES/MIT-fixture.txt"))
        for name, data, message in cases:
            with self.subTest(name=name):
                original = (self.root / name).read_bytes()
                self.put(name, data)
                self.inputs["application_files"] = build.tree_hashes(self.root / "application")
                build.write_json(self.root / "inputs.json", self.inputs)
                build.record_result(self.root, self.inputs)
                with mock.patch("sys.stderr", new_callable=io.StringIO) as err:
                    self.assertEqual(1, qualify.main(["--work", str(self.root)]))
                self.assertIn(message, err.getvalue())
                self.assertFalse((self.root / "output/qualification").exists())
                self.put(name, original)

    def test_report_records_manifest_hash_components_and_not_performed_triage(self):
        report, sbom, *_ = qualify.qualify(self.root)
        manifest_sha256 = build.digest(self.root / "application/tools/system/components.json")
        self.assertEqual(manifest_sha256, report["component_manifest_sha256"])
        self.assertEqual({"fixture": {"version": "1.0", "files_sha256": self.by_id("fixture")["files_sha256"],
                                      "targets": ["linux"], "review_decision": "accepted"}},
                         report["application_components"])
        self.assertEqual("passed", report["automated_checks"]["component_manifest_consistency"])
        triage = report["vulnerability_triage"]
        self.assertEqual("not_performed", triage["status"])
        self.assertIsNone(triage["data_source"])
        self.assertIsNone(triage["record_sha256"])
        self.assertEqual({}, triage["findings"])
        self.assertIn("Buildroot packages are not covered", triage["scope"])
        metadata = {p["name"]: p["value"] for p in sbom["metadata"]["properties"]}
        self.assertEqual(manifest_sha256, metadata["awtrix:component-manifest-sha256"])
        self.assertEqual("not_performed", metadata["awtrix:vulnerability-triage"])
        self.assertNotIn("vulnerabilities", json.dumps(sbom))
        self.assertIn("not_performed", " ".join(report["limitations"]))
        self.assertNotIn("unmet:component_review:fixture", report["public_release"]["reasons"])
        self.by_id("fixture")["review"]["decision"] = "pending"
        self.write_manifest()
        report, *_ = qualify.qualify(self.root)
        self.assertIn("unmet:component_review:fixture", report["public_release"]["reasons"])
        self.assertEqual("pending", report["application_components"]["fixture"]["review_decision"])

    def triage_record(self, **changes):
        _, _, _, _, template = qualify.qualify(self.root)
        value = copy.deepcopy(template)
        value["data_source"] = {"name": "fixture advisory list", "reference": "https://example.invalid/advisories",
                                "retrieved": "2026-09-16"}
        value["reviewer"] = "test reviewer"
        for entry in value["components"].values():
            entry.update(status="no_known_advisories", note="no entries for this version")
        value.update(changes)
        path = self.root / "triage/record.json"
        path.parent.mkdir(exist_ok=True)
        build.write_json(path, value)
        return path, value

    def test_bound_triage_record_is_performed_and_findings_block_release(self):
        path, value = self.triage_record()
        report, sbom, *_ = qualify.qualify(self.root, triage=path)
        triage = report["vulnerability_triage"]
        self.assertEqual("performed", triage["status"])
        self.assertEqual(build.digest(path), triage["record_sha256"])
        self.assertEqual(value["data_source"], triage["data_source"])
        self.assertEqual("test reviewer", triage["reviewer"])
        self.assertEqual({"fixture": "no_known_advisories"}, triage["findings"])
        metadata = {p["name"]: p["value"] for p in sbom["metadata"]["properties"]}
        self.assertEqual("performed:" + build.digest(path), metadata["awtrix:vulnerability-triage"])
        self.assertNotIn("vulnerabilities", json.dumps(sbom))
        self.assertFalse(any(r.startswith("vulnerability_triage:") for r in report["public_release"]["reasons"]))
        for status in ("affected", "under_investigation"):
            with self.subTest(status=status):
                value["components"]["fixture"]["status"] = status
                build.write_json(path, value)
                report, *_ = qualify.qualify(self.root, triage=path)
                self.assertIn(f"vulnerability_triage:fixture:{status}", report["public_release"]["reasons"])
        for status in ("not_affected", "fixed_locally"):
            with self.subTest(status=status):
                value["components"]["fixture"]["status"] = status
                build.write_json(path, value)
                report, *_ = qualify.qualify(self.root, triage=path)
                self.assertFalse(any(r.startswith("vulnerability_triage:") for r in report["public_release"]["reasons"]))

    def test_invalid_triage_records_are_rejected_never_reported_as_not_performed(self):
        path, value = self.triage_record()
        cases = {
            "not bound to this component manifest": {"subject": {"component_manifest_sha256": "0" * 64}},
            "omits component: fixture": {"components": {}},
            "lists no components (list)": {"components": []},
            "lists no components (text)": {"components": "fixture"},
            "names unknown component: other": {"components": dict(value["components"], other={"status": "affected", "note": "x"})},
            "invalid triage status: fixture": {"components": {"fixture": {"status": "clean", "note": "x"}}},
            "missing triage note: fixture": {"components": {"fixture": {"status": "affected", "note": ""}}},
            "names no data source (empty)": {"data_source": {"name": "", "reference": "", "retrieved": ""}},
            "names no data source (date)": {"data_source": {"name": "x", "reference": "y", "retrieved": "yesterday"}},
            "names no reviewer": {"reviewer": " "},
            "unsupported triage record schema": {"schema_version": 2},
            "unsupported triage record schema (key)": {"vulnerabilities": []},
        }
        for message, changes in cases.items():
            with self.subTest(message=message):
                build.write_json(path, dict(value, **changes))
                with self.assertRaisesRegex(qualify.BuildError, message.split(" (")[0]):
                    qualify.qualify(self.root, triage=path)
                with mock.patch("sys.stderr", new_callable=io.StringIO) as err:
                    self.assertEqual(1, qualify.main(["--work", str(self.root), "--triage", str(path)]))
                self.assertNotIn("not_performed", err.getvalue())
                self.assertFalse((self.root / "output/qualification/qualification.json").exists())

    def test_triage_template_is_written_hashed_and_protected(self):
        with mock.patch("sys.stdout", new_callable=io.StringIO):
            self.assertEqual(0, qualify.main(["--work", str(self.root)]))
        output = self.root / "output/qualification"
        template = qualify.json_file(output / "triage-template.json")
        report = qualify.json_file(output / "qualification.json")
        self.assertEqual(build.digest(output / "triage-template.json"), report["generated_files_sha256"]["triage-template.json"])
        self.assertEqual(report["component_manifest_sha256"], template["subject"]["component_manifest_sha256"])
        self.assertEqual(["fixture"], list(template["components"]))
        self.assertEqual({"status": "under_investigation", "note": ""}, template["components"]["fixture"])
        self.assertEqual({"name": "", "reference": "", "retrieved": ""}, template["data_source"])
        original = (output / "triage-template.json").read_bytes()
        with mock.patch("sys.stderr", new_callable=io.StringIO) as err:
            self.assertEqual(1, qualify.main(["--work", str(self.root), "--triage", str(output / "triage-template.json")]))
        self.assertIn("overwrite", err.getvalue())
        self.assertEqual(original, (output / "triage-template.json").read_bytes())
        self.assertEqual(report, qualify.json_file(output / "qualification.json"))

    def test_cli_returns_failure_for_invalid_artifacts(self):
        self.put(qualify.ARTIFACTS[1], b"broken ELF")
        with mock.patch("sys.stderr", new_callable=io.StringIO):
            status = qualify.main(["--work", str(self.root)])
        self.assertEqual(1, status)
        self.assertFalse((self.root / "output/qualification/qualification.json").exists())

    def test_output_cannot_write_into_frozen_inputs_or_artifacts(self):
        for folder in ("application", "external/report", "buildroot", "output/target", "output/legal-info"):
            with self.subTest(folder=folder), mock.patch("sys.stderr", new_callable=io.StringIO):
                self.assertEqual(1, qualify.main(["--work", str(self.root), "--output", str(self.root / folder)]))
        build.verify(self.root)

    def test_output_cannot_overwrite_evidence_file(self):
        path = self.root / "output/qualification/evidence-template.json"
        path.parent.mkdir(parents=True)
        build.write_json(path, qualify.evidence_template(self.result))
        original = path.read_bytes()
        with mock.patch("sys.stderr", new_callable=io.StringIO):
            self.assertEqual(1, qualify.main(["--work", str(self.root), "--evidence", str(path)]))
        self.assertEqual(original, path.read_bytes())

    def test_output_preserves_existing_partial_write(self):
        path = self.put("output/qualification/inventory.cdx.json.partial", b"unfinished previous write")
        with mock.patch("sys.stderr", new_callable=io.StringIO):
            self.assertEqual(1, qualify.main(["--work", str(self.root)]))
        self.assertEqual(b"unfinished previous write", path.read_bytes())


if __name__ == "__main__":
    unittest.main(verbosity=2)
