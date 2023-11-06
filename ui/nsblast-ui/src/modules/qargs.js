
export default function qargs(from, max=30) {
    let q = []
    if (max > 0) q.push(`limit=${max}`);
    if (from) q.push(`from=${from}`)
    if (q.length === 0)
      return "";
    return "?" + q.join('&')
}