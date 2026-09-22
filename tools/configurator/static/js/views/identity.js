/** Node identity: app bar subtitle, plus board and runtime facts in the
 *  Profile header. The pill row now carries measurements instead. */

import { $ } from '../core/dom.js';
import { humanise } from '../core/format.js';

const setText = (selector, value) => {
  const element = $(selector);
  if (element) element.textContent = value;
};

export default {
  render({ profile }) {
    if (!profile) {
      setText('#appbarNode', 'no profile loaded');
      setText('#profileMeta', '');
      setText('#profileRuntime', '');
      return;
    }

    const board = profile.board ?? {};
    const runtime = profile.runtime ?? {};

    setText('#appbarNode', profile.node_id ?? '—');
    setText('#profileMeta', [
      board.variant ?? board.platform,
      `schema ${profile.schema_version}`,
      `${profile.devices?.length ?? 0} devices`,
    ].filter(Boolean).join(' · '));

    setText('#profileRuntime', [
      `Source: ${humanise(runtime.configuration_source ?? 'unset')}`,
      runtime.rollback_on_error ? 'rollback on error' : 'no rollback',
      `${runtime.max_device_tasks ?? '—'} task limit`,
    ].join(' · '));
  },
};
