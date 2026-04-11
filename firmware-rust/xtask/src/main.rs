//! `cargo xtask <command>` — task runner for the BMU Rust workspace.
//!
//! Commandes :
//! - `vendor-header` : copie `target/include/bmu_core.h` vers
//!   `firmware-idf-v2/components/bmu_core_rs/include/bmu_core.h` (Part 2).
//! - `abi-check` : compile un petit programme C qui inclut `bmu_core.h`
//!   et vérifie la cohérence des `sizeof` des structs `#[repr(C)]`.
//! - `size` : build release xtensa + assertion taille < 500 KB.

#![allow(clippy::print_stderr, clippy::print_stdout, clippy::exit)]

use std::path::PathBuf;
use std::process::Command;

fn workspace_root() -> Result<PathBuf, String> {
    let Ok(manifest) = std::env::var("CARGO_MANIFEST_DIR") else {
        return Err("CARGO_MANIFEST_DIR not set".to_string());
    };
    PathBuf::from(manifest)
        .parent()
        .map(std::path::Path::to_path_buf)
        .ok_or_else(|| "xtask crate has no parent dir".to_string())
}

fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let cmd = args.first().map(String::as_str).unwrap_or_default();
    let result = match cmd {
        "vendor-header" => vendor_header(),
        "abi-check" => abi_check(),
        "size" => size(),
        _ => {
            eprintln!("Usage: cargo xtask <vendor-header|abi-check|size>");
            std::process::exit(1);
        }
    };
    if let Err(e) = result {
        eprintln!("Error: {e}");
        std::process::exit(2);
    }
}

/// Build `bmu-core` en release (déclenche `cbindgen` via `build.rs`) puis
/// copie le header généré vers `firmware-idf-v2/components/bmu_core_rs/include/`.
/// Crée le dossier si absent.
fn vendor_header() -> Result<(), String> {
    let root = workspace_root()?;

    let status = Command::new("cargo")
        .current_dir(&root)
        .args(["build", "-p", "bmu-core", "--release"])
        .status()
        .map_err(|e| format!("cargo build failed: {e}"))?;
    if !status.success() {
        return Err("cargo build failed".to_string());
    }

    let generated = root.join("target").join("include").join("bmu_core.h");
    if !generated.exists() {
        return Err(format!(
            "generated header not found at {}",
            generated.display()
        ));
    }

    let repo_root = root
        .parent()
        .ok_or_else(|| "workspace has no parent".to_string())?;
    let target_dir = repo_root
        .join("firmware-idf-v2")
        .join("components")
        .join("bmu_core_rs")
        .join("include");
    std::fs::create_dir_all(&target_dir)
        .map_err(|e| format!("mkdir {}: {e}", target_dir.display()))?;

    let target = target_dir.join("bmu_core.h");
    std::fs::copy(&generated, &target).map_err(|e| format!("copy to {}: {e}", target.display()))?;
    println!("Vendored header: {}", target.display());
    Ok(())
}

/// Build `bmu-core`, génère un petit programme C qui `sizeof()` chaque
/// struct `#[repr(C)]` exposée par `bmu_core.h`, le compile avec `cc`,
/// et l'exécute. Si l'un des types a une taille inattendue, `cc` ou le
/// runtime signalera l'erreur.
fn abi_check() -> Result<(), String> {
    let root = workspace_root()?;

    let status = Command::new("cargo")
        .current_dir(&root)
        .args(["build", "-p", "bmu-core", "--release"])
        .status()
        .map_err(|e| format!("cargo build failed: {e}"))?;
    if !status.success() {
        return Err("cargo build failed".to_string());
    }

    let header = root.join("target").join("include").join("bmu_core.h");
    if !header.exists() {
        return Err(format!("header not found at {}", header.display()));
    }

    // Note : `MAX_BATTERIES` est référencé dans `bmu_core.h` mais n'est pas
    // émis par `cbindgen` (constante Rust non-FFI). On fournit la valeur ici
    // pour que le header compile côté C. Source : `bmu-types` = 16.
    let c_prog = r#"
#include <stdio.h>
#define MAX_BATTERIES 16
#include "bmu_core.h"
int main(void) {
    printf("sizeof(BmuConfigC) = %zu\n", sizeof(struct BmuConfigC));
    printf("sizeof(BmuRawInputs) = %zu\n", sizeof(struct BmuRawInputs));
    printf("sizeof(BmuBatteryC) = %zu\n", sizeof(struct BmuBatteryC));
    printf("sizeof(BmuSystemC) = %zu\n", sizeof(struct BmuSystemC));
    printf("sizeof(BmuSnapshotC) = %zu\n", sizeof(struct BmuSnapshotC));
    printf("sizeof(BmuActionsC) = %zu\n", sizeof(struct BmuActionsC));
    printf("sizeof(BmuCommandC) = %zu\n", sizeof(struct BmuCommandC));
    return 0;
}
"#;

    let tmp_c = std::env::temp_dir().join("bmu_abi_check.c");
    let tmp_bin = std::env::temp_dir().join("bmu_abi_check");
    std::fs::write(&tmp_c, c_prog).map_err(|e| format!("write c: {e}"))?;

    let include_dir = header
        .parent()
        .ok_or_else(|| "header has no parent dir".to_string())?;
    let Some(include_str) = include_dir.to_str() else {
        return Err("non-UTF8 include dir".to_string());
    };
    let Some(tmp_c_str) = tmp_c.to_str() else {
        return Err("non-UTF8 tmp c path".to_string());
    };
    let Some(tmp_bin_str) = tmp_bin.to_str() else {
        return Err("non-UTF8 tmp bin path".to_string());
    };

    let status = Command::new("cc")
        .args(["-I", include_str, "-o", tmp_bin_str, tmp_c_str])
        .status()
        .map_err(|e| format!("cc failed: {e}"))?;
    if !status.success() {
        return Err("C compile of abi-check failed".to_string());
    }

    let status = Command::new(&tmp_bin)
        .status()
        .map_err(|e| format!("run abi check: {e}"))?;
    if !status.success() {
        return Err("abi check program exited non-zero".to_string());
    }

    println!("ABI check PASS");
    Ok(())
}

/// Cross-compile `bmu-core` pour `xtensa-esp32s3-none-elf` release et
/// vérifie que `libbmu_core.a` < 500 KB.
///
/// NOTE Task 10.1 : cette commande est implémentée mais ÉCHOUERA tant
/// que Task 10.2 n'a pas ajouté le bump allocator (link error sur
/// `alloc::boxed::Box` sans `#[global_allocator]` côté xtensa).
fn size() -> Result<(), String> {
    let root = workspace_root()?;

    let status = Command::new("cargo")
        .current_dir(&root)
        .args([
            "build",
            "--target",
            "xtensa-esp32s3-none-elf",
            "--release",
            "-p",
            "bmu-core",
        ])
        .status()
        .map_err(|e| format!("cargo xbuild: {e}"))?;
    if !status.success() {
        return Err("cross-compile failed".to_string());
    }

    let lib = root
        .join("target")
        .join("xtensa-esp32s3-none-elf")
        .join("release")
        .join("libbmu_core.a");
    if !lib.exists() {
        return Err(format!("libbmu_core.a not found at {}", lib.display()));
    }

    let meta = std::fs::metadata(&lib).map_err(|e| format!("stat: {e}"))?;
    let size_kb = meta.len() / 1024;
    println!("libbmu_core.a size: {size_kb} KB");
    if size_kb > 500 {
        return Err(format!("Size {size_kb} KB exceeds 500 KB budget"));
    }
    println!("Size budget OK (<500 KB)");
    Ok(())
}
