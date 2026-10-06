"""Exercise the supervisor channel (--supervisor-fd 103) against the real Linux runtime."""
import sys
import argparse
import http.client
import io
import json
import os
from pathlib import Path
import select
import socket
import subprocess
import tempfile
import time
import unittest
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from runtime import stop_process, wait_ready, eventually

OPTIONS = None


class SupervisorChannel(unittest.TestCase):
    """The awtrix-tc002d end of --supervisor-fd, played by a SOCK_SEQPACKET socketpair."""

    CLOCK_PROBE = ('class ClockProbe\n def draw()\n  clear()\n'
                   '  if epoch_ms() >= 0 pixel(26, 8, 0x00FF00) else pixel(26, 8, 0xFF0000) end\n'
                   ' end\nend\nreturn ClockProbe()\n')

    def setUp(self):
        self.folder = tempfile.TemporaryDirectory(prefix='awtrix-supervisor-')
        self.addCleanup(self.folder.cleanup)
        self.data = Path(self.folder.name) / 'data'
        self.log = tempfile.TemporaryFile()
        self.addCleanup(self.log.close)
        self.child = None
        self.channel = None

    def start(self, port=None, extra=()):
        ours, theirs = socket.socketpair(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        self.channel = ours
        self.addCleanup(self.close_channel)
        os.dup2(theirs.fileno(), 103)
        theirs.close()
        if port is None:
            with socket.socket() as sock:
                sock.bind(('127.0.0.1', 0))
                port = sock.getsockname()[1]
        self.port = port
        command = [OPTIONS.binary, '--data', str(self.data), '--webui', OPTIONS.webui,
                   '--port', str(self.port), '--supervisor-fd', '103', *extra]
        try:
            self.child = subprocess.Popen(command, stdout=self.log, stderr=self.log, pass_fds=(103,))
        finally:
            os.close(103)
        self.addCleanup(self.stop)

    def close_channel(self):
        if self.channel is not None:
            self.channel.close()
            self.channel = None

    def stop(self):
        # Negative startup cases assert their exit status in the test itself.
        stop_process(self.child, self.output, timeout=5, require_success=False)

    def output(self):
        self.log.seek(0)
        return self.log.read().decode(errors='replace')

    def receive(self, timeout=10):
        if not select.select([self.channel], [], [], timeout)[0]:
            self.fail('no supervisor message; runtime output:\n' + self.output())
        message = json.loads(self.channel.recv(4096))
        self.assertEqual(2, message.pop('v'))
        return message

    def quiet(self, seconds=0.4):
        return not select.select([self.channel], [], [], seconds)[0]

    def send(self, kind, **fields):
        self.channel.send(json.dumps({'v': 2, 'type': kind, **fields}).encode())

    def request(self, method, path, body=None, content_type='application/json', headers=None):
        connection = http.client.HTTPConnection('127.0.0.1', self.port, timeout=4)
        try:
            payload = None if body is None else body if isinstance(body, bytes) else \
                body.encode() if isinstance(body, str) else json.dumps(body).encode()
            request_headers = {'Content-Type': content_type} if payload else {}
            request_headers.update(headers or {})
            connection.request(method, path, body=payload, headers=request_headers)
            response = connection.getresponse()
            raw = response.read()
            try:
                return response.status, json.loads(raw) if raw else None
            except ValueError:
                return response.status, raw
        finally:
            connection.close()

    def get(self, path):
        status, value = self.request('GET', path)
        self.assertEqual(200, status, value)
        return value

    def eventually(self, operation, predicate, timeout=8):
        return eventually(operation, predicate, timeout)

    READY = {'type': 'ready', 'board': 'headless', 'width': 52, 'height': 16, 'input': False}
    DHCP = {'type': 'address', 'static': False, 'ip': '', 'subnet': '', 'gateway': '', 'dns1': '', 'dns2': ''}

    def started(self, *extra):
        self.start(extra=extra)
        hello = self.receive()
        self.assertEqual('hello', hello['type'])
        self.assertEqual({'type': 'ntp', 'server': 'pool.ntp.org'}, self.receive())
        self.assertEqual({'type': 'hostname', 'name': ''}, self.receive())
        self.assertEqual(self.DHCP, self.receive())
        self.assertEqual(self.READY, self.receive())
        return hello

    def test_hello_carries_the_version_then_time_server_hostname_and_address_follow(self):
        hello = self.started()
        self.assertEqual(self.get('/api/v1/version')['version'], hello['version'])
        self.assertTrue(self.quiet())

    def test_ready_comes_once_with_the_listener_bound_and_the_first_frame_shown(self):
        self.started()
        self.assertTrue(self.get('/api/v1/capabilities')['display']['ready'])
        self.assertTrue(self.quiet(1.0), 'readiness is reported once')

    def test_a_listener_that_cannot_bind_never_reports_ready(self):
        with socket.socket() as held:
            held.bind(('127.0.0.1', 0))
            held.listen(1)
            self.start(held.getsockname()[1])
            self.assertEqual(1, self.child.wait(timeout=10), self.output())
        kinds = []
        while select.select([self.channel], [], [], 0.2)[0]:
            datagram = self.channel.recv(4096)
            if not datagram:
                break
            kinds.append(json.loads(datagram)['type'])
        self.assertIn('hello', kinds)
        self.assertNotIn('ready', kinds)

    def test_access_point_setup_guards_the_real_http_server(self):
        self.started('--lan', '--listen', '127.0.0.1')
        self.send('network', link='access-point', ssid='awtrixng-000001', rssi=0,
                  mac='02:00:00:00:00:01', ipv4='192.168.4.1', gateway='', dns='', hostname='awtrixng-000001')
        headers = {'Host': f'192.168.4.1:{self.port}'}
        device = self.eventually(lambda: self.request('GET', '/api/v1/device', headers=headers),
                                 lambda result: result[0] == 200 and result[1]['ipAddress'] == '0.0.0.0')
        self.assertEqual(0, device[1]['wifiRssi'])
        self.assertEqual(302, self.request('GET', '/generate_204')[0])
        for method, path, body in [('POST', '/update', 'not a package'),
                                   ('GET', '/api/v1/system?secrets=true', None),
                                   ('GET', '/device.json', None),
                                   ('GET', '/api/v1/logs', None),
                                   ('PUT', '/api/v1/system', {'authEnabled': False}),
                                   ('POST', '/api/v1/files?dir=/ICONS', 'not an icon')]:
            with self.subTest(method=method, path=path):
                self.assertEqual(403, self.request(method, path, body, headers=headers)[0])
        self.assertEqual(503, self.request('GET', '/api/v1/system/wifi-scan', headers=headers)[0])
        self.assertEqual(200, self.request('PUT', '/api/v1/system',
                         {'wifiSsid': 'Guest', 'wifiPass': ''}, headers=headers)[0])
        self.assertEqual({'type': 'wifi', 'ssid': 'Guest', 'password': ''}, self.receive())
        self.send('network', link='connected', ssid='Guest', rssi=-40, mac='02:00:00:00:00:01',
                  ipv4='192.168.1.20', gateway='192.168.1.1', dns='192.168.1.1', hostname='awtrixng-000001')
        self.eventually(lambda: self.request('GET', '/api/v1/device'),
                        lambda result: result[0] == 200 and result[1]['ipAddress'] == '192.168.1.20')
        self.assertEqual(200, self.request('GET', '/api/v1/logs')[0])

    def test_access_point_setup_restores_a_backup(self):
        self.started('--lan', '--listen', '127.0.0.1')
        self.send('network', link='access-point', ssid='awtrixng-000001', rssi=0,
                  mac='02:00:00:00:00:01', ipv4='192.168.4.1', gateway='', dns='', hostname='awtrixng-000001')
        headers = {'Host': f'192.168.4.1:{self.port}'}
        self.eventually(lambda: self.request('GET', '/api/v1/device', headers=headers),
                        lambda result: result[0] == 200 and result[1]['ipAddress'] == '0.0.0.0')
        archive = io.BytesIO()
        with zipfile.ZipFile(archive, 'w', compression=zipfile.ZIP_STORED) as backup:
            backup.writestr('manifest.json', json.dumps({'app': 'awtrix-ng', 'backupFormat': 1}))
            backup.writestr('config/wifi.json', json.dumps({'wifiSsid': 'Restored', 'wifiPass': 'restored-pass'}))
            backup.writestr('PALETTES/setup.txt', '#123456\n')
        boundary = 'setup-restore-boundary'
        body = (f'--{boundary}\r\nContent-Disposition: form-data; name="file"; filename="backup.zip"\r\n'
                'Content-Type: application/zip\r\n\r\n').encode() + archive.getvalue() + \
            f'\r\n--{boundary}--\r\n'.encode()
        content_type = f'multipart/form-data; boundary={boundary}'
        for refused in ({'Origin': 'https://unrelated.example'}, {'Host': 'foreign.example'}):
            with self.subTest(refused=refused):
                self.assertEqual(403, self.request('POST', '/api/v1/restore', body, content_type,
                                                   {**headers, **refused})[0])
        self.assertEqual(403, self.request('POST', '/api/v1/restore?secrets=1', body, content_type, headers)[0])
        self.assertFalse((self.data / 'PALETTES/setup.txt').exists())
        self.assertTrue(self.quiet())
        status, result = self.request('POST', '/api/v1/restore', body, content_type,
                                      {**headers, 'Origin': f'http://192.168.4.1:{self.port}'})
        self.assertEqual(200, status, result)
        self.assertTrue(result['ok'], result)
        self.assertEqual((1, 1), (result['applied']['wifi'], result['applied']['palettes']), result)
        self.assertEqual({'type': 'wifi', 'ssid': 'Restored', 'password': 'restored-pass'}, self.receive())
        self.assertEqual('#123456\n', (self.data / 'PALETTES/setup.txt').read_text())
        self.assertEqual(403, self.request('GET', '/PALETTES/setup.txt', headers=headers)[0])

    def test_access_point_keeps_an_existing_admin_login(self):
        self.data.mkdir()
        (self.data / 'device.json').write_text(json.dumps({'authEnabled': True,
            'authUser': 'owner', 'authPass': 'secret'}))
        self.started('--lan', '--listen', '127.0.0.1')
        self.send('network', link='access-point', ssid='awtrixng-000001', rssi=0,
                  mac='02:00:00:00:00:01', ipv4='192.168.4.1', gateway='', dns='', hostname='awtrixng-000001')
        headers = {'Host': f'192.168.4.1:{self.port}'}
        self.eventually(lambda: self.request('GET', '/api/v1/device', headers=headers),
                        lambda result: result[0] == 401)
        headers['Authorization'] = 'Basic b3duZXI6c2VjcmV0'
        self.assertEqual(200, self.request('GET', '/api/v1/system', headers=headers)[0])
        self.assertEqual(403, self.request('GET', '/api/v1/system?secrets=true', headers=headers)[0])

    def test_power_network_and_time_reach_device_state_and_applications(self):
        self.started()
        device = self.get('/api/v1/device')
        self.assertNotIn('batteryPercent', device)
        self.assertEqual(0, device['wifiRssi'])
        self.assertNotIn('"Battery"', json.dumps(self.get('/api/v1/apps')))

        self.send('power', usbPower=True, batteryPercent=76, batteryMillivolts=4012)
        device = self.eventually(lambda: self.get('/api/v1/device'), lambda value: 'batteryPercent' in value)
        self.assertEqual(76, device['batteryPercent'])
        self.assertAlmostEqual(4.01, device['batteryVoltage'], places=2)
        self.assertFalse(device['lowBattery'])
        for absent in ('temperature', 'humidity', 'pressureHpa', 'lightLevel'):
            self.assertNotIn(absent, device)
        self.assertIn('"Battery"', json.dumps(self.get('/api/v1/apps')))

        self.send('network', link='connected', ssid='Home', rssi=-61, mac='02:00:00:00:00:07',
                  ipv4='192.0.2.10', gateway='192.0.2.1', dns='192.0.2.1', hostname='awtrix-000007')
        device = self.eventually(lambda: self.get('/api/v1/device'), lambda value: value['wifiRssi'] == -61)
        self.assertEqual('192.0.2.10', device['ipAddress'])
        self.assertEqual('awtrix-000007', device['hostname'])
        self.assertEqual('connected', device['wifi']['state'])
        self.assertEqual('Home', device['wifi']['host'])
        self.assertEqual('192.0.2.10', device['wifi']['endpoint'])
        self.assertEqual('Home', self.get('/api/v1/system')['wifiSsid'])
        self.assertTrue(self.quiet(), 'adopting the SSID must not echo a wifi message')

        self.send('network', link='failed', ssid='Home', rssi=0, mac='02:00:00:00:00:07',
                  ipv4='', gateway='', dns='', hostname='awtrix-000007')
        device = self.eventually(lambda: self.get('/api/v1/device'),
                                 lambda value: value['wifi']['state'] == 'offline')
        self.assertEqual('badCredentials', device['wifi']['error'])
        self.assertEqual(0, device['wifiRssi'])

        self.assertEqual(200, self.request('PUT', '/api/v1/apps/script/clockprobe', self.CLOCK_PROBE, 'text/plain')[0])
        self.assertEqual(200, self.request('PUT', '/api/v1/apps/active', {'name': 'clockprobe', 'fast': True})[0])
        pixel = lambda: self.get('/api/v1/display/screen')['pixels'][8 * 52 + 26]
        self.eventually(pixel, lambda value: value == 0xFF0000)
        self.send('time', synchronized=True)
        self.eventually(pixel, lambda value: value == 0x00FF00)
        self.send('time', synchronized=False)
        time.sleep(0.3)
        self.assertEqual(0x00FF00, pixel(), 'a set clock stays set, as on ESP32')

    def test_supervisor_log_lines_join_the_log(self):
        self.started()
        self.send('log', component='wifi', text='association rejected: wrong key')
        logs = self.eventually(lambda: self.get('/api/v1/logs'),
                               lambda value: any('wifi: association rejected: wrong key' in line
                                                 for line in value['lines']))
        self.assertEqual(1, sum('association rejected' in line for line in logs['lines']))
        self.send('log', component='Bad Name', text='refused')
        time.sleep(0.3)
        self.assertFalse(any('refused' in line for line in self.get('/api/v1/logs')['lines']))

    def test_configuration_changes_are_forwarded_and_the_wifi_password_is_not_stored(self):
        self.started()
        status, value = self.request('PUT', '/api/v1/system', {'wifiSsid': 'Cafe', 'wifiPass': 'correct horse'})
        self.assertEqual(200, status, value)
        self.assertEqual({'type': 'wifi', 'ssid': 'Cafe', 'password': 'correct horse'}, self.receive())
        self.assertTrue(self.quiet())
        stored = (self.data / 'device.json').read_text()
        self.assertNotIn('correct horse', stored)
        self.assertEqual('Cafe', json.loads(stored)['wifiSsid'])
        secrets = self.get('/api/v1/system?secrets=1')
        self.assertEqual('Cafe', secrets['wifiSsid'])
        self.assertFalse(secrets.get('wifiPass'))

        status, value = self.request('PUT', '/api/v1/system', {'wifiSsid': 'Cafe-5G', 'wifiPass': ''})
        self.assertEqual((200, 'Cafe'), (status, value['wifiSsid']), 'a new network needs its password')
        self.assertTrue(self.quiet(), 'a blank password is not sent as an open network')
        self.assertEqual('Cafe', json.loads((self.data / 'device.json').read_text())['wifiSsid'])

        psk = '0123456789abcdef' * 4
        status, value = self.request('PUT', '/api/v1/system', {'wifiSsid': 'Office', 'wifiPass': psk})
        self.assertEqual((200, 'Office'), (status, value['wifiSsid']))
        self.assertEqual({'type': 'wifi', 'ssid': 'Office', 'password': psk}, self.receive())
        status, value = self.request('PUT', '/api/v1/system', {'wifiSsid': 'Lab', 'wifiPass': 'short'})
        self.assertEqual((200, 'Office'), (status, value['wifiSsid']), 'a refused password keeps the network')
        self.assertTrue(self.quiet())

        self.assertEqual(200, self.request('PUT', '/api/v1/system', {'ntpServer': 'de.pool.ntp.org'})[0])
        self.assertEqual({'type': 'ntp', 'server': 'de.pool.ntp.org'}, self.receive())
        self.assertEqual(200, self.request('PUT', '/api/v1/system', {'hostname': 'kitchen'})[0])
        self.assertEqual({'type': 'hostname', 'name': 'kitchen'}, self.receive())
        self.assertEqual(200, self.request('PUT', '/api/v1/system', {'hostname': 'kitchen', 'wifiSsid': 'Office'})[0])
        self.assertTrue(self.quiet(), 'an unchanged configuration sends nothing')

        fixed = {'netStatic': True, 'ip': '192.168.1.50/24', 'gateway': '192.168.1.1'}
        self.assertEqual(200, self.request('PUT', '/api/v1/system', fixed)[0])
        self.assertEqual({'type': 'address', 'static': True, 'ip': '192.168.1.50', 'subnet': '255.255.255.0',
                          'gateway': '192.168.1.1', 'dns1': '', 'dns2': ''}, self.receive())
        self.assertEqual(200, self.request('PUT', '/api/v1/system', {'netStatic': False})[0])
        self.assertEqual(self.DHCP, self.receive())

    def test_a_stored_password_is_dropped_at_start_and_the_runtime_settings_are_sent(self):
        self.data.mkdir(mode=0o700)
        (self.data / 'device.json').write_text(json.dumps(
            {'wifiSsid': 'Old', 'wifiPass': 'stale secret', 'ntpServer': 'time.example', 'hostname': 'hall'}))
        self.start()
        self.assertEqual('hello', self.receive()['type'])
        self.assertEqual({'type': 'ntp', 'server': 'time.example'}, self.receive())
        self.assertEqual({'type': 'hostname', 'name': 'hall'}, self.receive())
        self.assertEqual(self.DHCP, self.receive())
        self.assertEqual(self.READY, self.receive())
        self.assertTrue(self.quiet(), 'stored credentials are never forwarded')
        self.assertNotIn('stale secret', (self.data / 'device.json').read_text())
        self.assertIn('"Old"', (self.data / 'device.json').read_text())

    def test_reboot_is_requested_after_state_is_saved_and_the_process_stops(self):
        self.started()
        self.assertEqual(200, self.request('POST', '/api/v1/device/reboot')[0])
        self.assertEqual({'type': 'reboot'}, self.receive())
        self.assertEqual(0, self.child.wait(timeout=10), self.output())

    def test_wifi_scan_answers_like_the_device(self):
        self.started()
        self.assertEqual((202, {'scanning': True}), self.request('GET', '/api/v1/system/wifi-scan'))
        self.assertEqual({'type': 'wifiScan'}, self.receive())
        self.assertEqual((202, {'scanning': True}), self.request('GET', '/api/v1/system/wifi-scan'))
        self.assertTrue(self.quiet(), 'a running scan is not requested twice')
        self.send('wifiScanResult', networks=[{'ssid': 'Home', 'rssi': -52, 'secure': True},
                                              {'ssid': '', 'rssi': -80, 'secure': True},
                                              {'ssid': 'Guest', 'rssi': -71, 'secure': False}])
        status, networks = self.eventually(lambda: self.request('GET', '/api/v1/system/wifi-scan'),
                                           lambda result: result[0] == 200)
        self.assertEqual([{'ssid': 'Home', 'rssi': -52, 'enc': True}, {'ssid': '', 'rssi': -80, 'enc': True},
                          {'ssid': 'Guest', 'rssi': -71, 'enc': False}], networks)
        self.assertEqual((202, {'scanning': True}), self.request('GET', '/api/v1/system/wifi-scan'))
        self.assertEqual({'type': 'wifiScan'}, self.receive(), 'a delivered result is consumed')
        self.send('wifiScanResult', networks=[])
        self.assertEqual([], self.eventually(lambda: self.request('GET', '/api/v1/system/wifi-scan'),
                                             lambda result: result[0] == 200)[1])
        self.send('wifiScanResult', networks=[{'ssid': 'Late', 'rssi': -40, 'secure': True}])
        time.sleep(0.2)
        self.assertEqual((202, {'scanning': True}), self.request('GET', '/api/v1/system/wifi-scan'),
                         'an unrequested result is not served')
        self.assertEqual({'type': 'wifiScan'}, self.receive())

    def test_factory_reset_also_erases_the_supervisor_credentials(self):
        self.started()
        self.assertEqual(200, self.request('PUT', '/api/v1/system', {'hostname': 'kitchen'})[0])
        self.assertEqual({'type': 'hostname', 'name': 'kitchen'}, self.receive())
        self.assertEqual(200, self.request('POST', '/api/v1/device/factory-reset', {})[0])
        self.assertEqual(0, self.child.wait(timeout=10), self.output())
        self.assertEqual({'type': 'factoryReset'}, self.receive())
        self.assertEqual(['.lock'], [entry.name for entry in self.data.iterdir()])
        self.assertTrue(self.quiet(0.1) or self.channel.recv(4096) == b'')

    def test_settings_reset_keeps_the_network(self):
        self.started()
        self.assertEqual(200, self.request('POST', '/api/v1/settings/reset')[0])
        self.assertEqual(0, self.child.wait(timeout=10), self.output())
        self.assertTrue(self.quiet(0.1) or self.channel.recv(4096) == b'')

    def test_other_restarting_actions_stop_without_a_reboot_request(self):
        self.started()
        self.assertEqual(200, self.request('POST', '/api/v1/device/sleep', {'durationMs': 5000})[0])
        self.assertEqual(0, self.child.wait(timeout=10), self.output())
        self.assertTrue(self.quiet(0.1) or self.channel.recv(4096) == b'')

    def test_closing_the_channel_stops_the_runtime_in_order(self):
        self.started()
        self.close_channel()
        self.assertEqual(0, self.child.wait(timeout=10), self.output())
        self.assertIn('Supervisor channel closed', self.output())

    def test_the_device_id_comes_from_the_supervisor(self):
        self.started('--uid', 'a4cf12ab34cd', '--start-reason', 'software')
        device = self.get('/api/v1/device')
        self.assertEqual('a4cf12ab34cd', device['uid'])
        self.assertEqual('software', device['resetReason'])
        self.assertFalse((self.data / 'identity').exists())

    def test_without_a_device_id_the_runtime_keeps_none(self):
        self.started()
        uid = self.get('/api/v1/device')['uid']
        self.assertRegex(uid, '^[0-9a-f]{16}$')
        self.assertFalse((self.data / 'identity').exists())

    def test_start_facts_need_the_supervisor_and_valid_values(self):
        for arguments in (['--uid', 'a4cf12ab34cd'], ['--start-reason', 'poweron'],
                          ['--supervisor-fd', '103', '--uid', 'A4CF12AB34CD'],
                          ['--supervisor-fd', '103', '--uid', 'a4cf12ab34c'],
                          ['--supervisor-fd', '103', '--uid', 'a4cf12ab34cd', '--uid', 'a4cf12ab34cd'],
                          ['--supervisor-fd', '103', '--start-reason', 'reboot']):
            with self.subTest(arguments=arguments):
                result = subprocess.run([OPTIONS.binary, '--data', str(self.data), '--webui', OPTIONS.webui,
                                         *arguments], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=10)
                self.assertEqual(2, result.returncode)
                if arguments[0] != '--supervisor-fd':
                    self.assertIn(arguments[0].encode() + b' requires --supervisor-fd', result.stdout)
        self.assertFalse(self.data.exists())

    def test_unusable_descriptors_are_refused_before_startup(self):
        for arguments in (['--supervisor-fd', '104'], ['--supervisor-fd', '103', '--supervisor-fd', '103']):
            with self.subTest(arguments=arguments):
                result = subprocess.run([OPTIONS.binary, '--data', str(self.data), '--webui', OPTIONS.webui,
                                         *arguments], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=10)
                self.assertEqual(2, result.returncode)
        read, write = os.pipe()
        os.dup2(read, 103)
        os.close(read)
        try:
            result = subprocess.run([OPTIONS.binary, '--data', str(self.data), '--webui', OPTIONS.webui,
                                     '--supervisor-fd', '103'], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                    pass_fds=(103,), timeout=10)
        finally:
            os.close(103)
            os.close(write)
        self.assertEqual(2, result.returncode)
        self.assertIn(b'Supervisor channel refused', result.stdout)
        self.assertFalse(self.data.exists())

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--binary', required=True)
    parser.add_argument('--webui', required=True)
    OPTIONS, arguments = parser.parse_known_args()
    unittest.main(argv=[__file__, *arguments])
