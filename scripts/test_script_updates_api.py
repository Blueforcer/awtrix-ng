"""Explicitly targeted HTTP regression; creates and removes only its own test script."""
import argparse
import json
import urllib.error
import urllib.request

parser = argparse.ArgumentParser()
parser.add_argument('--device', required=True, help='Explicit device or simulator origin')
args = parser.parse_args()
base = args.device.rstrip('/')
name = 'HubUpdateRegression'

def request(method, path, body=None, expected=200):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(base + path, data=data, method=method,
                                 headers={'Content-Type': 'application/json'})
    try:
        response = urllib.request.urlopen(req, timeout=20)
    except urllib.error.HTTPError as error:
        response = error
    with response:
        raw = response.read()
        assert response.status == expected, (path, response.status, raw)
        return raw

def update(before, after, expected=200):
    return request('PUT', '/api/v1/apps/script-update/' + name,
                   {'expected_source': before, 'source': after}, expected)

def source():
    return request('GET', '/api/v1/apps/script/' + name).decode()

apps = json.loads(request('GET', '/api/v1/apps'))
assert all(a['name'] != name for a in apps), 'Test name already exists; refusing to overwrite'
assert json.loads(request('GET', '/api/v1/capabilities'))['scriptUpdates']
previous = {a['name']: request('GET', '/api/v1/apps/script/' + a['name'])
            for a in apps if a['origin'] in ('script', 'module')}
v1 = "# @config city text default=Rom\nclass App\n def draw() end\nend\nreturn App()\n"
v2 = v1 + '# release 2\n'
created = False
try:
    update(None, v1)
    created = True
    assert source() == v1
    update(None, v2, 409)
    request('PATCH', '/api/v1/apps/' + name + '/config', {'city': 'Berlin'})
    update(v1, v2)
    assert source() == v2
    config = json.loads(request('GET', '/api/v1/apps/' + name + '/config'))
    assert 'Berlin' in json.dumps(config), config
    update(v1, v1, 409)
    assert source() == v2
    update(v2, 'class Broken\n def draw( end', 422)
    assert source() == v2
    bad_setup = "class App\n def init() raise 'test', 'broken setup' end\n def draw() end\nend\nreturn App()"
    update(v2, bad_setup, 422)
    assert source() == v2
    state = json.loads(request('GET', '/api/v1/apps'))
    assert not next(a for a in state if a['name'] == name).get('error')
    assert 'Berlin' in request('GET', '/api/v1/apps/' + name + '/config').decode()
    print('PASS: create-only collision, settings preservation, stale source, syntax rollback, startup rollback')
finally:
    if created:
        request('DELETE', '/api/v1/apps/' + name)
        request('PUT', '/api/v1/apps/order', {
            'order': [a['name'] for a in sorted(apps, key=lambda a: a.get('slot') or 0) if a.get('inLoop')],
            'disabled': [a['name'] for a in apps if not a.get('enabled', True)]})
    for original_name, original_source in previous.items():
        assert request('GET', '/api/v1/apps/script/' + original_name) == original_source
    assert all(a['name'] != name for a in json.loads(request('GET', '/api/v1/apps')))
    print('PASS: test script removed; existing scripts unchanged')
