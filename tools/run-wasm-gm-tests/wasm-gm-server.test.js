const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const { createHash, randomBytes } = crypto;
const { once } = require('node:events');
const fs = require('node:fs');
const http = require('node:http');
const path = require('node:path');
const { test } = require('node:test');
const { startServer } = require('./wasm-gm-server');

const png = Buffer.from(
  'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+ip1sAAAAASUVORK5CYII=',
  'base64');
const digest = createHash('md5').update(png).digest('hex');
const wasm = Buffer.from([0, 97, 115, 109, 1, 0, 0, 0]);
const javascript = 'const example = "<img src=x onerror=alert(1)>";';
const hashes = digest + '\n<img src=x onerror=alert(1)>\n';

async function fixture(t, resourceNames = ['nested/image.png']) {
  const root = path.join(__dirname, '.server-test-' + randomBytes(16).toString('hex'));
  fs.mkdirSync(root);
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const resources = path.join(root, 'resources');
  const output = path.join(root, 'output');
  fs.mkdirSync(resources);
  fs.mkdirSync(output);
  for (const name of resourceNames) {
    const filename = path.join(resources, name);
    fs.mkdirSync(path.dirname(filename), { recursive: true });
    fs.writeFileSync(filename, png);
  }
  const options = {
    resources,
    output,
    port: 0,
    js_file: path.join(root, 'tests.js'),
    wasm_file: path.join(root, 'tests.wasm'),
    known_hashes: path.join(root, 'hashes.txt'),
  };
  fs.writeFileSync(options.js_file, javascript);
  fs.writeFileSync(options.wasm_file, wasm);
  fs.writeFileSync(options.known_hashes, hashes);
  const server = startServer(options);
  t.after(() => new Promise((resolve, reject) => server.close((err) => {
    if (err) {
      reject(err);
    } else {
      resolve();
    }
  })));
  await once(server, 'listening');

  const request = (url, md5, body = png) => new Promise((resolve, reject) => {
    const headers = {};
    if (md5 !== undefined) {
      headers['X-MD5-Hash'] = md5;
    }
    if (url === '/write_png') {
      headers['Content-Type'] = 'image/png';
    }
    const req = http.request({
      host: '127.0.0.1',
      port: server.address().port,
      path: url,
      method: url === '/write_png' ? 'POST' : 'GET',
      headers,
    }, (res) => {
      const chunks = [];
      res.on('data', (chunk) => chunks.push(chunk));
      res.on('end', () => resolve({
        status: res.statusCode,
        type: res.headers['content-type'],
        body: Buffer.concat(chunks),
      }));
      res.on('error', reject);
    });
    req.on('error', reject);
    req.end(url === '/write_png' ? body : undefined);
  });
  return { root, resources, output, server, request };
}

test('listens only on IPv4 loopback', async (t) => {
  const { server, request } = await fixture(t);
  assert.equal(server.address().address, '127.0.0.1');
  assert.equal((await request('/')).status, 200);
});

test('resource filenames round-trip as JSON, not executable HTML', async (t) => {
  // Windows does not allow these filename characters, but their serialized data is portable.
  const names = process.platform === 'win32' ?
    ['nested/quotes-and-unicode-\u2603.png'] :
    ['nested/<img src=x onerror=alert(1)>.png', 'quotes-"\\&\u2603.png'];
  const { request } = await fixture(t, names);
  const response = await request('/static/resource_listing.json');
  assert.equal(response.status, 200);
  assert.match(response.type, /^application\/json\b/);
  const expected = names.map((name) => path.normalize(name)).sort();
  assert.deepEqual(JSON.parse(response.body.toString()).sort(), expected);
});

test('driver HTML, string assets, WASM and static resources retain their bytes and types',
    async (t) => {
  const { request } = await fixture(t);
  for (const [url, type, expected] of [
    ['/', /^text\/html\b/,
      fs.readFileSync(path.join(__dirname, 'run-wasm-gm-tests.html'))],
    ['/static/hashes.txt', /^text\/plain\b/, Buffer.from(hashes)],
    ['/static/wasm_gm_tests.js', /^(application|text)\/javascript\b/, Buffer.from(javascript)],
    ['/static/wasm_gm_tests.wasm', /^application\/wasm\b/, wasm],
    ['/static/resources/nested/image.png', /^image\/png\b/, png],
  ]) {
    const response = await request(url);
    assert.equal(response.status, 200, url);
    assert.match(response.type, type, url);
    assert.deepEqual(response.body, expected, url);
  }
  assert.equal((await request('/static/resources/../tests.js')).status, 404);
});

test('writes PNG bytes for a valid digest, including duplicate output', async (t) => {
  const { output, request } = await fixture(t);
  for (let i = 0; i < 2; i++) {
    assert.equal((await request('/write_png', digest)).status, 200);
    assert.deepEqual(fs.readFileSync(path.join(output, digest + '.png')), png);
  }
  assert.deepEqual(fs.readdirSync(output), [digest + '.png']);
});

test('accepts uppercase hexadecimal digests without changing the filename', async (t) => {
  const { output, request } = await fixture(t);
  const uppercaseDigest = digest.toUpperCase();
  assert.equal((await request('/write_png', uppercaseDigest)).status, 200);
  assert.deepEqual(fs.readFileSync(path.join(output, uppercaseDigest + '.png')), png);
  assert.deepEqual(fs.readdirSync(output), [uppercaseDigest + '.png']);
});

test('rejects missing, malformed and path-like digest headers without writing files', async (t) => {
  const { root, output, request } = await fixture(t);
  const sentinel = path.join(root, 'outside.png');
  fs.writeFileSync(sentinel, 'unchanged');
  const invalid = [
    undefined, '', 'abc', 'a'.repeat(31), 'a'.repeat(33), 'g'.repeat(32),
    'a'.repeat(15) + ' ' + 'b'.repeat(16),
    '../outside', '..\\outside', '/absolute', path.join(root, 'absolute'),
    'C:\\absolute', 'C:/absolute',
    '\\\\server\\share\\outside', digest + '/..', digest + '\\..', digest + '.png',
    '../' + 'a'.repeat(29), '..\\' + 'a'.repeat(29), '/' + 'a'.repeat(31),
    digest + ',' + digest, [digest, digest],
  ];
  for (const md5 of invalid) {
    await t.test('header ' + JSON.stringify(md5), async () => {
      const response = await request('/write_png', md5);
      assert.equal(response.status, 400);
      assert.match(response.type, /^text\/plain\b/);
      assert.match(response.body.toString(), /X-MD5-Hash.*32 hexadecimal/);
      assert.deepEqual(fs.readdirSync(output), []);
      assert.equal(fs.readFileSync(sentinel, 'utf8'), 'unchanged');
    });
  }
});

function symlinkOrSkip(t, target, link) {
  try {
    fs.symlinkSync(target, link);
    return true;
  } catch (err) {
    if (process.platform === 'win32' &&
        ['EPERM', 'EACCES', 'ENOSYS', 'ENOTSUP'].includes(err.code)) {
      t.skip('Creating file symlinks is not permitted on this Windows host.');
      return false;
    }
    throw err;
  }
}

test('replaces an output symlink without modifying its external target', async (t) => {
  const { root, output, request } = await fixture(t);
  const target = path.join(root, 'outside.png');
  const destination = path.join(output, digest + '.png');
  fs.writeFileSync(target, 'unchanged');
  if (!symlinkOrSkip(t, target, destination)) {
    return;
  }
  assert.equal((await request('/write_png', digest)).status, 200);
  assert.equal(fs.readFileSync(target, 'utf8'), 'unchanged');
  assert.equal(fs.lstatSync(destination).isSymbolicLink(), false);
  assert.deepEqual(fs.readFileSync(destination), png);
  assert.deepEqual(fs.readdirSync(output), [digest + '.png']);
});

test('does not follow or remove a pre-existing staging symlink on a name collision', async (t) => {
  const { root, output, request } = await fixture(t);
  const target = path.join(root, 'outside.png');
  const random = Buffer.alloc(16, 1);
  const stagingName = '.wasm-gm-' + random.toString('hex');
  const stagingFile = path.join(output, stagingName);
  fs.writeFileSync(target, 'unchanged');
  if (!symlinkOrSkip(t, target, stagingFile)) {
    return;
  }
  const mock = t.mock.method(crypto, 'randomBytes', () => random);
  try {
    assert.equal((await request('/write_png', digest)).status, 500);
  } finally {
    mock.mock.restore();
  }
  assert.equal(fs.readFileSync(target, 'utf8'), 'unchanged');
  assert.equal(fs.lstatSync(stagingFile).isSymbolicLink(), true);
  assert.deepEqual(fs.readdirSync(output), [stagingName]);
});

test('reports output write failures instead of acknowledging a saved image', async (t) => {
  const { output, request } = await fixture(t);
  fs.rmdirSync(output);
  assert.equal((await request('/write_png', digest)).status, 500);
  assert.equal(fs.existsSync(output), false);
});

test('cleans up its staging file when replacing the destination fails', async (t) => {
  const { output, request } = await fixture(t);
  const destination = path.join(output, digest + '.png');
  fs.mkdirSync(destination);
  fs.writeFileSync(path.join(destination, 'keep.txt'), 'unchanged');
  assert.equal((await request('/write_png', digest)).status, 500);
  assert.deepEqual(fs.readdirSync(output), [digest + '.png']);
  assert.equal(fs.readFileSync(path.join(destination, 'keep.txt'), 'utf8'), 'unchanged');
});

test('cleans up a partially written staging file and preserves previous output', async (t) => {
  const { output, request } = await fixture(t);
  const destination = path.join(output, digest + '.png');
  fs.writeFileSync(destination, png);
  const writeFileSync = fs.writeFileSync;
  const mock = t.mock.method(fs, 'writeFileSync', (file, data, options) => {
    writeFileSync(file, data.subarray(0, 8), options);
    throw Object.assign(new Error('Simulated disk write failure'), { code: 'EIO' });
  });
  try {
    assert.equal((await request('/write_png', digest)).status, 500);
  } finally {
    mock.mock.restore();
  }
  assert.deepEqual(fs.readFileSync(destination), png);
  assert.deepEqual(fs.readdirSync(output), [digest + '.png']);
});
