/** Typed access to the localhost review API in tools/configurator/web.py.
 *
 * Every endpoint lives here so no view knows a URL. A validation failure is a
 * normal result (the body carries `errors`), not an exception; anything else
 * rejects with the server's message.
 */

async function request(path, body) {
  const response = await fetch(path, {
    method: body === undefined ? 'GET' : 'POST',
    headers: body === undefined ? {} : { 'Content-Type': 'application/json' },
    body: body === undefined ? undefined : JSON.stringify(body),
  });

  const data = await response.json();
  if (!response.ok && !Array.isArray(data.errors)) {
    throw new Error(data.error || `Request to ${path} failed.`);
  }
  return data;
}

export const api = {
  sampleProfile: () => request('/api/sample/profile'),
  sampleHealth: () => request('/api/sample/health'),
  generate: (recommendation) => request('/api/generate', recommendation),
  validate: (profile) => request('/api/validate', profile),
  createCandidate: (profile) => request('/api/candidate', profile),
  review: (decision, payload) => request(`/api/${decision}`, payload),
  normaliseHealth: (snapshot) => request('/api/health', snapshot),
};
