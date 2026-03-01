mod app;
mod cli;
mod config;
mod output;
mod records;

use app::run;
use clap::Parser;
use cli::Cli;

#[tokio::main]
async fn main() {
    let cli = Cli::parse();
    std::process::exit(run(cli).await);
}
