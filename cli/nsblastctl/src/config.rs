use std::collections::BTreeMap;
use std::fs;
use std::io::Write;
use std::os::unix::fs::OpenOptionsExt;
use std::os::unix::fs::PermissionsExt;
use std::path::{Path, PathBuf};

use anyhow::{Context, Result, bail};
use dirs::config_dir;
use keyring::Entry;
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use url::Url;

use crate::cli::OutputFormat;

pub const CONFIG_SERVICE: &str = "io.nsblast.nsblastctl";
pub const CONFIG_NAMESPACE: &str = "io.nsblast.nsblastctl/basic-auth/v1";

#[derive(Clone, Debug, Default, Deserialize, Serialize)]
pub struct StoredProfile {
    pub server: Option<String>,
    pub username: Option<String>,
    pub password_source: Option<String>,
    pub password_key_id: Option<String>,
    pub password: Option<String>,
    pub timeout: Option<u64>,
    pub output: Option<OutputFormat>,
}

#[derive(Clone, Debug, Default, Deserialize, Serialize)]
pub struct ConfigFile {
    pub server: Option<String>,
    pub username: Option<String>,
    pub password_source: Option<String>,
    pub password_key_id: Option<String>,
    pub password: Option<String>,
    pub timeout: Option<u64>,
    pub output: Option<OutputFormat>,
    pub profile: Option<String>,
    #[serde(default)]
    pub profiles: BTreeMap<String, StoredProfile>,
}

#[derive(Clone, Debug, Default)]
pub struct EffectiveProfile {
    pub server: Option<String>,
    pub username: Option<String>,
    pub password_source: Option<String>,
    pub password_key_id: Option<String>,
    pub password: Option<String>,
    pub timeout: Option<u64>,
    pub output: Option<OutputFormat>,
}

pub fn config_path() -> Result<PathBuf> {
    let base = config_dir().context("unable to resolve config dir")?;
    Ok(base.join("nsblast").join("nsblastctl.yaml"))
}

pub fn load_config(path: &Path, allow_insecure: bool) -> Result<ConfigFile> {
    if !path.exists() {
        return Ok(ConfigFile::default());
    }
    validate_permissions(path, allow_insecure)?;
    let data = fs::read_to_string(path)
        .with_context(|| format!("failed to read config {}", path.display()))?;
    let cfg = serde_yaml::from_str(&data)
        .with_context(|| format!("failed to parse config {}", path.display()))?;
    Ok(cfg)
}

pub fn save_config(path: &Path, cfg: &ConfigFile) -> Result<()> {
    if let Some(parent) = path.parent() {
        if !parent.exists() {
            fs::create_dir_all(parent)
                .with_context(|| format!("failed to create {}", parent.display()))?;
        }
        fs::set_permissions(parent, fs::Permissions::from_mode(0o700))
            .with_context(|| format!("failed to secure {}", parent.display()))?;
    }
    let body = serde_yaml::to_string(cfg).context("failed to serialize config")?;
    let mut file = fs::OpenOptions::new()
        .create(true)
        .truncate(true)
        .write(true)
        .mode(0o600)
        .open(path)
        .with_context(|| format!("failed to open {}", path.display()))?;
    file.write_all(body.as_bytes())
        .with_context(|| format!("failed to write {}", path.display()))?;
    fs::set_permissions(path, fs::Permissions::from_mode(0o600))
        .with_context(|| format!("failed to secure {}", path.display()))?;
    Ok(())
}

pub fn select_profile(cfg: &ConfigFile, requested: Option<&str>) -> EffectiveProfile {
    let profile_name = requested
        .map(ToOwned::to_owned)
        .or_else(|| cfg.profile.clone())
        .unwrap_or_else(|| "default".to_string());
    let mut effective = EffectiveProfile {
        server: cfg.server.clone(),
        username: cfg.username.clone(),
        password_source: cfg.password_source.clone(),
        password_key_id: cfg.password_key_id.clone(),
        password: cfg.password.clone(),
        timeout: cfg.timeout,
        output: cfg.output.clone(),
    };
    if let Some(profile) = cfg.profiles.get(&profile_name) {
        if profile.server.is_some() {
            effective.server = profile.server.clone();
        }
        if profile.username.is_some() {
            effective.username = profile.username.clone();
        }
        if profile.password_source.is_some() {
            effective.password_source = profile.password_source.clone();
        }
        if profile.password_key_id.is_some() {
            effective.password_key_id = profile.password_key_id.clone();
        }
        if profile.password.is_some() {
            effective.password = profile.password.clone();
        }
        if profile.timeout.is_some() {
            effective.timeout = profile.timeout;
        }
        if profile.output.is_some() {
            effective.output = profile.output.clone();
        }
    }
    effective
}

pub fn upsert_profile(cfg: &mut ConfigFile, profile_name: &str, profile: StoredProfile) {
    if profile_name == "default" && cfg.profiles.is_empty() {
        cfg.server = profile.server;
        cfg.username = profile.username;
        cfg.password_source = profile.password_source;
        cfg.password_key_id = profile.password_key_id;
        cfg.password = profile.password;
        cfg.timeout = profile.timeout;
        cfg.output = profile.output;
        cfg.profile = Some(profile_name.to_string());
    } else {
        cfg.profile = Some(profile_name.to_string());
        cfg.profiles.insert(profile_name.to_string(), profile);
    }
}

pub fn validate_permissions(path: &Path, allow_insecure: bool) -> Result<()> {
    let metadata =
        fs::metadata(path).with_context(|| format!("failed to stat {}", path.display()))?;
    let mode = metadata.permissions().mode() & 0o777;
    if !allow_insecure && mode & 0o077 != 0 {
        bail!(
            "refusing to use insecure config {}; mode {:o} is broader than 0600",
            path.display(),
            mode
        );
    }
    Ok(())
}

pub fn canonicalize_server_url(server: &str) -> Result<String> {
    let mut url = Url::parse(server).with_context(|| format!("invalid server url: {server}"))?;
    if url.scheme().is_empty() || url.host_str().is_none() {
        bail!("server url must include scheme and host");
    }
    if let Some(host) = url.host_str() {
        let host_lc = host.to_ascii_lowercase();
        url.set_host(Some(&host_lc))
            .context("failed to normalize host")?;
    }
    url.set_fragment(None);
    url.set_query(None);
    let normalized = url.to_string();
    Ok(normalized.trim_end_matches('/').to_string())
}

pub fn derive_key_id(server: &str, username: &str) -> Result<String> {
    let canonical = canonicalize_server_url(server)?;
    let mut hasher = Sha256::new();
    hasher.update(canonical.to_ascii_lowercase().as_bytes());
    hasher.update(b"\n");
    hasher.update(username.as_bytes());
    Ok(format!("{CONFIG_NAMESPACE}/{:x}", hasher.finalize()))
}

pub fn store_password(key_id: &str, password: &str) -> Result<()> {
    Entry::new(CONFIG_SERVICE, key_id)
        .context("failed to create keyring entry")?
        .set_password(password)
        .context("failed to store password in keyring")
}

pub fn load_password(key_id: &str) -> Result<String> {
    Entry::new(CONFIG_SERVICE, key_id)
        .context("failed to create keyring entry")?
        .get_password()
        .context("failed to load password from keyring")
}

pub fn delete_password(key_id: &str) -> Result<()> {
    let entry = Entry::new(CONFIG_SERVICE, key_id).context("failed to create keyring entry")?;
    match entry.delete_credential() {
        Ok(()) => Ok(()),
        Err(err) => {
            let message = err.to_string();
            if message.contains("No such secret item") || message.contains("NoEntry") {
                Ok(())
            } else {
                Err(err).context("failed to delete password from keyring")
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use std::fs;
    use std::os::unix::fs::PermissionsExt;

    use tempfile::tempdir;

    use super::{canonicalize_server_url, validate_permissions};

    #[test]
    fn canonicalize_server_url_normalizes_host_and_trailing_slash() {
        let actual =
            canonicalize_server_url("HTTPS://DNS.Example.COM/api/").expect("canonicalized");
        assert_eq!(actual, "https://dns.example.com/api");
    }

    #[test]
    fn validate_permissions_rejects_group_readable_file() {
        let dir = tempdir().expect("tempdir");
        let path = dir.path().join("config.yaml");
        fs::write(&path, "server: http://localhost\n").expect("write");
        let mut perms = fs::metadata(&path).expect("meta").permissions();
        perms.set_mode(0o644);
        fs::set_permissions(&path, perms).expect("chmod");
        let err = validate_permissions(&path, false).expect_err("must reject");
        assert!(err.to_string().contains("refusing to use insecure config"));
    }
}
