#!/usr/bin/env python3
"""Regression tests for documentation drift detection; no firmware build needed."""
from pathlib import Path
import tempfile
import unittest

import check_error_messages as check


class MessageCheck(unittest.TestCase):
    def test_source_strings_exclude_comments_and_character_literals(self):
        source = r'''// "retired error"
        char quote = '"'; /* "obsolete error" */
        auto message = "must be IPv4 or \"\"";
        auto raw = R"msg(no such file)msg";'''
        self.assertEqual(check.literals(source), ['must be IPv4 or ""', 'no such file'])

    def test_old_message_is_rejected(self):
        source = {"api.cpp": ["no MP3 output", "nothing called"]}
        self.assertTrue(check.matches('nothing called "doorbell"', source))
        self.assertFalse(check.matches('no MP3 called "doorbell"', source))

    def test_dynamic_parts_keep_all_fixed_words(self):
        source = {"pins.h": [": GPIO ", " are reserved for ", "SPI flash"]}
        self.assertTrue(check.matches('<pin>: GPIO <range> are reserved for <purpose>', source))
        self.assertFalse(check.matches('<pin>: GPIO <range> are available for <purpose>', source))

    def test_unrelated_files_do_not_complete_a_template(self):
        source = {"a.cpp": ['invalid '], "b.cpp": [' GPIO']}
        self.assertFalse(check.matches('invalid <chip> GPIO', source))

    def test_format_arguments_do_not_accept_arbitrary_messages(self):
        source = {"api.cpp": ['%s', '%s: %s', 'at most %u tracks']}
        self.assertTrue(check.matches('at most 16 tracks', source))
        self.assertFalse(check.matches('at most 16 instruments', source))
        self.assertFalse(check.matches('completely invented error', source))

    def test_message_column_and_status_table_are_checked(self):
        doc = ('| Field | Message |\n|---|---|\n| `name` | `name taken` |\n\n'
               '| Status | Condition |\n|---|---|\n| 422 | `out of range` |\n')
        self.assertEqual(list(check.quoted_messages(doc)).count((3, 'name taken')), 1)
        self.assertIn((7, 'out of range'), list(check.quoted_messages(doc)))

    def test_multiline_json_error_and_escaped_quotes(self):
        doc = '```json\n{"error":{"code":"invalidJson",\n"message":"must be IPv4 or \\"\\""}}\n```'
        self.assertIn((2, 'must be IPv4 or ""'), list(check.quoted_messages(doc)))

    def test_examples_and_headers_are_not_error_messages(self):
        doc = ('```bash\n# message `invented text`\n```\n'
               '| Status | Headers |\n|---|---|\n'
               '| 304 | `Cache-Control: no-cache`, `ETag: <etag>` |\n')
        self.assertEqual(list(check.quoted_messages(doc)), [])

    def test_wrapped_openapi_message(self):
        doc = 'description: |\n  Refused with `needs a sound\n  key`.\n'
        self.assertEqual(list(check.quoted_messages(doc)), [(2, 'needs a sound key')])

    def test_recursive_platform_and_inc_sources_and_failure_location(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name, value in {
                'src/platform/linux/settings/Config.cpp': 'reject("TLS needs a host");',
                'src/platform_settings/ConfigFields.inc': 'RULE("must be a boolean")',
                'src/vendor/dep.h': '"retired error"',
                'docs/reference/errors.md': '| Message |\n|---|\n| `TLS needs a host` |\n| `must be a boolean` |\n',
                'docs/api/openapi.yaml': 'openapi: 3.1.0\n',
            }.items():
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(value, encoding='utf-8')
            self.assertEqual(check.check(root)[1], [])
            (root / 'src/platform/linux/settings/Config.cpp').write_text('// removed', encoding='utf-8')
            failures = check.check(root)[1]
            self.assertEqual(len(failures), 1)
            self.assertIn('docs/reference/errors.md:3:', failures[0])

    def test_berry_exception_text_comes_from_the_vm(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name, value in {
                'lib/berry/src/be_vm.c': 'vm_error(vm, "type_error", "\'%s\' value is not callable", t);',
                'docs/guides/troubleshooting.md': '| Message |\n|---|\n| `type_error: \'string\' value is not callable` |\n'
                                                  '| `type_error: \'string\' value is not iterable` |\n',
                'docs/api/openapi.yaml': 'openapi: 3.1.0\n',
            }.items():
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(value, encoding='utf-8')
            (root / 'src').mkdir()
            failures = check.check(root)[1]
            self.assertEqual(len(failures), 1)
            self.assertIn('not iterable', failures[0])


if __name__ == '__main__':
    unittest.main()
