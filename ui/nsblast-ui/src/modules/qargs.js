
export default function qargs(from, max=30, kind=null, direction='forward') {
    let q = []
    if (max > 0) q.push(`limit=${max}`);
    if (from) q.push(`from=${from}`)
    if (kind) q.push(`kind=${kind}`)
    if (direction) q.push(`direction=${direction}`)
    if (q.length === 0)
      return "";
    return "?" + q.join('&')
}