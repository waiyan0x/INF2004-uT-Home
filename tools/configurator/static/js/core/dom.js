/** DOM helpers and an escaping HTML template tag.
 *
 * Interpolations are escaped by default, so a view cannot accidentally inject
 * profile content as markup. Nested templates and `raw()` opt out explicitly.
 */

const RAW = Symbol('raw');

export const $ = (selector, root = document) => root.querySelector(selector);

export function escapeHtml(value) {
  return String(value).replace(/[&<>"']/g, (character) => ({
    '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;',
  }[character]));
}

/** Mark a string as already-safe markup. */
export const raw = (value) => ({ [RAW]: String(value) });

function serialise(value) {
  if (value === null || value === undefined || value === false) return '';
  if (Array.isArray(value)) return value.map(serialise).join('');
  if (typeof value === 'object' && RAW in value) return value[RAW];
  return escapeHtml(value);
}

/** Tagged template that escapes every interpolation and returns safe markup. */
export function html(strings, ...values) {
  const markup = strings.reduce(
    (out, chunk, index) => out + chunk + (index < values.length ? serialise(values[index]) : ''),
    '',
  );
  return raw(markup);
}

/** Replace an element's contents with safe markup. */
export function render(target, content) {
  if (target) target.innerHTML = serialise(content);
}

/** Set a control's disabled state, tolerating a missing element. */
export function setDisabled(selector, disabled) {
  const element = $(selector);
  if (element) element.disabled = disabled;
}
