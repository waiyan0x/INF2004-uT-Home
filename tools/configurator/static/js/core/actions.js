/** Every state transition the UI can perform.
 *
 * Views bind controls to these; nothing here touches layout, so the workflow
 * rules stay readable in one place: a profile must validate before it becomes a
 * candidate, and a candidate must be decided before it can be exported.
 */

import { api } from './api.js';
import { store } from './store.js';
import * as editor from './editor.js';

/** Replace the editor contents and reset any in-flight review. */
function loadProfileText(text, title) {
  editor.setText(text);
  const parsed = editor.parse();
  store.set({
    profile: parsed.ok ? parsed.profile : null,
    candidate: null,
    reviewed: null,
    review: null,
    validation: parsed.ok
      ? { tone: 'neutral', title, text: 'Validate before a review decision can be recorded.', errors: [] }
      : { tone: 'bad', title: 'JSON could not be parsed', text: parsed.message, errors: [] },
  });
}

export async function loadSampleProfile() {
  const profile = await api.sampleProfile();
  loadProfileText(JSON.stringify(profile, null, 2), 'Prototype profile loaded');
}

export async function importProfile(file) {
  loadProfileText(await file.text(), `Imported ${file.name}`);
}

export async function importRecommendation(file) {
  try {
    const recommendation = JSON.parse(await file.text());
    const profile = await api.generate(recommendation);
    loadProfileText(JSON.stringify(profile, null, 2), `Generated from ${file.name}`);
  } catch (error) {
    store.set({ validation: { tone: 'bad', title: 'Recommendation rejected', text: error.message, errors: [] } });
  }
}

export function markProfileEdited() {
  const parsed = editor.parse();
  store.set({
    profile: parsed.ok ? parsed.profile : null,
    candidate: null,
    reviewed: null,
    review: null,
    validation: { tone: 'neutral', title: 'Profile changed', text: 'Validate the edited profile before review.', errors: [] },
  });
}

export function formatProfile() {
  const parsed = editor.parse();
  if (!parsed.ok) {
    store.set({ validation: { tone: 'bad', title: 'JSON could not be parsed', text: parsed.message, errors: [] } });
    return;
  }
  loadProfileText(JSON.stringify(parsed.profile, null, 2), 'Profile reformatted');
}

export async function validateProfile() {
  const sourceText = editor.getText();
  const parsed = editor.parse();
  if (!parsed.ok) {
    store.set({
      profile: null,
      candidate: null,
      reviewed: null,
      review: null,
      validation: { tone: 'bad', title: 'JSON could not be parsed', text: parsed.message, errors: [] },
    });
    return;
  }

  store.set({ profile: parsed.profile, candidate: null, reviewed: null, review: null });

  const result = await api.validate(parsed.profile);
  if (editor.getText() !== sourceText) return;
  if (!result.valid) {
    store.set({
      validation: {
        tone: 'bad',
        title: 'Profile rejected by the validator',
        text: 'Fix every finding below, then validate again.',
        errors: result.errors,
      },
    });
    return;
  }

  const candidate = await api.createCandidate(parsed.profile);
  if (editor.getText() !== sourceText) return;
  store.set({
    candidate,
    validation: {
      tone: 'ok',
      title: 'Profile is valid',
      text: 'A candidate was created and is awaiting an explicit decision.',
      errors: [],
    },
  });
}

export async function decide(decision, reviewer, note) {
  const { candidate } = store.get();
  if (!candidate) return;

  if (!reviewer.trim()) {
    store.set({ review: { tone: 'warn', title: 'Reviewer is required', text: 'Enter who is recording this decision.' } });
    return;
  }

  let reviewed;
  try {
    reviewed = await api.review(decision, { candidate, reviewer: reviewer.trim(), note });
  } catch (error) {
    if (store.get().candidate !== candidate) return;
    store.set({ review: { tone: 'bad', title: 'Decision was not recorded', text: error.message } });
    return;
  }
  if (store.get().candidate !== candidate) return;

  const approved = reviewed.status === 'approved';
  store.set({
    reviewed,
    review: {
      tone: approved ? 'ok' : 'bad',
      title: approved ? 'Approved for bundling' : 'Rejected',
      text: approved
        ? `Recorded by ${reviewed.review.reviewer}. The profile has not been activated on any device.`
        : `Recorded by ${reviewed.review.reviewer}. This candidate cannot be bundled.`,
    },
  });
}

export function exportReviewed() {
  const { reviewed } = store.get();
  if (!reviewed) return;

  const blob = new Blob([`${JSON.stringify(reviewed, null, 2)}\n`], { type: 'application/json' });
  const link = document.createElement('a');
  link.href = URL.createObjectURL(blob);
  link.download = `${reviewed.profile.node_id}-${reviewed.status}.json`;
  link.click();
  URL.revokeObjectURL(link.href);
}

export async function loadHealthSnapshot() {
  try {
    store.set({ health: await api.normaliseHealth(await api.sampleHealth()), healthSource: 'sample fixture', healthError: null });
  } catch (error) {
    store.set({ health: null, healthSource: null, healthError: error.message });
  }
}

export async function importHealthSnapshot(file) {
  try {
    const snapshot = JSON.parse(await file.text());
    store.set({ health: await api.normaliseHealth(snapshot), healthSource: `imported ${file.name}`, healthError: null });
  } catch (error) {
    store.set({ health: null, healthSource: null, healthError: error.message });
  }
}
