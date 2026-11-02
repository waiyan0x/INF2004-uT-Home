import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { once } from 'node:events';
import { copyFile, mkdir, mkdtemp, rm, writeFile } from 'node:fs/promises';
import http from 'node:http';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { test } from 'node:test';
import { fileURLToPath } from 'node:url';
import { startGuide } from './server-process.mjs';

const script = fileURLToPath(new URL('../serve.mjs', import.meta.url));

function request(port, path, method = 'GET') {
  return new Promise((resolve, reject) => {
    const req = http.request({host: '127.0.0.1', port, path, method}, res => {
      let body = '';
      res.setEncoding('utf8');
      res.on('data', data => { body += data; });
      res.on('end', () => resolve({status: res.statusCode, headers: res.headers, body}));
      res.on('error', reject);
    });
    req.on('error', reject);
    req.end();
  });
}

test('the loopback server bounds methods and paths and survives malformed URLs', {timeout: 15000}, async t => {
  const server = await startGuide();
  t.after(server.stop);
  const {port} = server;
  for (const [path, type] of [['/', 'text/html'], ['/index.html?view=focus', 'text/html'],
    ['/style.css', 'text/css'], ['/app.mjs', 'text/javascript'],
    ['/diagram.mjs', 'text/javascript'], ['/plan.mjs', 'text/javascript']]) {
    const response = await request(port, path);
    assert.equal(response.status, 200, path);
    assert.ok(response.headers['content-type'].startsWith(type));
    assert.equal(response.headers['cache-control'], 'no-store');
    assert.equal(response.headers['x-content-type-options'], 'nosniff');
    assert.match(response.headers['content-security-policy'], /connect-src 'none'/);
    assert.ok(response.body.length > 0);
  }
  const head = await request(port, '/', 'HEAD');
  assert.equal(head.status, 200);
  assert.equal(head.body, '');
  for (const path of ['/README.md', '/.git/config', '/%2e%2e/README.md', '/missing']) {
    assert.equal((await request(port, path)).status, 404, path);
  }
  const method = await request(port, '/', 'POST');
  assert.equal(method.status, 405);
  assert.equal(method.headers.allow, 'GET, HEAD');
  assert.equal((await request(port, '//[')).status, 400);
  assert.equal((await request(port, '/')).status, 200, 'malformed input must not terminate the server');
});

test('missing assets retain local diagnostics and recover without exposing paths', {timeout: 15000}, async t => {
  const temporaryRoot = resolve(tmpdir());
  const directory = await mkdtemp(join(temporaryRoot, 'uthome-server-'));
  assert.equal(dirname(resolve(directory)), temporaryRoot);
  let server;
  t.after(async () => {
    await server?.stop();
    await rm(directory, {recursive: true, force: true});
  });
  const copy = join(directory, 'serve.mjs');
  await copyFile(script, copy);
  await mkdir(join(directory, 'dist'));
  server = await startGuide(copy);
  const failure = await request(server.port, '/');
  assert.equal(failure.status, 500);
  assert.equal(failure.body, 'Unable to read visualiser file');
  assert.ok(!failure.body.includes(directory));
  await writeFile(join(directory, 'dist', 'index.html'), '<p>Available again</p>');
  const recovered = await request(server.port, '/');
  assert.equal(recovered.status, 200);
  assert.equal(recovered.body, '<p>Available again</p>');
  await server.stop();
  assert.match(server.diagnostics(), /index\.html/);
  assert.match(server.diagnostics(), /ENOENT/);
});

test('invalid configured ports fail before listening', async () => {
  for (const port of ['1023', '65536', '4173.5', 'invalid']) {
    const child = spawn(process.execPath, [script], {
      env: {...process.env, VISUALISER_PORT: port}, windowsHide: true,
      stdio: ['ignore', 'ignore', 'pipe'],
    });
    let error = '';
    child.stderr.on('data', data => { error += data; });
    const [code] = await once(child, 'exit');
    assert.notEqual(code, 0);
    assert.match(error, /VISUALISER_PORT must be an integer/);
  }
});
