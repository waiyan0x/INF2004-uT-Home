"use strict";

function line(container, value) {
  const item = document.createElement('p');
  item.className = 'hint';
  item.textContent = value;
  container.append(item);
}

// These strings come from an untrusted model and are never interpreted as HTML.
export function renderDatasheet(result, facts, uncertainties) {
  facts.replaceChildren(); uncertainties.replaceChildren();
  for (const fact of result.draft.facts) {
    const pages = fact.pages.length ? ` · pages ${fact.pages.join(', ')}` : '';
    line(facts, `${fact.name}: ${fact.value ?? 'unknown'} (${fact.status}${pages})${fact.note ? ' — ' + fact.note : ''}`);
  }
  if (result.i2c_address_review.derived_7bit !== null) {
    const derived = result.i2c_address_review.derived_7bit.toString(16).padStart(2, '0');
    line(facts, `Calculated 7-bit I2C address: 0x${derived} — check the cited source.`);
  }
  for (const issue of result.draft.unresolved) line(uncertainties, issue);
  if (result.i2c_address_review.conflicting) line(uncertainties, 'The reported I2C addresses conflict; review the source.');
  if (!result.draft.unresolved.length && !result.i2c_address_review.conflicting) {
    line(uncertainties, 'No unresolved points were reported; verify the source yourself.');
  }
}
