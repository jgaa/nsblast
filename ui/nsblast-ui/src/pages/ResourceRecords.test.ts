import { describe, expect, it } from 'vitest';
import { normalizeRrValue } from './ResourceRecords';

describe('normalizeRrValue', () => {
  it('strips a trailing dot from cname values', () => {
    expect(normalizeRrValue('cname', 'target.example.com.')).toBe('target.example.com');
  });

  it('leaves non-cname values unchanged', () => {
    expect(normalizeRrValue('txt', 'value.')).toBe('value.');
  });
});
