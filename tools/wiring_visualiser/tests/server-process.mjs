import { spawn } from 'node:child_process';
import { once } from 'node:events';
import net from 'node:net';
import { fileURLToPath } from 'node:url';

const script = fileURLToPath(new URL('../serve.mjs', import.meta.url));

// Callers own cleanup; the helper stops its child if startup fails.
export async function startGuide(scriptPath = script) {
  const reservation = net.createServer();
  reservation.listen(0, '127.0.0.1');
  await once(reservation, 'listening');
  const { port } = reservation.address();
  await new Promise(resolve => reservation.close(resolve));
  const child = spawn(process.execPath, [scriptPath], {
    env: {...process.env, VISUALISER_PORT: String(port)}, windowsHide: true,
    stdio: ['ignore', 'pipe', 'pipe'],
  });
  let output = '', errors = '';
  const closed = new Promise(resolve => child.once('close', resolve));
  const stop = async () => {
    if (child.exitCode === null && child.signalCode === null) child.kill();
    await closed;
  };
  try {
    await new Promise((resolve, reject) => {
      const timer = setTimeout(() => reject(new Error(`Server startup timed out: ${output}`)), 10000);
      const finish = error => {
        clearTimeout(timer);
        if (error) reject(error);
        else resolve();
      };
      child.once('error', finish);
      child.once('exit', code => finish(new Error(`Server exited (${code}): ${output}`)));
      child.stderr.on('data', data => { errors += data; output += data; });
      child.stdout.on('data', data => {
        output += data;
        if (output.includes(`Visualiser ready: http://127.0.0.1:${port}`)) finish();
      });
    });
  } catch (error) {
    await stop();
    throw error;
  }
  return {port, stop, diagnostics: () => errors};
}
