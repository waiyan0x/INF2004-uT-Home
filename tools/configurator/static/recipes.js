/** Form-only parsing. The host and firmware independently admit every profile. */
const bounds = {
  register: [0, 255], read_length: [1, 16], read_pause_ms: [0, 10],
  select_offset: [0, 15], select_length: [1, 16],
  interval_ms: [200, 60000], timeout_ms: [10, 100],
};

export function parseRecipeFields(fields) {
  const recipe = {};
  for (const key of ['device_id', 'bus_ref']) {
    const pattern = key === 'bus_ref' ? /^[A-Za-z][A-Za-z0-9_.-]{0,30}$/ : /^[A-Za-z][A-Za-z0-9_.-]{0,21}$/;
    if (!pattern.test(fields[key])) {
      throw new Error(key === 'bus_ref' ? 'Choose an existing I²C bus in the profile.'
        : 'Use a source name starting with a letter, up to 22 characters.');
    }
    recipe[key] = fields[key];
  }
  if (!/^0x[0-9a-f]{2}$/.test(fields.address) || Number(fields.address) < 8 || Number(fields.address) > 119) {
    throw new Error('Use a 7-bit address from 0x08 to 0x77 in lower-case hex.');
  }
  recipe.address = fields.address;
  for (const [key, [min, max]] of Object.entries(bounds)) {
    const input = fields[key];
    const isRegisterHex = key === 'register' && /^0x[0-9a-f]{1,2}$/i.test(input);
    if (!(isRegisterHex || /^[0-9]+$/.test(input))) {
      throw new Error(`${key}: enter a whole number${key === 'register' ? ' or hex register' : ''}.`);
    }
    const value = Number(input);
    if (!Number.isSafeInteger(value) || value < min || value > max) {
      throw new Error(`${key}: use ${min}–${max}.`);
    }
    recipe[key] = value;
  }
  if (recipe.select_offset + recipe.select_length > recipe.read_length) {
    throw new Error('Byte selection must fit inside the bytes read.');
  }
  if (recipe.read_pause_ms >= recipe.timeout_ms) {
    throw new Error('Read delay must be shorter than the deadline.');
  }
  return recipe;
}

export function describeRecipe(recipe) {
  const register = recipe.register.toString(16).padStart(2, '0').toUpperCase();
  const bytes = size => `${size} byte${size === 1 ? '' : 's'}`;
  return `Select register 0x${register} → wait ${recipe.read_pause_ms} ms → read ${bytes(recipe.read_length)} → keep ${bytes(recipe.select_length)} from offset ${recipe.select_offset}. Repeat every ${recipe.interval_ms} ms; deadline ${recipe.timeout_ms} ms.`;
}
