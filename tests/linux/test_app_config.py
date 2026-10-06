"""Built-in app configuration against real saved and backup state."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import tempfile
import unittest

import test_contract as fixture


def builtin(name):
    return f"/api/v1/apps/builtin/{name}/config"


class AppConfigContract(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="awtrix-app-config-")
        self.addCleanup(temporary.cleanup)
        self.app = fixture.Runtime(Path(temporary.name))
        self.addCleanup(self.app.stop)
        (self.app.data / "device.json").write_text(json.dumps({"scriptingEnabled": False}))
        (self.app.data / "settings.json").write_text(json.dumps({
            "clockFace": "ring", "weekdayBar": {"show": False, "activeColor": "#123456"}}))
        self.app.start()

    def config(self, name):
        return {field["key"]: field for field in self.app.json("GET", builtin(name))["fields"]}

    def test_builtin_forms_work_without_scripting_and_migrate_existing_values(self):
        self.assertFalse(self.app.json("GET", "/api/v1/device")["scriptingRunning"])
        apps = {app["name"]: app for app in self.app.json("GET", "/api/v1/apps")}
        self.assertTrue(apps["Time"]["config"])
        self.assertTrue(apps["Date"]["config"])
        for name in ("Time", "Date"):
            fields = self.config(name)
            self.assertFalse(fields["weekdayBar.show"]["value"])
            self.assertEqual(0x123456, fields["weekdayBar.activeColor"]["value"])
            self.assertEqual(["weekdayBar", "show"], fields["weekdayBar.show"]["path"])
            self.assertEqual("days", fields["weekdayBar.weekendDays"]["type"])
        self.assertIsNone(self.config("Time")["timeColor"]["value"])
        self.assertTrue(self.config("Time")["timeColor"]["nullable"])
        self.assertEqual(list(range(7)), self.config("Time")["timeMode"]["options"])

    def test_scoped_writes_and_settings_writes_keep_each_bar(self):
        self.app.json("PATCH", builtin("Time"), {
            "weekdayBar": {"show": True, "activeColor": "#FF0000"}, "timeColor": 0})
        self.assertTrue(self.config("Time")["weekdayBar.show"]["value"])
        self.assertFalse(self.config("Date")["weekdayBar.show"]["value"])
        self.assertEqual(0, self.config("Time")["timeColor"]["value"])
        self.app.json("PATCH", builtin("Date"), {"weekdayBar": {"activeColor": "#00FF00"}})
        self.app.json("PATCH", "/api/v1/settings", {"weekdayBar": {"startOnMonday": False}})
        self.assertFalse(self.config("Time")["weekdayBar.startOnMonday"]["value"])
        self.assertFalse(self.config("Date")["weekdayBar.startOnMonday"]["value"])
        self.assertEqual(0xFF0000, self.config("Time")["weekdayBar.activeColor"]["value"])
        self.assertEqual(0x00FF00, self.config("Date")["weekdayBar.activeColor"]["value"])
        for payload in ({"weekdayBar": {"show": True}, "dateWeekdayBar": {"show": False}},
                        {"dateWeekdayBar": {"show": False}, "weekdayBar": {"show": True}}):
            self.app.json("PATCH", "/api/v1/settings", payload)
            self.assertTrue(self.config("Time")["weekdayBar.show"]["value"])
            self.assertFalse(self.config("Date")["weekdayBar.show"]["value"])
        for payload in ({"weekdayBar": {"startOnMonday": True, "activeColor": "#111111"},
                         "dateWeekdayBar": {"activeColor": "#222222"}},
                        {"dateWeekdayBar": {"activeColor": "#222222"},
                         "weekdayBar": {"startOnMonday": True, "activeColor": "#111111"}}):
            self.app.json("PATCH", "/api/v1/settings", {"weekdayBar": {"startOnMonday": False}})
            self.app.json("PATCH", "/api/v1/settings", payload)
            self.assertTrue(self.config("Date")["weekdayBar.startOnMonday"]["value"])
            self.assertEqual(0x111111, self.config("Time")["weekdayBar.activeColor"]["value"])
            self.assertEqual(0x222222, self.config("Date")["weekdayBar.activeColor"]["value"])
        snapshot = self.app.json("GET", "/api/v1/settings")
        self.assertEqual(snapshot, self.app.json("PATCH", "/api/v1/settings", snapshot))

    def test_an_invalid_app_write_changes_nothing(self):
        before = self.app.json("GET", "/api/v1/settings")
        status, body = self.app.request("PATCH", builtin("Time"),
                                        {"timeColor": 123, "brightness": 7})
        self.assertEqual(422, status)
        self.assertEqual("brightness", body["error"]["field"])
        self.assertEqual(before, self.app.json("GET", "/api/v1/settings"))

    def test_restart_and_backup_restore_keep_independent_bars(self):
        self.app.json("PATCH", builtin("Time"), {"weekdayBar": {"show": True}})
        self.app.json("PATCH", builtin("Date"), {"weekdayBar": {"show": False}})
        saved = self.app.json("GET", "/api/v1/settings")
        self.app.stop()
        blob = json.loads((self.app.data / "settings.json").read_text())
        self.assertTrue(blob["weekdayBar"]["show"])
        self.assertFalse(blob["dateWeekdayBar"]["show"])
        self.app.start()
        self.assertEqual(saved, self.app.json("GET", "/api/v1/settings"))
        for backup, expected in ((saved, (True, False)),
                                 ({"clockFace": "ring", "weekdayBar": {"show": True}}, (True, True))):
            archive = fixture.Contract.backup_bytes([("config/settings.json", json.dumps(backup))])
            status, result = self.app.request("POST", "/api/v1/restore", archive, "application/zip")
            self.assertEqual(200, status, result)
            self.assertTrue(result["ok"])
            values = self.app.json("GET", "/api/v1/settings")
            self.assertEqual(expected, (values["weekdayBar"]["show"], values["dateWeekdayBar"]["show"]))

    def test_absent_and_shadowed_apps_are_not_given_builtin_settings(self):
        self.assertEqual(404, self.app.request("GET", builtin("Temperature"))[0])
        self.assertEqual(404, self.app.request("PATCH", builtin("Temperature"), {"useCelsius": False})[0])
        self.app.json("PUT", "/api/v1/apps/order", {"disabled": ["Time"]})
        self.assertTrue(next(app for app in self.app.json("GET", "/api/v1/apps")
                             if app["name"] == "Time")["config"])
        self.assertIn("timeMode", self.config("Time"))
        self.app.json("PUT", "/api/v1/apps/pushed/Time", {"text": "shadow"})
        self.assertEqual(404, self.app.request("GET", builtin("Time"))[0])
        self.assertEqual(404, self.app.request("PATCH", builtin("Time"), {"time24h": False})[0])

    def test_scripts_keep_their_config_route_whatever_shares_their_name(self):
        self.app.stop()
        (self.app.data / "device.json").write_text(json.dumps({"scriptingEnabled": True}))
        self.app.start()
        source = '# @config city text default="Berlin"\nreturn module("{}")\n'
        for name in ("Time", "Ticker"):
            module = f'# @module {name.lower()}lib\n' + source.format(f"{name.lower()}lib")
            self.assertIsNone(self.app.json("PUT", f"/api/v1/apps/script/{name}", module)["error"])
        self.app.json("PUT", "/api/v1/apps/pushed/Ticker", {"text": "pushed"})
        for name in ("Time", "Ticker"):
            path = f"/api/v1/apps/{name}/config"
            self.assertEqual({"city"}, {field["key"] for field in self.app.json("GET", path)["fields"]})
            self.app.json("PATCH", path, {"city": "Hamburg"})
            self.assertEqual("Hamburg", self.app.json("GET", path)["fields"][0]["value"])
        self.assertIn("time24h", self.config("Time"))
        self.app.json("PATCH", builtin("Time"), {"time24h": False})
        self.assertFalse(self.app.json("GET", "/api/v1/settings")["time24h"])
        self.app.stop()
        (self.app.data / "device.json").write_text(json.dumps({"scriptingEnabled": False}))
        self.app.start()
        self.assertEqual("Hamburg",
                         self.app.json("GET", "/api/v1/apps/Time/config")["fields"][0]["value"])
        self.assertEqual(503, self.app.request("PATCH", "/api/v1/apps/Time/config",
                                             {"city": "Bonn"})[0])
        self.app.json("PATCH", builtin("Time"), {"time24h": True})
        self.assertTrue(self.app.json("GET", "/api/v1/settings")["time24h"])


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True)
    parser.add_argument("--webui", required=True)
    options, remaining = parser.parse_known_args()
    options.binary = str(Path(options.binary).resolve())
    options.webui = str(Path(options.webui).resolve())
    fixture.OPTIONS = options
    unittest.main(argv=[__file__, *remaining], verbosity=2)
