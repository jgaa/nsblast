use anyhow::Result;
use serde::Serialize;
use serde_json::Value;

use crate::cli::OutputFormat;

pub fn print_serialized<T: Serialize>(format: &OutputFormat, value: &T) -> Result<()> {
    let json = serde_json::to_value(value)?;
    print_value(format, &json)
}

pub fn print_value(format: &OutputFormat, value: &Value) -> Result<()> {
    match format {
        OutputFormat::Json => println!("{}", serde_json::to_string_pretty(value)?),
        OutputFormat::Yaml => println!("{}", serde_yaml::to_string(value)?),
        OutputFormat::Table => print_table(value),
    }
    Ok(())
}

fn print_table(value: &Value) {
    match value {
        Value::Array(items) => {
            if items.is_empty() {
                return;
            }
            if items.iter().all(Value::is_object) {
                let mut headers = Vec::<String>::new();
                for item in items {
                    if let Some(obj) = item.as_object() {
                        for key in obj.keys() {
                            if !headers.iter().any(|existing| existing == key) {
                                headers.push(key.clone());
                            }
                        }
                    }
                }
                println!("{}", headers.join("\t"));
                for item in items {
                    let obj = item.as_object().expect("object");
                    let row = headers
                        .iter()
                        .map(|header| cell(obj.get(header)))
                        .collect::<Vec<_>>();
                    println!("{}", row.join("\t"));
                }
            } else {
                for item in items {
                    println!("{}", scalar(item));
                }
            }
        }
        Value::Object(obj) => {
            for (key, val) in obj {
                println!("{key}\t{}", scalar(val));
            }
        }
        _ => println!("{}", scalar(value)),
    }
}

fn cell(value: Option<&Value>) -> String {
    value.map(scalar).unwrap_or_default()
}

fn scalar(value: &Value) -> String {
    match value {
        Value::Null => String::new(),
        Value::Bool(v) => v.to_string(),
        Value::Number(v) => v.to_string(),
        Value::String(v) => v.clone(),
        Value::Array(v) => v.iter().map(scalar).collect::<Vec<_>>().join(","),
        Value::Object(v) => serde_json::to_string(v).unwrap_or_else(|_| "{}".to_string()),
    }
}
