/** Composition root: mount every view, then re-render them all on state change. */

import { store } from './core/store.js';
import { loadSampleProfile, loadHealthSnapshot } from './core/actions.js';

import theme, { applyStoredTheme } from './views/theme.js';
import identity from './views/identity.js';
import readings from './views/readings.js';
import profile from './views/profile.js';
import validation from './views/validation.js';
import review from './views/review.js';
import devices from './views/devices.js';
import interfaces from './views/interfaces.js';
import queues from './views/queues.js';
import health from './views/health.js';

const views = [theme, identity, readings, profile, validation, review, devices, interfaces, queues, health];

applyStoredTheme();
views.forEach((view) => view.mount?.());
store.subscribe((state) => views.forEach((view) => view.render?.(state)));

// Render once at rest, then open on the prototype so the desk is usable
// immediately. Loading a profile is not approval; the decision stays explicit.
views.forEach((view) => view.render?.(store.get()));
loadSampleProfile();
loadHealthSnapshot();
