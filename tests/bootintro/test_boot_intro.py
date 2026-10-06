"""Boot flags, HTTP availability and panel ownership while the intro runs."""
import sys
import argparse
import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time
import unittest
import urllib.error
import urllib.request

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from runtime import free_port

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from runtime import stop_process, wait_ready, eventually

OPTIONS = None
MOOD = 0x123456
WIDTH, HEIGHT = 52, 16


class BootIntro(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory(prefix='awtrix-boot-intro-')
        self.addCleanup(self.folder.cleanup)
        self.data = Path(self.folder.name) / 'data'
        self.log = tempfile.TemporaryFile()
        self.addCleanup(self.log.close)
        self.child = None
        self.channel = None
        self.port = free_port()

    def command(self, *extra):
        return [OPTIONS.binary, '--data', str(self.data), '--webui', OPTIONS.webui, '--port', str(self.port), *extra]

    def run_flags(self, *extra):
        return subprocess.run(self.command(*extra), stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                              timeout=10)

    def start(self, *extra, supervised=False):
        descriptors = ()
        if supervised:
            ours, theirs = socket.socketpair(socket.AF_UNIX, socket.SOCK_SEQPACKET)
            self.channel = ours
            self.addCleanup(ours.close)
            os.dup2(theirs.fileno(), 103)
            theirs.close()
            descriptors = (103,)
            extra = (*extra, '--supervisor-fd', '103')
        try:
            self.child = subprocess.Popen(self.command(*extra), stdout=self.log, stderr=self.log,
                                          pass_fds=descriptors)
        finally:
            if supervised:
                os.close(103)
        self.addCleanup(self.stop)
        wait_ready(self.child, lambda: self.request('GET', '/api/v1/version')[0] == 200, self.output)
        return time.monotonic()

    def stop(self):
        stop_process(self.child, self.output, timeout=5)

    def output(self):
        self.log.seek(0)
        return self.log.read().decode(errors='replace')

    def send(self, kind, **fields):
        self.channel.send(json.dumps({'v': 2, 'type': kind, **fields}).encode())

    def request(self, method, path, body=None):
        data = None if body is None else json.dumps(body).encode()
        request = urllib.request.Request(f'http://127.0.0.1:{self.port}{path}', data=data, method=method,
                                         headers={'Content-Type': 'application/json'})
        try:
            response = urllib.request.urlopen(request, timeout=4)
        except urllib.error.HTTPError as error:
            response = error
        with response:
            raw = response.read()
            return response.status, json.loads(raw) if raw else None

    def screen(self):
        status, value = self.request('GET', '/api/v1/display/screen')
        self.assertEqual(200, status, value)
        self.assertEqual((WIDTH, HEIGHT), (value['width'], value['height']))
        return value['pixels']

    def moodlight(self):
        status, value = self.request('PUT', '/api/v1/display/moodlight', {'color': '#123456', 'brightness': 50})
        self.assertEqual(200, status, value)

    def frame_until(self, predicate):
        deadline = time.monotonic() + 1
        while time.monotonic() < deadline:
            pixels = self.screen()
            if predicate(pixels):
                return pixels
            time.sleep(0.01)
        self.fail('expected display state did not appear:\n' + self.output())

    def test_boot_sound_needs_the_intro_and_a_file(self):
        for extra in (('--boot-sound', '/tmp/boot.mp3'),
                      ('--boot-intro', '--boot-sound'),
                      ('--boot-intro', '--boot-sound', ''),
                      ('--boot-intro', '--boot-sound', 'a.mp3', '--boot-sound', 'b.mp3')):
            with self.subTest(extra=extra):
                result = self.run_flags(*extra)
                self.assertEqual(2, result.returncode, result.stdout)
                self.assertTrue(result.stdout.strip(), 'invalid flags explain their failure')
                self.assertFalse(self.data.exists())

    def test_without_the_intro_the_apps_start_at_once(self):
        self.start()
        self.moodlight()
        self.frame_until(lambda pixels: all(pixel == MOOD for pixel in pixels))

    def test_the_intro_animates_while_http_accepts_app_changes(self):
        self.start('--boot-intro', '--boot-sound', str(Path(self.folder.name) / 'boot.mp3'))
        first = self.screen()
        self.moodlight()
        self.frame_until(lambda pixels: pixels != first and any(pixels)
                         and not all(pixel == MOOD for pixel in pixels))

    def test_supervisor_network_updates_arrive_during_the_intro(self):
        self.start('--boot-intro', supervised=True)
        self.send('network', link='connected', ssid='Home', rssi=-61, mac='02:00:00:00:00:07',
                  ipv4='192.0.2.7', gateway='192.0.2.1', dns='192.0.2.1', hostname='awtrix-000007')
        self.moodlight()
        deadline = time.monotonic() + 1
        while time.monotonic() < deadline:
            status, device = self.request('GET', '/api/v1/device')
            self.assertEqual(200, status)
            if device.get('ipAddress') == '192.0.2.7':
                break
            time.sleep(0.01)
        else:
            self.fail('supervisor address did not reach the device API')
        self.frame_until(lambda pixels: any(pixels) and not all(pixel == MOOD for pixel in pixels))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--binary', required=True)
    parser.add_argument('--webui', required=True)
    OPTIONS, arguments = parser.parse_known_args()
    unittest.main(argv=[__file__, *arguments])
