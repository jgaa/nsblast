use std::fs::{File, OpenOptions};
use std::net::IpAddr;
use std::os::unix::fs::PermissionsExt;
use std::path::{Path, PathBuf};
use std::time::Duration;

use anyhow::{Context, Result};
use clap::{ArgAction, Parser};
use fs2::FileExt;
use reqwest::StatusCode;
use serde::{Deserialize, Serialize};
use tokio::process::Command;
use tokio::time::timeout;
use tracing::{debug, error, info, warn};
use tracing_subscriber::layer::SubscriberExt;
use tracing_subscriber::util::SubscriberInitExt;
use url::Url;

const DEFAULT_CONFIG_PATH: &str = "~/.config/nsblast-dynip/config.yaml";
const DEFAULT_REPEAT_MINUTES: u64 = 15;
const DEFAULT_TIMEOUT_SECONDS: u64 = 10;
const DEFAULT_LOCK_FILE: &str = "/tmp/nsblast-dynip.lock";
const ON_CHANGED_TIMEOUT_SECONDS: u64 = 30;

#[derive(Parser, Debug)]
#[command(name = "nsblast-dynip")]
#[command(about = "nsblast DynIP client")]
struct Cli {
    #[arg(long)]
    config: Option<PathBuf>,
    #[arg(long)]
    daemon: bool,
    #[arg(long)]
    repeat_minutes: Option<u64>,
    #[arg(long)]
    on_changed: Option<PathBuf>,
    #[arg(long)]
    fqdn: Option<String>,
    #[arg(long, action = ArgAction::Append)]
    ip: Vec<String>,
}

#[derive(Debug, Deserialize)]
struct FileConfig {
    url: Option<String>,
    token: Option<String>,
    #[serde(alias = "username")]
    auth_name: Option<String>,
    #[serde(alias = "password")]
    password: Option<String>,
    fqdn: Option<String>,
    ip: Option<String>,
    ips: Option<Vec<String>>,
    repeat_minutes: Option<u64>,
    lock_file: Option<PathBuf>,
    tls_ca_file: Option<PathBuf>,
    timeout_seconds: Option<u64>,
    client_ref: Option<String>,
}

#[derive(Debug, Clone)]
struct AppConfig {
    url: String,
    token: String,
    fqdn: String,
    ips: Vec<String>,
    repeat_minutes: u64,
    lock_file: PathBuf,
    tls_ca_file: Option<PathBuf>,
    timeout_seconds: u64,
    client_ref: Option<String>,
    on_changed: Option<PathBuf>,
    daemon: bool,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(i32)]
enum ExitCode {
    SuccessNoChange = 0,
    SuccessChanged = 2,
    AuthFailure = 3,
    NetworkFailure = 4,
    OtherError = 5,
}

#[derive(Debug)]
struct LockGuard {
    _file: File,
    path: PathBuf,
}

#[derive(Debug, Serialize)]
struct UpdateRequest<'a> {
    fqdn: &'a str,
    #[serde(skip_serializing_if = "Option::is_none")]
    ip: Option<&'a str>,
    #[serde(skip_serializing_if = "Option::is_none")]
    client_ref: Option<&'a str>,
}

#[derive(Debug, Deserialize)]
struct UpdateResponse {
    status: Option<String>,
    changed: Option<bool>,
    effective_ip: Option<String>,
    fqdn: Option<String>,
    #[serde(alias = "hostname")]
    response_fqdn: Option<String>,
}

#[derive(Debug)]
struct UpdateOutcome {
    changed: bool,
    effective_ip: String,
}

#[tokio::main]
async fn main() {
    let cli = Cli::parse();
    if let Err(err) = run(cli).await {
        std::process::exit(err as i32);
    }
}

async fn run(cli: Cli) -> std::result::Result<(), ExitCode> {
    init_logging(cli.daemon).map_err(|e| {
        eprintln!("failed to initialize logging: {e:#}");
        ExitCode::OtherError
    })?;

    let cfg = load_config(cli)?;
    let _lock = acquire_lock(&cfg.lock_file)?;

    let client = build_http_client(&cfg)?;

    if cfg.daemon {
        run_daemon(client, cfg).await;
        Ok(())
    } else {
        let result = update_once(&client, &cfg).await;
        match result {
            Ok(outcome) => {
                maybe_run_on_changed(
                    &cfg,
                    None,
                    Some(outcome.effective_ip.clone()),
                    outcome.changed,
                )
                .await;
                if outcome.changed {
                    Err(ExitCode::SuccessChanged)
                } else {
                    Err(ExitCode::SuccessNoChange)
                }
            }
            Err(code) => Err(code),
        }
    }
}

fn init_logging(daemon: bool) -> Result<()> {
    let has_journal = Path::new("/run/systemd/journal/socket").exists();

    let mut use_stdout = !daemon;
    if daemon && !has_journal {
        use_stdout = true;
    }

    let fmt_layer = if use_stdout {
        Some(
            tracing_subscriber::fmt::layer()
                .with_target(false)
                .compact(),
        )
    } else {
        None
    };

    let journald_layer = if has_journal {
        Some(tracing_journald::layer().context("unable to create journald layer")?)
    } else {
        None
    };

    let filter = tracing_subscriber::EnvFilter::try_from_default_env()
        .unwrap_or_else(|_| tracing_subscriber::EnvFilter::new("info"));

    tracing_subscriber::registry()
        .with(filter)
        .with(fmt_layer)
        .with(journald_layer)
        .init();

    if daemon && !has_journal {
        warn!("systemd journal is unavailable, logging to stdout instead");
    }

    Ok(())
}

fn load_config(cli: Cli) -> std::result::Result<AppConfig, ExitCode> {
    let config_path = cli
        .config
        .unwrap_or_else(|| expand_path(DEFAULT_CONFIG_PATH));

    validate_config_permissions(&config_path)?;

    let data = std::fs::read_to_string(&config_path).map_err(|err| {
        error!(path = %config_path.display(), "failed to read config: {err}");
        ExitCode::OtherError
    })?;

    let file_cfg: FileConfig = serde_yaml::from_str(&data).map_err(|err| {
        error!(path = %config_path.display(), "failed to parse YAML config: {err}");
        ExitCode::OtherError
    })?;

    let file_ips = collect_file_ips(&file_cfg);

    let fqdn = cli.fqdn.or(file_cfg.fqdn).ok_or_else(|| {
        error!("missing required config key: fqdn");
        ExitCode::OtherError
    })?;

    let url = file_cfg.url.ok_or_else(|| {
        error!("missing required config key: url");
        ExitCode::OtherError
    })?;

    if file_cfg.auth_name.is_some() {
        warn!("config key 'username' is deprecated and ignored for Bearer token auth");
    }

    let token = if let Some(token) = file_cfg.token {
        token
    } else if let Some(password) = file_cfg.password {
        warn!("config key 'password' is deprecated; use 'token' instead");
        password
    } else {
        error!("missing required config key: token");
        return Err(ExitCode::OtherError);
    };

    let repeat_minutes = cli
        .repeat_minutes
        .or(file_cfg.repeat_minutes)
        .unwrap_or(DEFAULT_REPEAT_MINUTES)
        .max(1);

    let cli_ips = cli.ip;
    let ips = if cli_ips.is_empty() {
        file_ips
    } else {
        cli_ips
    };
    let ips = validate_explicit_ips(ips)?;

    let lock_file = file_cfg
        .lock_file
        .unwrap_or_else(|| PathBuf::from(DEFAULT_LOCK_FILE));

    Ok(AppConfig {
        url,
        token,
        fqdn,
        ips,
        repeat_minutes,
        lock_file,
        tls_ca_file: file_cfg.tls_ca_file,
        timeout_seconds: file_cfg
            .timeout_seconds
            .unwrap_or(DEFAULT_TIMEOUT_SECONDS)
            .max(1),
        client_ref: file_cfg.client_ref,
        on_changed: cli.on_changed,
        daemon: cli.daemon,
    })
}

fn collect_file_ips(file_cfg: &FileConfig) -> Vec<String> {
    let mut ips = file_cfg.ips.clone().unwrap_or_default();
    if let Some(ip) = &file_cfg.ip {
        ips.push(ip.clone());
    }
    ips
}

fn validate_explicit_ips(ips: Vec<String>) -> std::result::Result<Vec<String>, ExitCode> {
    if ips.is_empty() {
        return Ok(ips);
    }

    let mut normalized = Vec::with_capacity(ips.len());
    for value in ips {
        let ip = value.parse::<IpAddr>().map_err(|err| {
            error!(ip = value, "invalid explicit ip: {err}");
            ExitCode::OtherError
        })?;
        normalized.push(ip.to_string());
    }

    if normalized.len() > 1 {
        error!(
            count = normalized.len(),
            "the current server endpoint accepts only one explicit IP per update request"
        );
        return Err(ExitCode::OtherError);
    }

    Ok(normalized)
}

fn expand_path(value: &str) -> PathBuf {
    PathBuf::from(shellexpand::tilde(value).to_string())
}

fn validate_config_permissions(path: &Path) -> std::result::Result<(), ExitCode> {
    let metadata = std::fs::metadata(path).map_err(|err| {
        error!(path = %path.display(), "failed to stat config file: {err}");
        ExitCode::OtherError
    })?;

    let mode = metadata.permissions().mode() & 0o777;
    if mode & 0o077 != 0 {
        error!(
            path = %path.display(),
            mode = format_args!("{mode:#o}"),
            "config permissions are too broad; requires 0600"
        );
        return Err(ExitCode::OtherError);
    }

    if let Some(parent) = path.parent() {
        if let Ok(parent_meta) = std::fs::metadata(parent) {
            let parent_mode = parent_meta.permissions().mode() & 0o777;
            if parent_mode & 0o077 != 0 {
                warn!(
                    path = %parent.display(),
                    mode = format_args!("{parent_mode:#o}"),
                    "config directory permissions are broader than recommended 0700"
                );
            }
        }
    }

    Ok(())
}

fn acquire_lock(path: &Path) -> std::result::Result<LockGuard, ExitCode> {
    let lock_file = OpenOptions::new()
        .create(true)
        .read(true)
        .write(true)
        .open(path)
        .map_err(|err| {
            error!(path = %path.display(), "failed to open lock file: {err}");
            ExitCode::OtherError
        })?;

    lock_file.try_lock_exclusive().map_err(|err| {
        warn!(path = %path.display(), "lock acquisition failed: {err}");
        ExitCode::OtherError
    })?;

    info!(path = %path.display(), "lock acquired");
    Ok(LockGuard {
        _file: lock_file,
        path: path.to_path_buf(),
    })
}

fn build_http_client(cfg: &AppConfig) -> std::result::Result<reqwest::Client, ExitCode> {
    let mut builder = reqwest::Client::builder().timeout(Duration::from_secs(cfg.timeout_seconds));

    if let Some(ca_path) = &cfg.tls_ca_file {
        let pem = std::fs::read(ca_path).map_err(|err| {
            error!(path = %ca_path.display(), "failed to read custom CA file: {err}");
            ExitCode::OtherError
        })?;

        let cert = reqwest::Certificate::from_pem(&pem).map_err(|err| {
            error!(path = %ca_path.display(), "invalid custom CA file: {err}");
            ExitCode::OtherError
        })?;
        builder = builder.add_root_certificate(cert);
    }

    builder.build().map_err(|err| {
        error!("failed to build HTTP client: {err}");
        ExitCode::OtherError
    })
}

async fn run_daemon(client: reqwest::Client, cfg: AppConfig) {
    let jitter = startup_jitter_seconds();
    info!(jitter_seconds = jitter, "daemon startup jitter");
    if !sleep_with_shutdown(Duration::from_secs(jitter)).await {
        info!("shutting down before first update");
        return;
    }

    let base_interval = Duration::from_secs(cfg.repeat_minutes * 60);
    let mut backoff = Duration::from_secs(1);
    let mut previous_ip: Option<String> = None;

    loop {
        match update_once(&client, &cfg).await {
            Ok(outcome) => {
                info!(
                    changed = outcome.changed,
                    effective_ip = %outcome.effective_ip,
                    "dynip update completed"
                );
                maybe_run_on_changed(
                    &cfg,
                    previous_ip.clone(),
                    Some(outcome.effective_ip.clone()),
                    outcome.changed,
                )
                .await;
                previous_ip = Some(outcome.effective_ip);
                backoff = Duration::from_secs(1);

                if !sleep_with_shutdown(base_interval).await {
                    info!("received shutdown signal");
                    return;
                }
            }
            Err(code) => {
                warn!(exit_code = code as i32, "dynip update failed");
                let sleep_for = if backoff > base_interval {
                    base_interval
                } else {
                    backoff
                };

                if !sleep_with_shutdown(sleep_for).await {
                    info!("received shutdown signal");
                    return;
                }

                backoff = std::cmp::min(backoff.saturating_mul(2), base_interval);
            }
        }
    }
}

async fn sleep_with_shutdown(duration: Duration) -> bool {
    tokio::select! {
        _ = tokio::signal::ctrl_c() => false,
        _ = tokio::time::sleep(duration) => true,
    }
}

fn startup_jitter_seconds() -> u64 {
    let nanos = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.subsec_nanos() as u64)
        .unwrap_or(0);
    nanos % 31
}

async fn update_once(
    client: &reqwest::Client,
    cfg: &AppConfig,
) -> std::result::Result<UpdateOutcome, ExitCode> {
    let endpoint = build_endpoint_url(&cfg.url).map_err(|err| {
        error!("invalid URL: {err}");
        ExitCode::OtherError
    })?;

    let payload = UpdateRequest {
        fqdn: &cfg.fqdn,
        ip: cfg.ips.first().map(String::as_str),
        client_ref: cfg.client_ref.as_deref(),
    };

    debug!(
        endpoint = %endpoint,
        fqdn = %cfg.fqdn,
        explicit_ip = ?cfg.ips.first(),
        "sending dynip update request"
    );

    let response = client
        .post(endpoint)
        .bearer_auth(&cfg.token)
        .header(reqwest::header::ACCEPT, "application/json")
        .json(&payload)
        .send()
        .await
        .map_err(|err| {
            warn!("request failed: {err}");
            ExitCode::NetworkFailure
        })?;

    let status = response.status();
    if status != StatusCode::OK {
        return Err(map_http_status(status));
    }

    let parsed: UpdateResponse = response.json().await.map_err(|err| {
        error!("failed to parse JSON response: {err}");
        ExitCode::OtherError
    })?;

    interpret_success(parsed, &cfg.fqdn)
}

fn build_endpoint_url(base: &str) -> Result<Url> {
    let mut url = Url::parse(base).context("failed to parse base URL")?;
    url.set_path("/api/v1/dynip/update");
    url.set_query(None);
    Ok(url)
}

fn map_http_status(status: StatusCode) -> ExitCode {
    match status {
        StatusCode::UNAUTHORIZED | StatusCode::FORBIDDEN => ExitCode::AuthFailure,
        StatusCode::TOO_MANY_REQUESTS
        | StatusCode::INTERNAL_SERVER_ERROR
        | StatusCode::SERVICE_UNAVAILABLE => ExitCode::NetworkFailure,
        _ => ExitCode::OtherError,
    }
}

fn interpret_success(
    response: UpdateResponse,
    expected_fqdn: &str,
) -> std::result::Result<UpdateOutcome, ExitCode> {
    let status_value = response.status.ok_or_else(|| {
        error!("missing response field: status");
        ExitCode::OtherError
    })?;

    let changed = response.changed.ok_or_else(|| {
        error!("missing response field: changed");
        ExitCode::OtherError
    })?;

    let effective_ip = response.effective_ip.ok_or_else(|| {
        error!("missing response field: effective_ip");
        ExitCode::OtherError
    })?;

    let response_fqdn = response.fqdn.or(response.response_fqdn).ok_or_else(|| {
        error!("missing response field: fqdn");
        ExitCode::OtherError
    })?;

    if response_fqdn != expected_fqdn {
        warn!(
            response_fqdn,
            expected_fqdn, "response hostname differs from request"
        );
    }

    match status_value.as_str() {
        "good" | "nochg" => Ok(UpdateOutcome {
            changed,
            effective_ip,
        }),
        other => {
            error!(
                status = other,
                "unexpected status token in success response"
            );
            Err(ExitCode::OtherError)
        }
    }
}

async fn maybe_run_on_changed(
    cfg: &AppConfig,
    previous_ip: Option<String>,
    new_ip: Option<String>,
    changed: bool,
) {
    if !changed {
        return;
    }

    let Some(script) = &cfg.on_changed else {
        return;
    };

    let mut cmd = Command::new(script);
    cmd.env("NSBLAST_DYNIP_FQDN", &cfg.fqdn);
    cmd.env("NSBLAST_DYNIP_NEW_IP", new_ip.clone().unwrap_or_default());
    if let Some(prev) = previous_ip {
        cmd.env("NSBLAST_DYNIP_PREV_IP", prev);
    }

    match timeout(
        Duration::from_secs(ON_CHANGED_TIMEOUT_SECONDS),
        cmd.status(),
    )
    .await
    {
        Ok(Ok(status)) => {
            if !status.success() {
                warn!(
                    script = %script.display(),
                    code = status.code().unwrap_or(-1),
                    "on-changed script failed"
                );
            }
        }
        Ok(Err(err)) => {
            warn!(script = %script.display(), "failed to execute on-changed script: {err}");
        }
        Err(_) => {
            warn!(script = %script.display(), "on-changed script timed out");
        }
    }
}

impl Drop for LockGuard {
    fn drop(&mut self) {
        info!(path = %self.path.display(), "releasing lock");
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::os::unix::fs::PermissionsExt;

    #[test]
    fn test_map_http_status() {
        assert_eq!(
            map_http_status(StatusCode::UNAUTHORIZED),
            ExitCode::AuthFailure
        );
        assert_eq!(
            map_http_status(StatusCode::FORBIDDEN),
            ExitCode::AuthFailure
        );
        assert_eq!(
            map_http_status(StatusCode::TOO_MANY_REQUESTS),
            ExitCode::NetworkFailure
        );
        assert_eq!(
            map_http_status(StatusCode::INTERNAL_SERVER_ERROR),
            ExitCode::NetworkFailure
        );
        assert_eq!(
            map_http_status(StatusCode::SERVICE_UNAVAILABLE),
            ExitCode::NetworkFailure
        );
        assert_eq!(
            map_http_status(StatusCode::BAD_REQUEST),
            ExitCode::OtherError
        );
    }

    #[test]
    fn test_interpret_success_ok() {
        let response = UpdateResponse {
            status: Some("good".to_string()),
            changed: Some(true),
            effective_ip: Some("203.0.113.10".to_string()),
            fqdn: Some("host.example.com".to_string()),
            response_fqdn: None,
        };

        let outcome = interpret_success(response, "host.example.com").expect("valid response");
        assert!(outcome.changed);
        assert_eq!(outcome.effective_ip, "203.0.113.10");
    }

    #[test]
    fn test_interpret_success_missing_field() {
        let response = UpdateResponse {
            status: Some("nochg".to_string()),
            changed: None,
            effective_ip: Some("203.0.113.10".to_string()),
            fqdn: Some("host.example.com".to_string()),
            response_fqdn: None,
        };

        let err = interpret_success(response, "host.example.com").expect_err("must fail");
        assert_eq!(err, ExitCode::OtherError);
    }

    #[test]
    fn test_interpret_success_accepts_legacy_hostname_alias() {
        let response = UpdateResponse {
            status: Some("good".to_string()),
            changed: Some(true),
            effective_ip: Some("203.0.113.10".to_string()),
            fqdn: None,
            response_fqdn: Some("host.example.com".to_string()),
        };

        let outcome = interpret_success(response, "host.example.com").expect("valid response");
        assert!(outcome.changed);
        assert_eq!(outcome.effective_ip, "203.0.113.10");
    }

    #[test]
    fn test_build_endpoint_url_uses_primary_dynip_path() {
        let url = build_endpoint_url("https://dns.example.com/base").expect("url");
        assert_eq!(url.as_str(), "https://dns.example.com/api/v1/dynip/update");
    }

    #[test]
    fn test_validate_explicit_ips_accepts_single_ip() {
        let ips = validate_explicit_ips(vec!["203.0.113.10".to_string()]).expect("valid");
        assert_eq!(ips, vec!["203.0.113.10".to_string()]);
    }

    #[test]
    fn test_validate_explicit_ips_rejects_invalid_ip() {
        let err = validate_explicit_ips(vec!["not-an-ip".to_string()]).expect_err("must fail");
        assert_eq!(err, ExitCode::OtherError);
    }

    #[test]
    fn test_validate_explicit_ips_rejects_multiple_values_for_current_server() {
        let err =
            validate_explicit_ips(vec!["203.0.113.10".to_string(), "2001:db8::1".to_string()])
                .expect_err("must fail");
        assert_eq!(err, ExitCode::OtherError);
    }

    #[test]
    fn test_validate_permissions_rejects_world_readable() {
        let dir = tempfile::tempdir().expect("tempdir");
        let path = dir.path().join("config.yaml");
        std::fs::write(&path, "fqdn: host.example.com\n").expect("write");
        std::fs::set_permissions(&path, std::fs::Permissions::from_mode(0o644)).expect("chmod");

        let err = validate_config_permissions(&path).expect_err("must reject");
        assert_eq!(err, ExitCode::OtherError);
    }

    #[test]
    fn test_validate_permissions_accepts_owner_only() {
        let dir = tempfile::tempdir().expect("tempdir");
        let path = dir.path().join("config.yaml");
        std::fs::write(&path, "fqdn: host.example.com\n").expect("write");
        std::fs::set_permissions(&path, std::fs::Permissions::from_mode(0o600)).expect("chmod");

        validate_config_permissions(&path).expect("must pass");
    }
}
