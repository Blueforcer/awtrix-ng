"""Helper contracts. No UART access or network; real vendor input is optional and local."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest


@unittest.skipUnless(os.environ.get('AWTRIX_MCU_PREPARE'), 'set AWTRIX_MCU_PREPARE to the built helper')
class PrepareTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.patch = self.root/'patch'
        self.patch.mkdir()
        self.cache = self.root/'cache'
        self.code = bytes(300) + b'AWMC' + struct.pack('<BBHII',1,3,123,3,17)
        self.manifest = dict(schema=2,target='tc002',recipe='tc002-pcm-v1',base_version='V1.0.17',
                            base_sha256='ae9737b190501c8ac9500cae31d9fb496f33f55b8e87413dee8f4b90a9c28bbd',
                            abi=1,features=3,family=123,version=3,build_tag=17,file='extension.bin',
                            bytes=len(self.code),sha256=hashlib.sha256(self.code).hexdigest(),
                            image_bytes=36864,image_sha256='a'*64)
        self.write()

    def write(self):
        (self.patch/'extension.bin').write_bytes(self.code)
        (self.patch/'manifest.json').write_text(json.dumps(self.manifest))

    def run_helper(self):
        prefix = [os.environ['AWTRIX_MCU_PREPARE']]
        if os.environ.get('AWTRIX_MCU_PREPARE_MODE') == 'runtime':
            prefix.append('--prepare-mcu')
        return subprocess.run(prefix + [str(self.patch),str(self.cache),
                               '/unused-ca-in-offline-mode','--offline'],capture_output=True,text=True,timeout=50)

    def test_missing_original_is_not_downloaded_in_offline_mode(self):
        result = self.run_helper()
        self.assertEqual(result.returncode,10,result.stderr)
        self.assertFalse((self.cache/'prepared.pot').exists())

    def test_corrupt_cached_original_cannot_be_patched(self):
        self.cache.mkdir()
        (self.cache/'base-v1.0.17.fot').write_bytes(bytes(35840))
        result = self.run_helper()
        self.assertEqual(result.returncode,12,result.stderr)
        self.assertFalse((self.cache/'prepared.pot').exists())

    def test_extension_and_manifest_identity_must_agree(self):
        self.manifest['build_tag'] = 18
        self.write()
        self.assertEqual(self.run_helper().returncode,13)
        self.assertFalse(self.cache.exists())

    def test_legacy_complete_image_manifest_is_refused(self):
        self.manifest['schema'] = 1
        self.manifest['file'] = 'firmware.pot'
        self.write()
        self.assertEqual(self.run_helper().returncode,13)
        self.assertFalse(self.cache.exists())

    def test_symlink_original_is_refused(self):
        self.cache.mkdir()
        (self.root/'outside').write_bytes(bytes(35840))
        (self.cache/'base-v1.0.17.fot').symlink_to(self.root/'outside')
        self.assertEqual(self.run_helper().returncode,14)

    def test_corrupt_extension_is_refused_before_cache_or_network(self):
        (self.patch/'extension.bin').write_bytes(self.code+b'corrupt')
        self.assertEqual(self.run_helper().returncode,13)
        self.assertFalse(self.cache.exists())

    @unittest.skipUnless(os.environ.get('AWTRIX_MCU_BASE') and os.environ.get('AWTRIX_MCU_PATCH'),
                         'real-image equivalence requires a private original and built public patch')
    def test_exact_private_base_reproduces_the_release_image(self):
        self.patch = Path(os.environ['AWTRIX_MCU_PATCH'])
        self.cache.mkdir()
        shutil.copyfile(os.environ['AWTRIX_MCU_BASE'],self.cache/'base-v1.0.17.fot')
        result = self.run_helper()
        self.assertEqual(result.returncode,0,result.stderr)
        image = (self.cache/'prepared.pot').read_bytes()
        manifest = json.loads((self.patch/'manifest.json').read_bytes())
        self.assertEqual(len(image),manifest['image_bytes'])
        self.assertEqual(hashlib.sha256(image).hexdigest(),manifest['image_sha256'])
        self.assertEqual((self.cache/'prepared.pot').stat().st_mode & 0o777,0o600)


if __name__ == '__main__':
    unittest.main()
