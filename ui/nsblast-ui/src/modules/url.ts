export function encodePathSegment(value: string | number): string {
  return encodeURIComponent(String(value));
}
