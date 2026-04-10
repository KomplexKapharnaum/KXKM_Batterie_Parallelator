//! cargo xtask <command> — task runner for the BMU Rust workspace.

fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();
    match args.first().map(String::as_str) {
        Some("vendor-header") => {
            eprintln!("vendor-header: not yet implemented");
            std::process::exit(2);
        }
        Some("abi-check") => {
            eprintln!("abi-check: not yet implemented");
            std::process::exit(2);
        }
        Some("size") => {
            eprintln!("size: not yet implemented");
            std::process::exit(2);
        }
        _ => {
            eprintln!("Usage: cargo xtask <vendor-header|abi-check|size>");
            std::process::exit(1);
        }
    }
}
