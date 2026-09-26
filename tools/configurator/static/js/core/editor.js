/** Thin wrapper over the profile textarea.
 *
 * The textarea owns its own text (re-rendering it from state on every keystroke
 * would fight the caret), so actions read and write it through this module
 * instead of touching the DOM directly.
 */

import { $ } from './dom.js';

const element = () => $('#editor');

export const getText = () => element()?.value ?? '';

export const setText = (text) => {
  const node = element();
  if (node) node.value = text;
};

/**
 * Parse the editor contents.
 * @returns {{ok: true, profile: object} | {ok: false, message: string}}
 */
export function parse() {
  try {
    const profile = JSON.parse(getText());
    if (profile === null || typeof profile !== 'object' || Array.isArray(profile)) {
      return { ok: false, message: 'A device profile must be a JSON object.' };
    }
    return { ok: true, profile };
  } catch (error) {
    return { ok: false, message: error.message };
  }
}
