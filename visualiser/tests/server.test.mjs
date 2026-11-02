import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { once } from 'node:events';
import http from 'node:http';
import net from 'node:net';
import { test } from 'node:test';
import { fileURLToPath } from 'node:url';

const script = fileURLToPath(new URL('../serve.mjs', import.meta.url));

async function startGuide(t) {
  const reservation = net.createServer();
  reservation.listen(0, '127.0.0.1');
  await once(reservation, 'listening');
  const { port } = reservation.address();
  await new Promise(resolve => reservation.close(resolve));
  const child = spawn(process.execPath, [script], {
    env: {...process.env, VISUALISER_PORT: String(port)}, windowsHide: true,
    stdio: ['ignore', 'pipe', 'pipe'],
  });
  let output = '';
  const finished = once(child, 'exit');
  t.after(async () => {
    if (child.exitCode === null) child.kill();
    await finished;
  });
  await new Promise((resolve, reject) => {
    child.on('error', reject);
    child.on('exit', code => reject(new Error(`Server exited (${code}): ${output}`)));
    child.stderr.on('data', data => { output += data; });
    child.stdout.on('data', data => {
      output += data;
      if (output.includes(`Visualiser ready: http://127.0.0.1:${port}`)) resolve();
    });
  });
  return port;
}

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
  const port = await startGuide(t);
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
