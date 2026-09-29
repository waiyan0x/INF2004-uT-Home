/** The pill row: latest sensor readings, the way a Lovelace overview leads with
 *  the values people actually came to see. Populated from the health snapshot;
 *  the node's own identity lives in the app bar and the Profile header. */

import { $, render } from '../core/dom.js';
import { pill } from '../ui/pill.js';
import { describeMetric } from '../domain/catalog.js';
import { formatReading } from '../core/format.js';

export default {
  render({ health }) {
    const readings = health?.readings ?? [];
    const target = $('#pills');

    if (!readings.length) {
      render(target, pill({ icon: 'activity', label: 'No readings reported', accent: 'slate' }));
      return;
    }

    render(target, readings.map((reading) => {
      const spec = describeMetric(reading.metric);
      return pill({
        icon: spec.icon,
        accent: spec.accent,
        label: `${spec.label} ${formatReading(reading)}`,
      });
    }));
  },
};
