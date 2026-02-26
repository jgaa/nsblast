
export default function qargs(from, max=30, kind=null, direction='forward') {
    const params = new URLSearchParams();
    if (max > 0) params.set('limit', String(max));
    if (from) params.set('from', String(from));
    if (kind) params.set('kind', String(kind));
    if (direction) params.set('direction', String(direction));

    const query = params.toString();
    if (!query) {
      return "";
    }
    return `?${query}`;
}
