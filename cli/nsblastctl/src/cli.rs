use std::path::PathBuf;

use clap::{ArgAction, Parser, Subcommand, ValueEnum};
use serde::{Deserialize, Serialize};

#[derive(Clone, Debug, Deserialize, Serialize, ValueEnum)]
pub enum OutputFormat {
    Table,
    Json,
    Yaml,
}

#[derive(Clone, Debug, ValueEnum)]
pub enum ConflictMode {
    Fail,
    Overwrite,
    Skip,
}

#[derive(Debug, Parser)]
#[command(name = "nsblastctl")]
#[command(about = "nsblast operator CLI")]
pub struct Cli {
    #[arg(long)]
    pub server: Option<String>,
    #[arg(long)]
    pub username: Option<String>,
    #[arg(long)]
    pub password: Option<String>,
    #[arg(long)]
    pub password_stdin: bool,
    #[arg(long)]
    pub save_password: bool,
    #[arg(long)]
    pub no_keyring: bool,
    #[arg(long)]
    pub allow_insecure_config: bool,
    #[arg(long)]
    pub profile: Option<String>,
    #[arg(long, value_enum, default_value = "table")]
    pub output: OutputFormat,
    #[arg(long, default_value_t = 30)]
    pub timeout: u64,
    #[arg(long, action = ArgAction::Count)]
    pub verbose: u8,
    #[arg(long)]
    pub quiet: bool,
    #[arg(long)]
    pub yes: bool,
    #[arg(long)]
    pub dry_run: bool,
    #[command(subcommand)]
    pub command: Command,
}

#[derive(Clone, Debug, Subcommand)]
pub enum Command {
    Zone {
        #[command(subcommand)]
        command: ZoneCommand,
    },
    Rr {
        #[command(subcommand)]
        command: RrCommand,
    },
    Tenant {
        #[command(subcommand)]
        command: TenantCommand,
    },
    Import {
        #[command(subcommand)]
        command: ImportCommand,
    },
    Export {
        #[command(subcommand)]
        command: ExportCommand,
    },
    Auth {
        #[command(subcommand)]
        command: AuthCommand,
    },
    Health,
}

#[derive(Clone, Debug, Subcommand)]
pub enum ZoneCommand {
    List {
        #[arg(long)]
        tenant: Option<String>,
        #[arg(long)]
        filter: Option<String>,
    },
    Get {
        zone: String,
        #[arg(long)]
        tenant: Option<String>,
    },
    Create {
        zone: String,
        #[arg(long)]
        tenant: String,
        #[arg(long)]
        primary_ns: Option<String>,
        #[arg(long)]
        admin: Option<String>,
    },
    Delete {
        zone: String,
    },
    Export {
        zone: String,
        #[arg(long)]
        file: PathBuf,
        #[arg(long)]
        tenant: Option<String>,
    },
    Import {
        #[arg(long)]
        file: PathBuf,
        #[arg(long)]
        upsert: bool,
        #[arg(long, value_enum, default_value = "fail")]
        conflict: ConflictMode,
    },
}

#[derive(Clone, Debug, Subcommand)]
pub enum RrCommand {
    List {
        zone: String,
        #[arg(long)]
        name: Option<String>,
        #[arg(long = "type")]
        rr_type: Option<String>,
    },
    Add {
        zone: String,
        #[arg(long)]
        name: String,
        #[arg(long = "type")]
        rr_type: String,
        #[arg(long)]
        ttl: u32,
        #[arg(long)]
        value: String,
        #[arg(long)]
        priority: Option<u16>,
        #[arg(long)]
        weight: Option<u16>,
        #[arg(long)]
        port: Option<u16>,
    },
    Update {
        zone: String,
        #[arg(long)]
        id: String,
        #[arg(long)]
        name: Option<String>,
        #[arg(long = "type")]
        rr_type: Option<String>,
        #[arg(long)]
        ttl: Option<u32>,
        #[arg(long)]
        value: Option<String>,
        #[arg(long)]
        priority: Option<u16>,
        #[arg(long)]
        weight: Option<u16>,
        #[arg(long)]
        port: Option<u16>,
    },
    Delete {
        zone: String,
        #[arg(long)]
        id: String,
    },
    BulkAdd {
        #[arg(long)]
        csv: PathBuf,
        #[arg(long)]
        upsert: bool,
        #[arg(long)]
        strict: bool,
    },
    BulkDelete {
        #[arg(long)]
        csv: PathBuf,
    },
}

#[derive(Clone, Debug, Subcommand)]
pub enum TenantCommand {
    List,
    Get {
        tenant: String,
    },
    Create {
        tenant: String,
        #[arg(long)]
        display_name: Option<String>,
    },
    Delete {
        tenant: String,
    },
    Export {
        tenant: String,
        #[arg(long)]
        file: PathBuf,
    },
    Import {
        #[arg(long)]
        file: PathBuf,
        #[arg(long)]
        upsert: bool,
        #[arg(long, value_enum, default_value = "fail")]
        conflict: ConflictMode,
    },
}

#[derive(Clone, Debug, Subcommand)]
pub enum ImportCommand {
    Zone {
        #[arg(long)]
        file: PathBuf,
        #[arg(long)]
        upsert: bool,
        #[arg(long, value_enum, default_value = "fail")]
        conflict: ConflictMode,
    },
    Tenant {
        #[arg(long)]
        file: PathBuf,
        #[arg(long)]
        upsert: bool,
        #[arg(long, value_enum, default_value = "fail")]
        conflict: ConflictMode,
    },
}

#[derive(Clone, Debug, Subcommand)]
pub enum ExportCommand {
    Zone {
        zone: String,
        #[arg(long)]
        file: PathBuf,
        #[arg(long)]
        tenant: Option<String>,
    },
    Tenant {
        tenant: String,
        #[arg(long)]
        file: PathBuf,
    },
}

#[derive(Clone, Debug, Subcommand)]
pub enum AuthCommand {
    Login {
        #[arg(long)]
        server: Option<String>,
        #[arg(long)]
        username: Option<String>,
        #[arg(long)]
        password_stdin: bool,
    },
    SetPassword {
        #[arg(long)]
        server: Option<String>,
        #[arg(long)]
        username: Option<String>,
        #[arg(long)]
        password_stdin: bool,
    },
    Logout {
        #[arg(long)]
        server: Option<String>,
        #[arg(long)]
        username: Option<String>,
    },
    Whoami {
        #[arg(long)]
        server: Option<String>,
    },
}
