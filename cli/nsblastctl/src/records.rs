use anyhow::{Result, anyhow, bail};
use base64::Engine;
use base64::engine::general_purpose::URL_SAFE_NO_PAD;
use serde::{Deserialize, Serialize};
use serde_json::{Map, Value, json};

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct FlatRecord {
    pub id: String,
    pub fqdn: String,
    pub zone: String,
    pub name: String,
    pub rr_type: String,
    pub ttl: u32,
    pub value: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub priority: Option<u16>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub weight: Option<u16>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub port: Option<u16>,
}

pub fn make_record_id(record: &FlatRecord) -> String {
    let raw = format!(
        "{}|{}|{}|{}|{}|{}|{}|{}",
        record.zone,
        record.fqdn,
        record.rr_type,
        record.ttl,
        record.value,
        record.priority.unwrap_or_default(),
        record.weight.unwrap_or_default(),
        record.port.unwrap_or_default()
    );
    URL_SAFE_NO_PAD.encode(raw.as_bytes())
}

pub fn decode_record_id(id: &str) -> Result<FlatRecord> {
    let bytes = URL_SAFE_NO_PAD
        .decode(id)
        .map_err(|err| anyhow!("invalid id: {err}"))?;
    let raw = String::from_utf8(bytes).map_err(|err| anyhow!("invalid id: {err}"))?;
    let parts = raw.split('|').collect::<Vec<_>>();
    if parts.len() != 8 {
        bail!("invalid id payload");
    }
    let zone = parts[0].to_string();
    let fqdn = parts[1].to_string();
    let rr_type = parts[2].to_string();
    let ttl = parts[3].parse::<u32>()?;
    let value = parts[4].to_string();
    let priority = parse_optional_u16(parts[5])?;
    let weight = parse_optional_u16(parts[6])?;
    let port = parse_optional_u16(parts[7])?;
    Ok(FlatRecord {
        id: id.to_string(),
        zone: zone.clone(),
        fqdn: fqdn.clone(),
        name: relative_name(&zone, &fqdn),
        rr_type,
        ttl,
        value,
        priority,
        weight,
        port,
    })
}

fn parse_optional_u16(raw: &str) -> Result<Option<u16>> {
    if raw == "0" || raw.is_empty() {
        Ok(None)
    } else {
        Ok(Some(raw.parse::<u16>()?))
    }
}

pub fn relative_name(zone: &str, fqdn: &str) -> String {
    if fqdn.eq_ignore_ascii_case(zone) {
        "@".to_string()
    } else if let Some(prefix) = fqdn.strip_suffix(&format!(".{zone}")) {
        prefix.to_string()
    } else {
        fqdn.to_string()
    }
}

pub fn absolute_name(zone: &str, name: &str) -> String {
    if name == "@" {
        zone.to_string()
    } else if name.ends_with(zone) {
        name.to_string()
    } else {
        format!("{name}.{zone}")
    }
}

pub fn flatten_entry(zone: &str, entry: &Value) -> Result<Vec<FlatRecord>> {
    let obj = entry
        .as_object()
        .ok_or_else(|| anyhow!("entry must be an object"))?;
    let fqdn = obj
        .get("fqdn")
        .and_then(Value::as_str)
        .ok_or_else(|| anyhow!("entry missing fqdn"))?;
    let ttl = obj
        .get("ttl")
        .and_then(Value::as_u64)
        .ok_or_else(|| anyhow!("entry missing ttl"))? as u32;
    let mut out = Vec::new();
    push_strings(&mut out, zone, fqdn, ttl, obj, "a", "A");
    push_strings(&mut out, zone, fqdn, ttl, obj, "aaaa", "AAAA");
    push_strings(&mut out, zone, fqdn, ttl, obj, "txt", "TXT");
    push_strings(&mut out, zone, fqdn, ttl, obj, "ns", "NS");
    push_strings(&mut out, zone, fqdn, ttl, obj, "ptr", "PTR");
    if let Some(cname) = obj.get("cname").and_then(Value::as_str) {
        let mut record = base_record(zone, fqdn, ttl, "CNAME", cname);
        record.id = make_record_id(&record);
        out.push(record);
    }
    if let Some(mx) = obj.get("mx").and_then(Value::as_array) {
        for item in mx {
            let host = item
                .get("host")
                .and_then(Value::as_str)
                .ok_or_else(|| anyhow!("mx record missing host"))?;
            let priority =
                item.get("priority")
                    .and_then(Value::as_u64)
                    .ok_or_else(|| anyhow!("mx record missing priority"))? as u16;
            let mut record = base_record(zone, fqdn, ttl, "MX", host);
            record.priority = Some(priority);
            record.id = make_record_id(&record);
            out.push(record);
        }
    }
    if let Some(srv) = obj.get("srv").and_then(Value::as_array) {
        for item in srv {
            let target = item
                .get("target")
                .and_then(Value::as_str)
                .ok_or_else(|| anyhow!("srv record missing target"))?;
            let priority =
                item.get("priority")
                    .and_then(Value::as_u64)
                    .ok_or_else(|| anyhow!("srv record missing priority"))? as u16;
            let weight =
                item.get("weight")
                    .and_then(Value::as_u64)
                    .ok_or_else(|| anyhow!("srv record missing weight"))? as u16;
            let port = item
                .get("port")
                .and_then(Value::as_u64)
                .ok_or_else(|| anyhow!("srv record missing port"))? as u16;
            let mut record = base_record(zone, fqdn, ttl, "SRV", target);
            record.priority = Some(priority);
            record.weight = Some(weight);
            record.port = Some(port);
            record.id = make_record_id(&record);
            out.push(record);
        }
    }
    Ok(out)
}

fn push_strings(
    out: &mut Vec<FlatRecord>,
    zone: &str,
    fqdn: &str,
    ttl: u32,
    obj: &Map<String, Value>,
    field: &str,
    rr_type: &str,
) {
    if let Some(values) = obj.get(field).and_then(Value::as_array) {
        for value in values.iter().filter_map(Value::as_str) {
            let mut record = base_record(zone, fqdn, ttl, rr_type, value);
            record.id = make_record_id(&record);
            out.push(record);
        }
    }
}

fn base_record(zone: &str, fqdn: &str, ttl: u32, rr_type: &str, value: &str) -> FlatRecord {
    FlatRecord {
        id: String::new(),
        fqdn: fqdn.to_string(),
        zone: zone.to_string(),
        name: relative_name(zone, fqdn),
        rr_type: rr_type.to_string(),
        ttl,
        value: value.to_string(),
        priority: None,
        weight: None,
        port: None,
    }
}

pub fn build_entry(fqdn: &str, records: &[FlatRecord], soa: Option<Value>) -> Result<Value> {
    if records.is_empty() && soa.is_none() {
        bail!("entry must contain at least one record");
    }
    let ttl = records.first().map(|record| record.ttl).unwrap_or(300);
    let mut obj = Map::new();
    obj.insert("ttl".to_string(), json!(ttl));
    if let Some(soa) = soa {
        obj.insert("soa".to_string(), soa);
    }
    for record in records {
        match record.rr_type.as_str() {
            "A" => push_array_string(&mut obj, "a", &record.value),
            "AAAA" => push_array_string(&mut obj, "aaaa", &record.value),
            "TXT" => push_array_string(&mut obj, "txt", &record.value),
            "NS" => push_array_string(&mut obj, "ns", &record.value),
            "PTR" => push_array_string(&mut obj, "ptr", &record.value),
            "CNAME" => {
                obj.insert("cname".to_string(), json!(record.value));
            }
            "MX" => {
                let priority = record
                    .priority
                    .ok_or_else(|| anyhow!("mx requires priority"))?;
                obj.entry("mx".to_string())
                    .or_insert_with(|| Value::Array(Vec::new()))
                    .as_array_mut()
                    .expect("array")
                    .push(json!({"host": record.value, "priority": priority}));
            }
            "SRV" => {
                let priority = record
                    .priority
                    .ok_or_else(|| anyhow!("srv requires priority"))?;
                let weight = record
                    .weight
                    .ok_or_else(|| anyhow!("srv requires weight"))?;
                let port = record.port.ok_or_else(|| anyhow!("srv requires port"))?;
                obj.entry("srv".to_string())
                    .or_insert_with(|| Value::Array(Vec::new()))
                    .as_array_mut()
                    .expect("array")
                    .push(json!({
                        "target": record.value,
                        "priority": priority,
                        "weight": weight,
                        "port": port
                    }));
            }
            other => bail!("unsupported rr type: {other}"),
        }
    }
    let mut wrapped = Map::new();
    wrapped.insert("fqdn".to_string(), json!(fqdn));
    for (key, value) in obj {
        wrapped.insert(key, value);
    }
    Ok(Value::Object(wrapped))
}

fn push_array_string(obj: &mut Map<String, Value>, field: &str, value: &str) {
    obj.entry(field.to_string())
        .or_insert_with(|| Value::Array(Vec::new()))
        .as_array_mut()
        .expect("array")
        .push(json!(value));
}

#[cfg(test)]
mod tests {
    use super::{FlatRecord, decode_record_id, make_record_id};

    #[test]
    fn synthetic_id_round_trips() {
        let mut record = FlatRecord {
            id: String::new(),
            fqdn: "www.example.com".to_string(),
            zone: "example.com".to_string(),
            name: "www".to_string(),
            rr_type: "A".to_string(),
            ttl: 300,
            value: "192.0.2.10".to_string(),
            priority: None,
            weight: None,
            port: None,
        };
        record.id = make_record_id(&record);
        let decoded = decode_record_id(&record.id).expect("decode");
        assert_eq!(decoded.fqdn, record.fqdn);
        assert_eq!(decoded.zone, record.zone);
        assert_eq!(decoded.rr_type, record.rr_type);
        assert_eq!(decoded.value, record.value);
    }
}
