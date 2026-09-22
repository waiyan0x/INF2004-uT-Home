/** Presentation-only formatting. No DOM, no state: safe to unit test. */

export function formatClock(hertz) {
  if (hertz >= 1_000_000) return `${(hertz / 1_000_000).toLocaleString()} MHz`;
  if (hertz >= 1_000) return `${(hertz / 1_000).toLocaleString()} kHz`;
  return `${hertz.toLocaleString()} Hz`;
}

export function formatUptime(milliseconds) {
  const seconds = Math.floor(milliseconds / 1000);
  const hours = Math.floor(seconds / 3600);
  const minutes = Math.floor((seconds % 3600) / 60);
  if (hours) return `${hours} h ${minutes} m`;
  if (minutes) return `${minutes} m ${seconds % 60} s`;
  return `${seconds} s`;
}

/** "usb_serial" -> "usb serial" */
export const humanise = (value) => String(value).replace(/_/g, ' ');

/** Collect an interface's pin assignments: { sda_gpio: 8 } -> "SDA 8". */
export function formatPins(interfaceObject) {
  return Object.entries(interfaceObject)
    .filter(([key]) => key.endsWith('_gpio'))
    .map(([key, pin]) => `${key.replace(/_gpio$/, '').toUpperCase()} ${pin}`)
    .join(' · ');
}

/** Bus speed, whichever unit the protocol declares it in. */
export function formatBusSpeed(bus) {
  if (bus.clock_hz) return formatClock(bus.clock_hz);
  if (bus.baud_rate) return `${bus.baud_rate.toLocaleString()} baud`;
  return '';
}

/** Render a measurement for display: "23.4 °C", "1008.6 hPa", "1 target".
 *  A plural unit is singularised at a value of one. */
export function formatReading({ value, unit }) {
  const number = Number.isInteger(value) ? value : Number(value).toFixed(1);
  if (!unit) return String(number);
  if (value === 1 && /^[a-z]+s$/.test(unit)) return `${number} ${unit.slice(0, -1)}`;
  return `${number} ${unit}`;
}
