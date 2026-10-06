import hashlib
import json
from pathlib import Path
import tempfile
import unittest

from bundle import stage


class BundleTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.release = self.root/'release'
        self.release.mkdir()
        self.image = b'extension fixture' * 24
        sha = hashlib.sha256(self.image).hexdigest()
        self.manifest = json.dumps(dict(schema=2, recipe='tc002-pcm-v1', target='tc002', version=3, file='extension.bin', bytes=len(self.image), sha256=sha, image_sha256='a'*64, base_sha256='b'*64)).encode()
        (self.release/'extension.bin').write_bytes(self.image)
        (self.release/'manifest.json').write_bytes(self.manifest)
        (self.release/'LICENSE.txt').write_bytes(b'MIT fixture')
        self.lock = self.root/'pin.json'
        self.lock.write_text(json.dumps(dict(version=3, sha256=sha, image_sha256='a'*64, base_sha256='b'*64, license_sha256=hashlib.sha256(b'MIT fixture').hexdigest(),
                                             manifest_sha256=hashlib.sha256(self.manifest).hexdigest())))
        self.output = self.root/'output'

    def test_exact_artifact_is_staged(self):
        stage(self.release, self.output, self.lock)
        self.assertEqual((self.output/'extension.bin').read_bytes(), self.image)
        self.assertEqual((self.output/'manifest.json').read_bytes(), self.manifest)
        self.assertEqual({p.name for p in self.output.iterdir()}, {'extension.bin','manifest.json','LICENSE.txt'})

    def test_changed_image_is_refused_before_output(self):
        (self.release/'extension.bin').write_bytes(self.image+b'changed')
        with self.assertRaises(ValueError):
            stage(self.release, self.output, self.lock)
        self.assertFalse(self.output.exists())

    def test_changed_manifest_is_refused_before_output(self):
        (self.release/'manifest.json').write_bytes(self.manifest+b'\n')
        with self.assertRaises(ValueError):
            stage(self.release, self.output, self.lock)
        self.assertFalse(self.output.exists())

    def test_missing_release_is_refused(self):
        (self.release/'extension.bin').unlink()
        with self.assertRaises(ValueError):
            stage(self.release, self.output, self.lock)
        self.assertFalse(self.output.exists())

    def test_vendor_image_is_refused_even_when_patch_files_match(self):
        (self.release/'firmware.pot').write_bytes(b'vendor firmware')
        with self.assertRaises(ValueError):
            stage(self.release, self.output, self.lock)
        self.assertFalse(self.output.exists())

    def test_stale_vendor_image_in_destination_is_not_published(self):
        self.output.mkdir()
        (self.output/'firmware.pot').write_bytes(b'old private image')
        with self.assertRaises(ValueError):
            stage(self.release, self.output, self.lock)
        self.assertFalse((self.output/'manifest.json').exists())
