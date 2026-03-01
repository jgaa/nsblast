use std::collections::{BTreeMap, BTreeSet};
use std::fs;
use std::io::{self, IsTerminal, Read};
use std::path::Path;

use anyhow::{Context, Result, anyhow, bail};
use clap::ValueEnum;
use csv::Trim;
use reqwest::{Method, StatusCode};
use serde::Serialize;
use serde_json::{Value, json};
use url::Url;

use crate::cli::{
    AuthCommand, Cli, Command, ConflictMode, ExportCommand, ImportCommand, OutputFormat, RrCommand,
    TenantCommand, ZoneCommand,
};
use crate::config::{
    ConfigFile, EffectiveProfile, StoredProfile, canonicalize_server_url, config_path,
    delete_password, derive_key_id, load_config, load_password, save_config, select_profile,
    store_password, upsert_profile,
};
use crate::output::{print_serialized, print_value};
use crate::records::{FlatRecord, absolute_name, build_entry, decode_record_id, flatten_entry};

const EXIT_SUCCESS: i32 = 0;
const EXIT_INPUT: i32 = 2;
const EXIT_AUTH: i32 = 3;
const EXIT_CONFLICT: i32 = 4;
const EXIT_OTHER: i32 = 5;

pub async fn run(cli: Cli) -> i32 {
    match run_inner(cli).await {
        Ok(code) => code,
        Err(err) => {
            eprintln!("{err}");
            map_anyhow_exit(&err)
        }
    }
}

async fn run_inner(cli: Cli) -> Result<i32> {
    match cli.command.clone() {
        Command::Auth { command } => run_auth_command(cli, &command).await,
        _ => {
            let runtime = Runtime::load(&cli, None, None, false)?;
            run_command(cli, runtime).await
        }
    }
}

async fn run_command(cli: Cli, runtime: Runtime) -> Result<i32> {
    match cli.command.clone() {
        Command::Zone { command } => run_zone_command(&runtime, &cli, command).await?,
        Command::Rr { command } => run_rr_command(&runtime, &cli, command).await?,
        Command::Tenant { command } => run_tenant_command(&runtime, &cli, command).await?,
        Command::Import { command } => run_import_command(&runtime, &cli, command).await?,
        Command::Export { command } => run_export_command(&runtime, &cli, command).await?,
        Command::Health => {
            let version = runtime.api.get_json("version", &[]).await?;
            let out = json!({
                "ok": true,
                "server": runtime.server,
                "username": runtime.username,
                "version": extract_value(&version)
            });
            print_value(&runtime.output, &out)?;
        }
        Command::Auth { .. } => unreachable!(),
    }
    Ok(EXIT_SUCCESS)
}

async fn run_auth_command(cli: Cli, command: &AuthCommand) -> Result<i32> {
    match command {
        AuthCommand::Login {
            server,
            username,
            password_stdin,
        } => {
            let runtime = Runtime::load(&cli, server.clone(), username.clone(), *password_stdin)?;
            runtime.api.get_json("version", &[]).await?;
            persist_login(&runtime, &cli, true)?;
            let out = json!({
                "server": runtime.server,
                "username": runtime.username,
                "authenticated": true
            });
            print_value(&runtime.output, &out)?;
        }
        AuthCommand::SetPassword {
            server,
            username,
            password_stdin,
        } => {
            let runtime = Runtime::load(&cli, server.clone(), username.clone(), *password_stdin)?;
            runtime.api.get_json("version", &[]).await?;
            persist_login(&runtime, &cli, false)?;
            let out = json!({
                "server": runtime.server,
                "username": runtime.username,
                "password_saved": cli.save_password
            });
            print_value(&runtime.output, &out)?;
        }
        AuthCommand::Logout { server, username } => {
            let path = config_path()?;
            let mut cfg = load_config(&path, cli.allow_insecure_config)?;
            let requested_profile = cli.profile.as_deref();
            let selected = select_profile(&cfg, requested_profile);
            let resolved_server = server
                .clone()
                .or(cli.server.clone())
                .or_else(|| std::env::var("NSBLASTCTL_SERVER").ok())
                .or(selected.server)
                .ok_or_else(|| anyhow!("missing server"))?;
            let resolved_username = username
                .clone()
                .or(cli.username.clone())
                .or_else(|| std::env::var("NSBLASTCTL_USERNAME").ok())
                .or(selected.username)
                .ok_or_else(|| anyhow!("missing username"))?;
            let key_id = selected
                .password_key_id
                .or_else(|| derive_key_id(&resolved_server, &resolved_username).ok());
            if let Some(key_id) = key_id {
                delete_password(&key_id)?;
            }
            clear_password_fields(&mut cfg, requested_profile);
            save_config(&path, &cfg)?;
            let out = json!({
                "server": canonicalize_server_url(&resolved_server)?,
                "username": resolved_username,
                "logged_out": true
            });
            print_value(&cli.output, &out)?;
        }
        AuthCommand::Whoami { server } => {
            let runtime = Runtime::load(&cli, server.clone(), None, false)?;
            let version = runtime.api.get_json("version", &[]).await?;
            let permissions = runtime.api.get_json("permissions", &[]).await?;
            let out = json!({
                "server": runtime.server,
                "username": runtime.username,
                "permissions": extract_value(&permissions),
                "version": extract_value(&version)
            });
            print_value(&runtime.output, &out)?;
        }
    }
    Ok(EXIT_SUCCESS)
}

fn persist_login(runtime: &Runtime, cli: &Cli, keep_existing_without_save: bool) -> Result<()> {
    let path = config_path()?;
    let mut cfg = load_config(&path, cli.allow_insecure_config)?;
    let key_id = derive_key_id(&runtime.server, &runtime.username)?;
    let mut profile = StoredProfile {
        server: Some(runtime.server.clone()),
        username: Some(runtime.username.clone()),
        password_source: None,
        password_key_id: None,
        password: None,
        timeout: Some(runtime.timeout),
        output: Some(runtime.output.clone()),
    };

    if cli.save_password {
        if !cli.no_keyring {
            match store_password(&key_id, &runtime.password) {
                Ok(()) => {
                    profile.password_source = Some("keyring".to_string());
                    profile.password_key_id = Some(key_id);
                }
                Err(err) if cli.allow_insecure_config => {
                    profile.password_source = Some("config".to_string());
                    profile.password = Some(runtime.password.clone());
                    eprintln!("keyring unavailable, stored password in config: {err}");
                }
                Err(err) => {
                    return Err(anyhow!(
                        "failed to store password in keyring: {err}; rerun with --allow-insecure-config to permit config storage"
                    ));
                }
            }
        } else {
            if !cli.allow_insecure_config {
                bail!("--save-password with --no-keyring requires --allow-insecure-config");
            }
            profile.password_source = Some("config".to_string());
            profile.password = Some(runtime.password.clone());
        }
    } else if keep_existing_without_save {
        let existing = select_profile(&cfg, cli.profile.as_deref());
        if existing.password_source.as_deref() == Some("keyring") {
            profile.password_source = existing.password_source;
            profile.password_key_id = existing.password_key_id;
        }
    }

    upsert_profile(
        &mut cfg,
        cli.profile.as_deref().unwrap_or("default"),
        profile,
    );
    save_config(&path, &cfg)?;
    Ok(())
}

fn clear_password_fields(cfg: &mut ConfigFile, requested_profile: Option<&str>) {
    let profile_name =
        requested_profile.unwrap_or_else(|| cfg.profile.as_deref().unwrap_or("default"));
    if profile_name == "default" && cfg.profiles.is_empty() {
        cfg.password_source = None;
        cfg.password_key_id = None;
        cfg.password = None;
    } else if let Some(profile) = cfg.profiles.get_mut(profile_name) {
        profile.password_source = None;
        profile.password_key_id = None;
        profile.password = None;
    }
}

#[derive(Clone)]
struct Runtime {
    api: ApiClient,
    output: OutputFormat,
    yes: bool,
    dry_run: bool,
    server: String,
    username: String,
    password: String,
    timeout: u64,
}

impl Runtime {
    fn load(
        cli: &Cli,
        server_override: Option<String>,
        username_override: Option<String>,
        command_password_stdin: bool,
    ) -> Result<Self> {
        let path = config_path()?;
        let cfg = load_config(&path, cli.allow_insecure_config)?;
        let selected = select_profile(&cfg, cli.profile.as_deref());
        let server = resolve_server(cli, &selected, server_override)?;
        let username = resolve_username(cli, &selected, username_override)?;
        let password =
            resolve_password(cli, &selected, &server, &username, command_password_stdin)?;
        let output = selected
            .output
            .clone()
            .unwrap_or_else(|| cli.output.clone());
        let timeout = selected.timeout.unwrap_or(cli.timeout);
        let api = ApiClient::new(&server, &username, &password, timeout)?;
        Ok(Self {
            api,
            output,
            yes: cli.yes,
            dry_run: cli.dry_run,
            server,
            username,
            password,
            timeout,
        })
    }
}

fn resolve_server(
    cli: &Cli,
    selected: &EffectiveProfile,
    override_value: Option<String>,
) -> Result<String> {
    let raw = override_value
        .or_else(|| cli.server.clone())
        .or_else(|| std::env::var("NSBLASTCTL_SERVER").ok())
        .or_else(|| selected.server.clone())
        .ok_or_else(|| anyhow!("missing server; set --server or NSBLASTCTL_SERVER"))?;
    canonicalize_server_url(&raw)
}

fn resolve_username(
    cli: &Cli,
    selected: &EffectiveProfile,
    override_value: Option<String>,
) -> Result<String> {
    override_value
        .or_else(|| cli.username.clone())
        .or_else(|| std::env::var("NSBLASTCTL_USERNAME").ok())
        .or_else(|| selected.username.clone())
        .ok_or_else(|| anyhow!("missing username; set --username or NSBLASTCTL_USERNAME"))
}

fn resolve_password(
    cli: &Cli,
    selected: &EffectiveProfile,
    server: &str,
    username: &str,
    command_password_stdin: bool,
) -> Result<String> {
    if command_password_stdin || cli.password_stdin {
        return read_password_from_stdin();
    }
    if let Some(password) = cli.password.clone() {
        return Ok(password);
    }
    if !cli.no_keyring {
        let key_id = selected
            .password_key_id
            .clone()
            .or_else(|| derive_key_id(server, username).ok());
        if let Some(key_id) = key_id {
            if let Ok(password) = load_password(&key_id) {
                return Ok(password);
            }
        }
    }
    if selected.password_source.as_deref() == Some("config") {
        if let Some(password) = selected.password.clone() {
            return Ok(password);
        }
    }
    if let Ok(password) = std::env::var("NSBLASTCTL_PASSWORD") {
        return Ok(password);
    }
    if io::stdin().is_terminal() {
        return rpassword::prompt_password("Password: ").context("failed to read password");
    }
    bail!("no password source available in non-interactive mode")
}

fn read_password_from_stdin() -> Result<String> {
    let mut buf = String::new();
    io::stdin()
        .read_to_string(&mut buf)
        .context("failed to read password from stdin")?;
    let password = buf.trim_end_matches(['\r', '\n']).to_string();
    if password.is_empty() {
        bail!("password from stdin is empty");
    }
    Ok(password)
}

#[derive(Clone)]
struct ApiClient {
    client: reqwest::Client,
    base: Url,
    username: String,
    password: String,
}

impl ApiClient {
    fn new(server: &str, username: &str, password: &str, timeout: u64) -> Result<Self> {
        let base = Url::parse(server).with_context(|| format!("invalid server url: {server}"))?;
        let client = reqwest::Client::builder()
            .timeout(std::time::Duration::from_secs(timeout))
            .build()
            .context("failed to build http client")?;
        Ok(Self {
            client,
            base,
            username: username.to_string(),
            password: password.to_string(),
        })
    }

    async fn get_json(&self, path: &str, query: &[(&str, String)]) -> Result<Value> {
        self.send(Method::GET, path, query, None).await
    }

    async fn send(
        &self,
        method: Method,
        path: &str,
        query: &[(&str, String)],
        body: Option<&Value>,
    ) -> Result<Value> {
        let mut url = self.base.clone();
        url.set_path(&format!("/api/v1/{path}"));
        {
            let mut pairs = url.query_pairs_mut();
            for (key, value) in query {
                pairs.append_pair(key, value);
            }
        }
        let mut request = self
            .client
            .request(method, url)
            .basic_auth(&self.username, Some(&self.password))
            .header(reqwest::header::ACCEPT, "application/json");
        if let Some(body) = body {
            request = request.json(body);
        }
        let response = request.send().await.context("request failed")?;
        let status = response.status();
        let text = response
            .text()
            .await
            .context("failed to read response body")?;
        if !status.is_success() {
            let message = extract_error_message(&text).unwrap_or_else(|| status.to_string());
            match status {
                StatusCode::UNAUTHORIZED | StatusCode::FORBIDDEN => bail!("AUTH::{message}"),
                StatusCode::CONFLICT | StatusCode::PRECONDITION_FAILED => {
                    bail!("CONFLICT::{message}")
                }
                StatusCode::BAD_REQUEST
                | StatusCode::NOT_FOUND
                | StatusCode::METHOD_NOT_ALLOWED => {
                    bail!("INPUT::{message}")
                }
                _ => bail!("OTHER::{message}"),
            }
        }
        if text.trim().is_empty() {
            return Ok(Value::Null);
        }
        serde_json::from_str(&text)
            .with_context(|| format!("failed to parse JSON response: {text}"))
    }
}

fn extract_error_message(body: &str) -> Option<String> {
    let value = serde_json::from_str::<Value>(body).ok()?;
    value
        .get("message")
        .and_then(Value::as_str)
        .or_else(|| value.get("error").and_then(Value::as_str))
        .map(ToOwned::to_owned)
}

fn extract_value(root: &Value) -> Value {
    root.get("value").cloned().unwrap_or_else(|| root.clone())
}

async fn run_zone_command(runtime: &Runtime, cli: &Cli, command: ZoneCommand) -> Result<()> {
    match command {
        ZoneCommand::List { tenant, filter } => {
            let mut query = Vec::new();
            if let Some(tenant) = tenant {
                query.push(("tenant", tenant));
            }
            let response = runtime.api.get_json("zone", &query).await?;
            let mut items = extract_value(&response)
                .as_array()
                .cloned()
                .unwrap_or_default();
            if let Some(filter) = filter {
                items.retain(|item| item.to_string().contains(&filter));
            }
            print_value(&runtime.output, &Value::Array(items))?;
        }
        ZoneCommand::Get { zone, tenant } => {
            let document = export_zone_document(runtime, &zone, tenant.as_deref()).await?;
            print_value(&runtime.output, &document)?;
        }
        ZoneCommand::Create {
            zone,
            tenant,
            primary_ns,
            admin,
        } => {
            let body = bootstrap_zone_payload(&zone, primary_ns.as_deref(), admin.as_deref())?;
            execute_write(
                runtime,
                cli,
                json!({
                    "action": "zone.create",
                    "zone": zone,
                    "tenant": tenant,
                    "body": body
                }),
                || async {
                    runtime
                        .api
                        .send(
                            Method::POST,
                            &format!("zone/{zone}"),
                            &[("tenant", tenant)],
                            Some(&body),
                        )
                        .await
                },
            )
            .await?;
        }
        ZoneCommand::Delete { zone } => {
            require_yes(runtime.yes, "zone delete")?;
            execute_write(
                runtime,
                cli,
                json!({"action": "zone.delete", "zone": zone}),
                || async {
                    runtime
                        .api
                        .send(Method::DELETE, &format!("zone/{zone}"), &[], None)
                        .await
                },
            )
            .await?;
        }
        ZoneCommand::Export { zone, file, tenant } => {
            let document = export_zone_document(runtime, &zone, tenant.as_deref()).await?;
            write_json_file(&file, &document)?;
            print_serialized(&runtime.output, &json!({"file": file, "zone": zone}))?;
        }
        ZoneCommand::Import {
            file,
            upsert,
            conflict,
        } => {
            let mode = import_mode(upsert, conflict);
            let document = read_json_file(&file)?;
            let summary = import_zone_document(runtime, cli, &document, mode).await?;
            print_value(&runtime.output, &summary)?;
        }
    }
    Ok(())
}

async fn run_rr_command(runtime: &Runtime, cli: &Cli, command: RrCommand) -> Result<()> {
    match command {
        RrCommand::List {
            zone,
            name,
            rr_type,
        } => {
            let entries = fetch_zone_entries(runtime, &zone, None).await?;
            let mut records = flatten_zone_entries(&zone, &entries)?;
            if let Some(name) = name {
                let fqdn = absolute_name(&zone, &name);
                records.retain(|record| record.fqdn.eq_ignore_ascii_case(&fqdn));
            }
            if let Some(rr_type) = rr_type {
                let rr_type = rr_type.to_ascii_uppercase();
                records.retain(|record| record.rr_type == rr_type);
            }
            print_serialized(&runtime.output, &records)?;
        }
        RrCommand::Add {
            zone,
            name,
            rr_type,
            ttl,
            value,
            priority,
            weight,
            port,
        } => {
            let record =
                make_flat_record(&zone, &name, &rr_type, ttl, &value, priority, weight, port)?;
            let body = entry_body_for_records(&record.fqdn, std::slice::from_ref(&record), None)?;
            execute_write(
                runtime,
                cli,
                json!({"action": "rr.add", "fqdn": record.fqdn, "record": record}),
                || async {
                    runtime
                        .api
                        .send(
                            Method::POST,
                            &format!("rr/{}", record.fqdn),
                            &[],
                            Some(&body),
                        )
                        .await
                },
            )
            .await?;
        }
        RrCommand::Update {
            zone,
            id,
            name,
            rr_type,
            ttl,
            value,
            priority,
            weight,
            port,
        } => {
            let mut original = decode_record_id(&id)?;
            if !original.zone.eq_ignore_ascii_case(&zone) {
                bail!("record id does not belong to zone {zone}");
            }
            let entries = fetch_zone_entries(runtime, &zone, None).await?;
            let records = flatten_zone_entries(&zone, &entries)?;
            let current = records
                .into_iter()
                .find(|record| record.id == id)
                .ok_or_else(|| anyhow!("record not found"))?;
            let mut updated = current.clone();
            if let Some(name) = name {
                updated.fqdn = absolute_name(&zone, &name);
                updated.name = name;
            }
            if let Some(rr_type) = rr_type {
                updated.rr_type = rr_type.to_ascii_uppercase();
            }
            if let Some(ttl) = ttl {
                updated.ttl = ttl;
            }
            if let Some(value) = value {
                updated.value = value;
            }
            if priority.is_some() {
                updated.priority = priority;
            }
            if weight.is_some() {
                updated.weight = weight;
            }
            if port.is_some() {
                updated.port = port;
            }
            updated.id = crate::records::make_record_id(&updated);
            original.id = id;
            let summary = apply_record_update(runtime, cli, &original, &updated).await?;
            print_value(&runtime.output, &summary)?;
        }
        RrCommand::Delete { zone, id } => {
            require_yes(runtime.yes, "rr delete")?;
            let record = decode_record_id(&id)?;
            if !record.zone.eq_ignore_ascii_case(&zone) {
                bail!("record id does not belong to zone {zone}");
            }
            let summary = delete_one_record(runtime, cli, &record).await?;
            print_value(&runtime.output, &summary)?;
        }
        RrCommand::BulkAdd {
            csv,
            upsert,
            strict,
        } => {
            let rows = parse_bulk_add_csv(&csv, strict)?;
            let summary = bulk_add_records(runtime, cli, rows, upsert).await?;
            print_value(&runtime.output, &summary)?;
        }
        RrCommand::BulkDelete { csv } => {
            require_yes(runtime.yes, "rr bulk-delete")?;
            let rows = parse_bulk_delete_csv(&csv)?;
            let summary = bulk_delete_records(runtime, cli, rows).await?;
            print_value(&runtime.output, &summary)?;
        }
    }
    Ok(())
}

async fn run_tenant_command(runtime: &Runtime, cli: &Cli, command: TenantCommand) -> Result<()> {
    match command {
        TenantCommand::List => {
            let response = runtime
                .api
                .get_json("tenant", &[("kind", "brief".to_string())])
                .await?;
            print_value(&runtime.output, &extract_value(&response))?;
        }
        TenantCommand::Get { tenant } => {
            let response = runtime
                .api
                .get_json(&format!("tenant/{tenant}"), &[])
                .await?;
            print_value(&runtime.output, &extract_value(&response))?;
        }
        TenantCommand::Create {
            tenant,
            display_name,
        } => {
            let body = tenant_body(&tenant, display_name.as_deref());
            execute_write(
                runtime,
                cli,
                json!({"action": "tenant.create", "tenant": tenant, "body": body}),
                || async {
                    runtime
                        .api
                        .send(Method::POST, "tenant", &[], Some(&body))
                        .await
                },
            )
            .await?;
        }
        TenantCommand::Delete { tenant } => {
            require_yes(runtime.yes, "tenant delete")?;
            execute_write(
                runtime,
                cli,
                json!({"action": "tenant.delete", "tenant": tenant}),
                || async {
                    runtime
                        .api
                        .send(Method::DELETE, &format!("tenant/{tenant}"), &[], None)
                        .await
                },
            )
            .await?;
        }
        TenantCommand::Export { tenant, file } => {
            let document = export_tenant_document(runtime, &tenant).await?;
            write_json_file(&file, &document)?;
            print_serialized(&runtime.output, &json!({"file": file, "tenant": tenant}))?;
        }
        TenantCommand::Import {
            file,
            upsert,
            conflict,
        } => {
            let mode = import_mode(upsert, conflict);
            let document = read_json_file(&file)?;
            let summary = import_tenant_document(runtime, cli, &document, mode).await?;
            print_value(&runtime.output, &summary)?;
        }
    }
    Ok(())
}

async fn run_import_command(runtime: &Runtime, cli: &Cli, command: ImportCommand) -> Result<()> {
    match command {
        ImportCommand::Zone {
            file,
            upsert,
            conflict,
        } => {
            let document = read_json_file(&file)?;
            let summary =
                import_zone_document(runtime, cli, &document, import_mode(upsert, conflict))
                    .await?;
            print_value(&runtime.output, &summary)?;
        }
        ImportCommand::Tenant {
            file,
            upsert,
            conflict,
        } => {
            let document = read_json_file(&file)?;
            let summary =
                import_tenant_document(runtime, cli, &document, import_mode(upsert, conflict))
                    .await?;
            print_value(&runtime.output, &summary)?;
        }
    }
    Ok(())
}

async fn run_export_command(runtime: &Runtime, _cli: &Cli, command: ExportCommand) -> Result<()> {
    match command {
        ExportCommand::Zone { zone, file, tenant } => {
            let document = export_zone_document(runtime, &zone, tenant.as_deref()).await?;
            write_json_file(&file, &document)?;
            print_serialized(&runtime.output, &json!({"file": file, "zone": zone}))?;
        }
        ExportCommand::Tenant { tenant, file } => {
            let document = export_tenant_document(runtime, &tenant).await?;
            write_json_file(&file, &document)?;
            print_serialized(&runtime.output, &json!({"file": file, "tenant": tenant}))?;
        }
    }
    Ok(())
}

fn require_yes(yes: bool, operation: &str) -> Result<()> {
    if yes {
        Ok(())
    } else {
        bail!("{operation} requires --yes")
    }
}

async fn execute_write<F, Fut>(
    runtime: &Runtime,
    _cli: &Cli,
    preview: Value,
    action: F,
) -> Result<()>
where
    F: FnOnce() -> Fut,
    Fut: std::future::Future<Output = Result<Value>>,
{
    if runtime.dry_run {
        print_value(
            &runtime.output,
            &json!({"dry_run": true, "preview": preview}),
        )?;
        return Ok(());
    }
    let response = action().await?;
    let out = if response.is_null() {
        preview
    } else {
        extract_value(&response)
    };
    print_value(&runtime.output, &out)?;
    Ok(())
}

fn bootstrap_zone_payload(
    zone: &str,
    primary_ns: Option<&str>,
    admin: Option<&str>,
) -> Result<Value> {
    let ns = primary_ns
        .map(ToOwned::to_owned)
        .unwrap_or_else(|| format!("ns1.{zone}"));
    let secondary_ns = format!("ns2.{zone}");
    let admin = admin
        .map(ToOwned::to_owned)
        .unwrap_or_else(|| format!("hostmaster.{zone}"));
    Ok(json!({
        "ttl": 300,
        "soa": {
            "mname": ns,
            "rname": admin,
            "refresh": 3600,
            "retry": 600,
            "expire": 1209600,
            "minimum": 300
        },
        "ns": [
            primary_ns.unwrap_or(&ns),
            secondary_ns
        ]
    }))
}

fn tenant_body(name: &str, display_name: Option<&str>) -> Value {
    let mut body = json!({
        "name": name,
        "active": true,
    });
    if let Some(display_name) = display_name {
        body["properties"] = json!([{"key": "display_name", "value": display_name}]);
    }
    body
}

fn import_mode(upsert: bool, conflict: ConflictMode) -> ConflictMode {
    if upsert {
        ConflictMode::Overwrite
    } else {
        conflict
    }
}

async fn export_zone_document(
    runtime: &Runtime,
    zone: &str,
    tenant: Option<&str>,
) -> Result<Value> {
    let tenant_name = match tenant {
        Some(tenant) => tenant.to_string(),
        None => discover_zone_tenant(runtime, zone).await?,
    };
    let entries = fetch_zone_entries(runtime, zone, Some(&tenant_name)).await?;
    let apex = entries
        .iter()
        .find(|entry| entry.get("fqdn").and_then(Value::as_str) == Some(zone))
        .ok_or_else(|| anyhow!("zone apex entry not found"))?;
    let soa = apex
        .get("soa")
        .cloned()
        .ok_or_else(|| anyhow!("zone apex entry missing soa"))?;
    let mut records = flatten_zone_entries(zone, &entries)?;
    records.retain(|record| record.rr_type != "SOA");
    Ok(json!({
        "schema_version": "nsblast.zone.v1",
        "zone": {
            "name": zone,
            "tenant": tenant_name,
            "soa": soa
        },
        "records": records
            .into_iter()
            .map(|record| json!({
                "name": record.name,
                "type": record.rr_type,
                "ttl": record.ttl,
                "value": record.value,
                "priority": record.priority,
                "weight": record.weight,
                "port": record.port
            }))
            .collect::<Vec<_>>()
    }))
}

async fn export_tenant_document(runtime: &Runtime, tenant: &str) -> Result<Value> {
    let tenant_value = extract_value(
        &runtime
            .api
            .get_json(&format!("tenant/{tenant}"), &[])
            .await?,
    );
    let roles = extract_value(
        &runtime
            .api
            .get_json("role", &[("tenant", tenant.to_string())])
            .await?,
    );
    let users = extract_value(
        &runtime
            .api
            .get_json("user", &[("tenant", tenant.to_string())])
            .await?,
    );
    let zones_response = runtime
        .api
        .get_json("zone", &[("tenant", tenant.to_string())])
        .await?;
    let zones = extract_value(&zones_response)
        .as_array()
        .cloned()
        .unwrap_or_default();
    let mut zone_docs = Vec::new();
    for zone in zones {
        let zone_name = zone
            .as_str()
            .or_else(|| zone.get("zone").and_then(Value::as_str))
            .ok_or_else(|| anyhow!("invalid zone listing item"))?;
        zone_docs.push(export_zone_document(runtime, zone_name, Some(tenant)).await?);
    }

    Ok(json!({
        "schema_version": "nsblast.tenant.v1",
        "tenant": {
            "name": tenant_value.get("name").and_then(Value::as_str).unwrap_or(tenant),
            "id": tenant_value.get("id").cloned(),
            "active": tenant_value.get("active").cloned().unwrap_or(json!(true)),
            "root": tenant_value.get("root").cloned().unwrap_or(Value::Null),
            "properties": tenant_value.get("properties").cloned().unwrap_or_else(|| json!([]))
        },
        "access": {
            "roles": roles,
            "users": users
        },
        "zones": zone_docs
            .into_iter()
            .map(|zone_doc| json!({
                "name": zone_doc["zone"]["name"],
                "soa": zone_doc["zone"]["soa"],
                "records": zone_doc["records"]
            }))
            .collect::<Vec<_>>()
    }))
}

async fn discover_zone_tenant(runtime: &Runtime, zone: &str) -> Result<String> {
    let response = runtime
        .api
        .get_json("zone", &[("tenant", "*".to_string())])
        .await
        .context("zone export without --tenant requires permission to inspect all tenants")?;
    for item in extract_value(&response)
        .as_array()
        .cloned()
        .unwrap_or_default()
    {
        if item.get("zone").and_then(Value::as_str) == Some(zone) {
            if let Some(tenant) = item.get("tenant").and_then(Value::as_str) {
                return Ok(tenant.to_string());
            }
        }
    }
    bail!("unable to determine owning tenant for zone {zone}; pass --tenant explicitly")
}

async fn fetch_zone_entries(
    runtime: &Runtime,
    zone: &str,
    tenant: Option<&str>,
) -> Result<Vec<Value>> {
    let mut query = vec![("kind", "verbose".to_string())];
    if let Some(tenant) = tenant {
        query.push(("tenant", tenant.to_string()));
    }
    let response = runtime
        .api
        .get_json(&format!("zone/{zone}"), &query)
        .await?;
    Ok(extract_value(&response)
        .as_array()
        .cloned()
        .unwrap_or_default())
}

fn flatten_zone_entries(zone: &str, entries: &[Value]) -> Result<Vec<FlatRecord>> {
    let mut out = Vec::new();
    for entry in entries {
        out.extend(flatten_entry(zone, entry)?);
    }
    Ok(out)
}

fn make_flat_record(
    zone: &str,
    name: &str,
    rr_type: &str,
    ttl: u32,
    value: &str,
    priority: Option<u16>,
    weight: Option<u16>,
    port: Option<u16>,
) -> Result<FlatRecord> {
    let rr_type = rr_type.to_ascii_uppercase();
    if ttl == 0 {
        bail!("ttl must be positive");
    }
    let record = FlatRecord {
        id: String::new(),
        fqdn: absolute_name(zone, name),
        zone: zone.to_string(),
        name: name.to_string(),
        rr_type,
        ttl,
        value: value.to_string(),
        priority,
        weight,
        port,
    };
    let mut record = validate_record(record)?;
    record.id = crate::records::make_record_id(&record);
    Ok(record)
}

fn validate_record(record: FlatRecord) -> Result<FlatRecord> {
    match record.rr_type.as_str() {
        "A" | "AAAA" | "TXT" | "NS" | "PTR" | "CNAME" => Ok(record),
        "MX" => {
            if record.priority.is_none() {
                bail!("mx requires --priority");
            }
            Ok(record)
        }
        "SRV" => {
            if record.priority.is_none() || record.weight.is_none() || record.port.is_none() {
                bail!("srv requires --priority, --weight, and --port");
            }
            Ok(record)
        }
        other => bail!("unsupported rr type: {other}"),
    }
}

fn entry_body_for_records(fqdn: &str, records: &[FlatRecord], soa: Option<Value>) -> Result<Value> {
    let mut normalized = records.to_vec();
    normalized.sort_by(|left, right| left.rr_type.cmp(&right.rr_type));
    let entry = build_entry(fqdn, &normalized, soa)?;
    let mut obj = entry
        .as_object()
        .cloned()
        .ok_or_else(|| anyhow!("entry must be object"))?;
    obj.remove("fqdn");
    Ok(Value::Object(obj))
}

async fn apply_record_update(
    runtime: &Runtime,
    cli: &Cli,
    original: &FlatRecord,
    updated: &FlatRecord,
) -> Result<Value> {
    let entries = fetch_zone_entries(runtime, &original.zone, None).await?;
    let records = flatten_zone_entries(&original.zone, &entries)?;
    let mut grouped: BTreeMap<String, Vec<FlatRecord>> = BTreeMap::new();
    for record in records
        .into_iter()
        .filter(|record| record.id != original.id)
    {
        grouped.entry(record.fqdn.clone()).or_default().push(record);
    }
    grouped
        .entry(updated.fqdn.clone())
        .or_default()
        .push(updated.clone());

    if runtime.dry_run {
        return Ok(json!({
            "dry_run": true,
            "action": "rr.update",
            "original": original,
            "updated": updated
        }));
    }

    if original.fqdn != updated.fqdn {
        reconcile_entry(
            runtime,
            &original.fqdn,
            grouped.get(&original.fqdn).cloned().unwrap_or_default(),
        )
        .await?;
    }
    reconcile_entry(
        runtime,
        &updated.fqdn,
        grouped.get(&updated.fqdn).cloned().unwrap_or_default(),
    )
    .await?;
    let out = json!({"updated": true, "id": updated.id, "fqdn": updated.fqdn});
    let _ = cli;
    Ok(out)
}

async fn delete_one_record(runtime: &Runtime, _cli: &Cli, record: &FlatRecord) -> Result<Value> {
    let entries = fetch_zone_entries(runtime, &record.zone, None).await?;
    let records = flatten_zone_entries(&record.zone, &entries)?;
    let remaining = records
        .into_iter()
        .filter(|existing| existing.id != record.id)
        .collect::<Vec<_>>();
    if runtime.dry_run {
        return Ok(
            json!({"dry_run": true, "action": "rr.delete", "id": record.id, "fqdn": record.fqdn}),
        );
    }
    let same_fqdn = remaining
        .into_iter()
        .filter(|existing| existing.fqdn == record.fqdn)
        .collect::<Vec<_>>();
    reconcile_entry(runtime, &record.fqdn, same_fqdn).await?;
    Ok(json!({"deleted": true, "id": record.id, "fqdn": record.fqdn}))
}

async fn reconcile_entry(runtime: &Runtime, fqdn: &str, records: Vec<FlatRecord>) -> Result<()> {
    if records.is_empty() {
        runtime
            .api
            .send(Method::DELETE, &format!("rr/{fqdn}"), &[], None)
            .await?;
        return Ok(());
    }
    let body = entry_body_for_records(fqdn, &records, None)?;
    runtime
        .api
        .send(Method::PUT, &format!("rr/{fqdn}"), &[], Some(&body))
        .await?;
    Ok(())
}

#[derive(Default, Serialize)]
struct BulkSummary {
    processed: usize,
    created: usize,
    updated: usize,
    deleted: usize,
    skipped: usize,
    failed: usize,
    #[serde(skip_serializing_if = "Vec::is_empty")]
    errors: Vec<String>,
}

fn parse_bulk_add_csv(path: &Path, strict: bool) -> Result<Vec<FlatRecord>> {
    let mut reader = csv::ReaderBuilder::new()
        .comment(Some(b'#'))
        .trim(Trim::All)
        .from_path(path)
        .with_context(|| format!("failed to open {}", path.display()))?;
    let headers = reader.headers()?.clone();
    let mut rows = Vec::new();
    let mut errors = Vec::new();
    for (index, result) in reader.records().enumerate() {
        let row_number = index + 2;
        let record = result.with_context(|| format!("invalid csv row {row_number}"))?;
        match parse_bulk_add_row(row_number, &headers, &record) {
            Ok(row) => rows.push(row),
            Err(err) => {
                errors.push(err.to_string());
                if strict {
                    break;
                }
            }
        }
    }
    if !errors.is_empty() {
        bail!("{}", errors.join("\n"));
    }
    Ok(rows)
}

fn parse_bulk_add_row(
    row_number: usize,
    headers: &csv::StringRecord,
    row: &csv::StringRecord,
) -> Result<FlatRecord> {
    let map = headers
        .iter()
        .zip(row.iter())
        .map(|(key, value)| (key, value))
        .collect::<BTreeMap<_, _>>();
    let zone = required_csv(&map, "zone", row_number)?;
    let name = required_csv(&map, "name", row_number)?;
    let rr_type = required_csv(&map, "type", row_number)?;
    let ttl = required_csv(&map, "ttl", row_number)?
        .parse::<u32>()
        .with_context(|| format!("row {row_number}, column ttl: invalid integer"))?;
    let value = required_csv(&map, "value", row_number)?;
    let priority = optional_csv(&map, "priority")?
        .map(|v| v.parse())
        .transpose()
        .with_context(|| format!("row {row_number}, column priority: invalid integer"))?;
    let weight = optional_csv(&map, "weight")?
        .map(|v| v.parse())
        .transpose()
        .with_context(|| format!("row {row_number}, column weight: invalid integer"))?;
    let port = optional_csv(&map, "port")?
        .map(|v| v.parse())
        .transpose()
        .with_context(|| format!("row {row_number}, column port: invalid integer"))?;
    make_flat_record(zone, name, rr_type, ttl, value, priority, weight, port)
}

fn required_csv<'a>(
    map: &BTreeMap<&'a str, &'a str>,
    column: &str,
    row_number: usize,
) -> Result<&'a str> {
    map.get(column)
        .copied()
        .filter(|value| !value.is_empty())
        .ok_or_else(|| anyhow!("row {row_number}, column {column}: missing value"))
}

fn optional_csv<'a>(map: &BTreeMap<&'a str, &'a str>, column: &str) -> Result<Option<&'a str>> {
    Ok(map.get(column).copied().filter(|value| !value.is_empty()))
}

async fn bulk_add_records(
    runtime: &Runtime,
    _cli: &Cli,
    rows: Vec<FlatRecord>,
    upsert: bool,
) -> Result<Value> {
    let mut summary = BulkSummary::default();
    let mut grouped: BTreeMap<String, Vec<FlatRecord>> = BTreeMap::new();
    for row in rows {
        grouped.entry(row.fqdn.clone()).or_default().push(row);
    }
    for (fqdn, records) in grouped {
        summary.processed += records.len();
        let body = entry_body_for_records(&fqdn, &records, None)?;
        if runtime.dry_run {
            summary.created += records.len();
            continue;
        }
        let method = if upsert { Method::PATCH } else { Method::POST };
        match runtime
            .api
            .send(method, &format!("rr/{fqdn}"), &[], Some(&body))
            .await
        {
            Ok(_) => {
                if upsert {
                    summary.updated += records.len();
                } else {
                    summary.created += records.len();
                }
            }
            Err(err) => {
                summary.failed += records.len();
                summary.errors.push(err.to_string());
            }
        }
    }
    Ok(serde_json::to_value(summary)?)
}

#[derive(Clone)]
enum BulkDeleteRow {
    ById(String),
    ByTuple {
        zone: String,
        name: String,
        rr_type: String,
        value: String,
    },
}

fn parse_bulk_delete_csv(path: &Path) -> Result<Vec<BulkDeleteRow>> {
    let mut reader = csv::ReaderBuilder::new()
        .comment(Some(b'#'))
        .trim(Trim::All)
        .from_path(path)
        .with_context(|| format!("failed to open {}", path.display()))?;
    let headers = reader.headers()?.clone();
    let mut rows = Vec::new();
    for (index, result) in reader.records().enumerate() {
        let row_number = index + 2;
        let record = result.with_context(|| format!("invalid csv row {row_number}"))?;
        let map = headers
            .iter()
            .zip(record.iter())
            .map(|(key, value)| (key, value))
            .collect::<BTreeMap<_, _>>();
        if let Some(id) = optional_csv(&map, "id")? {
            rows.push(BulkDeleteRow::ById(id.to_string()));
            continue;
        }
        rows.push(BulkDeleteRow::ByTuple {
            zone: required_csv(&map, "zone", row_number)?.to_string(),
            name: required_csv(&map, "name", row_number)?.to_string(),
            rr_type: required_csv(&map, "type", row_number)?.to_ascii_uppercase(),
            value: required_csv(&map, "value", row_number)?.to_string(),
        });
    }
    Ok(rows)
}

async fn bulk_delete_records(
    runtime: &Runtime,
    cli: &Cli,
    rows: Vec<BulkDeleteRow>,
) -> Result<Value> {
    let mut summary = BulkSummary::default();
    let mut zone_cache: BTreeMap<String, Vec<FlatRecord>> = BTreeMap::new();
    for row in rows {
        summary.processed += 1;
        let record = match row {
            BulkDeleteRow::ById(id) => decode_record_id(&id)?,
            BulkDeleteRow::ByTuple {
                zone,
                name,
                rr_type,
                value,
            } => {
                let records = match zone_cache.get(&zone) {
                    Some(records) => records.clone(),
                    None => {
                        let entries = fetch_zone_entries(runtime, &zone, None).await?;
                        let records = flatten_zone_entries(&zone, &entries)?;
                        zone_cache.insert(zone.clone(), records.clone());
                        records
                    }
                };
                let fqdn = absolute_name(&zone, &name);
                records
                    .into_iter()
                    .find(|record| {
                        record.fqdn.eq_ignore_ascii_case(&fqdn)
                            && record.rr_type == rr_type
                            && record.value == value
                    })
                    .ok_or_else(|| anyhow!("record not found for {fqdn} {rr_type} {value}"))?
            }
        };
        match delete_one_record(runtime, cli, &record).await {
            Ok(_) => summary.deleted += 1,
            Err(err) => {
                summary.failed += 1;
                summary.errors.push(err.to_string());
            }
        }
    }
    Ok(serde_json::to_value(summary)?)
}

async fn import_zone_document(
    runtime: &Runtime,
    _cli: &Cli,
    document: &Value,
    mode: ConflictMode,
) -> Result<Value> {
    if document.get("schema_version").and_then(Value::as_str) != Some("nsblast.zone.v1") {
        bail!("unsupported schema_version");
    }
    let zone = document
        .get("zone")
        .and_then(Value::as_object)
        .ok_or_else(|| anyhow!("missing zone object"))?;
    let zone_name = zone
        .get("name")
        .and_then(Value::as_str)
        .ok_or_else(|| anyhow!("missing zone.name"))?;
    let tenant = zone
        .get("tenant")
        .and_then(Value::as_str)
        .ok_or_else(|| anyhow!("missing zone.tenant"))?;
    let soa = zone
        .get("soa")
        .cloned()
        .ok_or_else(|| anyhow!("missing zone.soa"))?;
    let records = document
        .get("records")
        .and_then(Value::as_array)
        .ok_or_else(|| anyhow!("missing records"))?;
    let parsed = parse_export_records(zone_name, records)?;
    let grouped = group_records_by_fqdn(&parsed);
    let exists = runtime
        .api
        .get_json(
            &format!("zone/{zone_name}"),
            &[("kind", "verbose".to_string())],
        )
        .await
        .is_ok();
    match mode {
        ConflictMode::Fail if exists => bail!("CONFLICT::zone already exists"),
        ConflictMode::Skip if exists => {
            return Ok(json!({"processed": 1, "skipped": 1, "zone": zone_name}));
        }
        _ => {}
    }

    if runtime.dry_run {
        let mode_name = mode.to_possible_value().map(|v| v.get_name().to_string());
        return Ok(json!({"dry_run": true, "zone": zone_name, "mode": mode_name}));
    }

    if !exists {
        let apex_records = grouped.get(zone_name).cloned().unwrap_or_default();
        let body = entry_body_for_records(zone_name, &apex_records, Some(soa.clone()))?;
        runtime
            .api
            .send(
                Method::POST,
                &format!("zone/{zone_name}"),
                &[("tenant", tenant.to_string())],
                Some(&body),
            )
            .await?;
        for (fqdn, records) in grouped
            .iter()
            .filter(|(fqdn, _)| fqdn.as_str() != zone_name)
        {
            let body = entry_body_for_records(fqdn, records, None)?;
            runtime
                .api
                .send(
                    Method::POST,
                    &format!("rr/{fqdn}"),
                    &[("tenant", tenant.to_string())],
                    Some(&body),
                )
                .await?;
        }
        return Ok(json!({"processed": parsed.len(), "created": parsed.len(), "zone": zone_name}));
    }

    let existing_entries = fetch_zone_entries(runtime, zone_name, Some(tenant)).await?;
    let existing = flatten_zone_entries(zone_name, &existing_entries)?;
    let existing_groups = group_records_by_fqdn(&existing);
    let desired_keys = grouped.keys().cloned().collect::<BTreeSet<_>>();
    let existing_keys = existing_groups.keys().cloned().collect::<BTreeSet<_>>();
    for fqdn in existing_keys.difference(&desired_keys) {
        runtime
            .api
            .send(
                Method::DELETE,
                &format!("rr/{fqdn}"),
                &[("tenant", tenant.to_string())],
                None,
            )
            .await?;
    }
    for (fqdn, records) in &grouped {
        let body = entry_body_for_records(
            fqdn,
            records,
            if fqdn == zone_name {
                Some(soa.clone())
            } else {
                None
            },
        )?;
        let path = if fqdn == zone_name {
            format!("rr/{fqdn}")
        } else {
            format!("rr/{fqdn}")
        };
        runtime
            .api
            .send(
                Method::PUT,
                &path,
                &[("tenant", tenant.to_string())],
                Some(&body),
            )
            .await?;
    }
    Ok(json!({"processed": parsed.len(), "updated": parsed.len(), "zone": zone_name}))
}

async fn import_tenant_document(
    runtime: &Runtime,
    cli: &Cli,
    document: &Value,
    mode: ConflictMode,
) -> Result<Value> {
    if document.get("schema_version").and_then(Value::as_str) != Some("nsblast.tenant.v1") {
        bail!("unsupported schema_version");
    }
    let tenant = document
        .get("tenant")
        .and_then(Value::as_object)
        .ok_or_else(|| anyhow!("missing tenant object"))?;
    let tenant_name = tenant
        .get("name")
        .and_then(Value::as_str)
        .ok_or_else(|| anyhow!("missing tenant.name"))?;
    let exists = runtime
        .api
        .get_json(&format!("tenant/{tenant_name}"), &[])
        .await
        .is_ok();
    match mode {
        ConflictMode::Fail if exists => bail!("CONFLICT::tenant already exists"),
        ConflictMode::Skip if exists => {
            return Ok(json!({"processed": 1, "skipped": 1, "tenant": tenant_name}));
        }
        _ => {}
    }

    if runtime.dry_run {
        let mode_name = mode.to_possible_value().map(|v| v.get_name().to_string());
        return Ok(json!({"dry_run": true, "tenant": tenant_name, "mode": mode_name}));
    }

    let body = json!({
        "name": tenant_name,
        "active": tenant.get("active").cloned().unwrap_or(json!(true)),
        "root": tenant.get("root").cloned().unwrap_or(Value::Null),
        "properties": tenant.get("properties").cloned().unwrap_or_else(|| json!([]))
    });
    let method = if exists { Method::PUT } else { Method::POST };
    let path = if exists {
        format!("tenant/{tenant_name}")
    } else {
        "tenant".to_string()
    };
    runtime.api.send(method, &path, &[], Some(&body)).await?;

    if let Some(access) = document.get("access").and_then(Value::as_object) {
        for role in access
            .get("roles")
            .and_then(Value::as_array)
            .cloned()
            .unwrap_or_default()
        {
            let role_name = role
                .get("name")
                .and_then(Value::as_str)
                .ok_or_else(|| anyhow!("role missing name"))?;
            runtime
                .api
                .send(
                    Method::PUT,
                    &format!("role/{role_name}"),
                    &[("tenant", tenant_name.to_string())],
                    Some(&role),
                )
                .await?;
        }
        for user in access
            .get("users")
            .and_then(Value::as_array)
            .cloned()
            .unwrap_or_default()
        {
            let user_name = user
                .get("name")
                .and_then(Value::as_str)
                .ok_or_else(|| anyhow!("user missing name"))?;
            runtime
                .api
                .send(
                    Method::PUT,
                    &format!("user/{user_name}"),
                    &[("tenant", tenant_name.to_string())],
                    Some(&user),
                )
                .await?;
        }
    }

    for zone in document
        .get("zones")
        .and_then(Value::as_array)
        .cloned()
        .unwrap_or_default()
    {
        let zone_name = zone
            .get("name")
            .and_then(Value::as_str)
            .ok_or_else(|| anyhow!("zone missing name"))?;
        let zone_doc = json!({
            "schema_version": "nsblast.zone.v1",
            "zone": {
                "name": zone_name,
                "tenant": tenant_name,
                "soa": zone.get("soa").cloned().ok_or_else(|| anyhow!("zone missing soa"))?
            },
            "records": zone.get("records").cloned().unwrap_or_else(|| json!([]))
        });
        import_zone_document(runtime, cli, &zone_doc, mode.clone()).await?;
    }
    Ok(json!({"processed": 1, "tenant": tenant_name}))
}

fn parse_export_records(zone: &str, records: &[Value]) -> Result<Vec<FlatRecord>> {
    let mut out = Vec::new();
    for record in records {
        let name = record
            .get("name")
            .and_then(Value::as_str)
            .ok_or_else(|| anyhow!("record missing name"))?;
        let rr_type = record
            .get("type")
            .and_then(Value::as_str)
            .ok_or_else(|| anyhow!("record missing type"))?;
        let ttl = record
            .get("ttl")
            .and_then(Value::as_u64)
            .ok_or_else(|| anyhow!("record missing ttl"))? as u32;
        let value = record
            .get("value")
            .and_then(Value::as_str)
            .ok_or_else(|| anyhow!("record missing value"))?;
        let priority = record
            .get("priority")
            .and_then(Value::as_u64)
            .map(|v| v as u16);
        let weight = record
            .get("weight")
            .and_then(Value::as_u64)
            .map(|v| v as u16);
        let port = record.get("port").and_then(Value::as_u64).map(|v| v as u16);
        out.push(make_flat_record(
            zone, name, rr_type, ttl, value, priority, weight, port,
        )?);
    }
    Ok(out)
}

fn group_records_by_fqdn(records: &[FlatRecord]) -> BTreeMap<String, Vec<FlatRecord>> {
    let mut grouped = BTreeMap::new();
    for record in records {
        grouped
            .entry(record.fqdn.clone())
            .or_insert_with(Vec::new)
            .push(record.clone());
    }
    grouped
}

fn read_json_file(path: &Path) -> Result<Value> {
    let data =
        fs::read_to_string(path).with_context(|| format!("failed to read {}", path.display()))?;
    serde_json::from_str(&data).with_context(|| format!("failed to parse {}", path.display()))
}

fn write_json_file(path: &Path, value: &Value) -> Result<()> {
    let body = serde_json::to_string_pretty(value)?;
    fs::write(path, body).with_context(|| format!("failed to write {}", path.display()))
}

fn map_anyhow_exit(err: &anyhow::Error) -> i32 {
    let message = err.to_string();
    if message.starts_with("AUTH::") {
        EXIT_AUTH
    } else if message.starts_with("CONFLICT::") {
        EXIT_CONFLICT
    } else if message.starts_with("INPUT::") {
        EXIT_INPUT
    } else if message.contains("missing ")
        || message.contains("requires --yes")
        || message.contains("unsupported schema_version")
        || message.contains("refusing to use insecure")
    {
        EXIT_INPUT
    } else {
        EXIT_OTHER
    }
}
