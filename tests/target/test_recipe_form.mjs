import assert from 'node:assert/strict';
import {test} from 'node:test';
import {parseRecipeFields, describeRecipe} from '../../tools/configurator/static/recipes.js';

const fields = () => ({device_id: 'chip_id', bus_ref: 'lab_i2c', address: '0x77',
  register: '0x00', read_length: '1', read_pause_ms: '0', select_offset: '0',
  select_length: '1', interval_ms: '500', timeout_ms: '100'});

test('recipe form keeps hexadecimal addresses and parses explicit integer instructions', () => {
  const recipe = parseRecipeFields(fields());
  assert.equal(recipe.address, '0x77');
  assert.equal(recipe.register, 0);
  assert.equal(recipe.read_length, 1);
  assert.match(describeRecipe(recipe), /0x00.*read 1 byte.*keep 1 byte.*offset 0.*500 ms.*100 ms/);
  assert.equal(parseRecipeFields({...fields(), register: '255'}).register, 255);
});

test('empty, fractional, negative or overflowing fields cannot become a recipe', () => {
  for (const [key, value] of [['register', ''], ['register', '0x100'], ['read_length', '1.5'],
    ['read_length', '17'], ['select_offset', '-1'], ['read_pause_ms', '11'],
    ['interval_ms', '199'], ['timeout_ms', '101'], ['address', '0x78'], ['bus_ref', '']]) {
    assert.throws(() => parseRecipeFields({...fields(), [key]: value}), {name: 'Error'});
  }
});

test('byte selection cannot leave the returned buffer and deadline includes the read delay', () => {
  assert.throws(() => parseRecipeFields({...fields(), select_offset: '1'}), /selection/);
  assert.throws(() => parseRecipeFields({...fields(), read_pause_ms: '10', timeout_ms: '10'}), /delay/);
  const recipe = parseRecipeFields({...fields(), read_length: '16', select_offset: '15'});
  assert.equal(recipe.select_offset, 15);
});
