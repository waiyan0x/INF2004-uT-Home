/** The hardware this project actually supports.
 *
 * Single source of truth for how a device, bus, or behaviour is presented.
 * The driver and protocol keys mirror `SUPPORTED_DRIVERS` and
 * `SUPPORTED_PROTOCOLS` in tools/configurator/validator.py and the `driver`
 * enum in schemas/profile.schema.json. Adding hardware to the project means
 * adding it there first, then adding one entry here — nothing else in the UI
 * names a specific sensor.
 */

export const DRIVERS = {
  bmp388: {
    label: 'Bosch BMP388',
    role: 'Environment sensor',
    measures: 'pressure · temperature',
    bus: 'i2c',
    icon: 'thermometer',
    accent: 'orange',
  },
  ssd1306_spi: {
    label: 'SSD1306 OLED',
    role: 'Status display',
    measures: 'monochrome panel',
    bus: 'spi',
    icon: 'monitor',
    accent: 'blue',
  },
  hlk_ld2450: {
    label: 'HLK-LD2450',
    role: 'Presence radar',
    measures: 'target tracking',
    bus: 'uart',
    icon: 'radar',
    accent: 'violet',
  },
  hc_sr04: {
    label: 'HC-SR04',
    role: 'Ultrasonic range',
    measures: 'distance',
    bus: 'gpio',
    icon: 'ruler',
    accent: 'teal',
  },
};

export const PROTOCOLS = {
  i2c: { label: 'I²C', icon: 'i2c', accent: 'indigo' },
  spi: { label: 'SPI', icon: 'spi', accent: 'violet' },
  uart: { label: 'UART', icon: 'uart', accent: 'cyan' },
  gpio: { label: 'GPIO', icon: 'gpio', accent: 'green' },
};

export const BEHAVIOURS = {
  periodic_sensor: 'Periodic sample',
  queue_display: 'Queue display',
  stream_sensor: 'Frame stream',
};

/** Measurement channels, mirroring ALLOWED_METRICS in monitoring.py.
 *
 * `humidity` is listed so it renders correctly the moment a humidity-capable
 * sensor is supported. No current driver produces it: the BMP388 reports
 * pressure and temperature only.
 */
export const METRICS = {
  temperature: { label: 'Temperature', icon: 'thermometer', accent: 'orange' },
  humidity: { label: 'Humidity', icon: 'droplet', accent: 'blue' },
  pressure: { label: 'Pressure', icon: 'gauge', accent: 'indigo' },
  presence: { label: 'Presence', icon: 'radar', accent: 'violet' },
  distance: { label: 'Distance', icon: 'ruler', accent: 'teal' },
};

export const describeMetric = (metric) =>
  METRICS[metric] ?? { label: String(metric ?? '—'), icon: 'alert', accent: 'rose' };

/** Status names used across tiles, badges, and alerts. */
export const STATUS_ACCENT = {
  ok: 'green',
  warn: 'amber',
  bad: 'rose',
  idle: 'slate',
};

const UNKNOWN_DRIVER = {
  label: 'Unsupported driver',
  role: 'Not in the supported set',
  measures: '',
  bus: null,
  icon: 'alert',
  accent: 'rose',
};

export const describeDriver = (driver) => DRIVERS[driver] ?? UNKNOWN_DRIVER;

export const describeProtocol = (protocol) =>
  PROTOCOLS[protocol] ?? { label: String(protocol ?? '—').toUpperCase(), icon: 'alert', accent: 'rose' };

export const describeBehaviour = (profile) => BEHAVIOURS[profile] ?? profile ?? '';

/** The HC-SR04 echo line is 5 V; the validator refuses a profile that does not
 *  declare the divider, so the interface states it explicitly. */
export const requiresVoltageDivider = (driver) => driver === 'hc_sr04';
