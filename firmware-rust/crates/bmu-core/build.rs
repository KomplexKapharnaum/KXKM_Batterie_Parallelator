fn main() {
    let Ok(crate_dir) = std::env::var("CARGO_MANIFEST_DIR") else {
        println!("cargo:warning=cbindgen: CARGO_MANIFEST_DIR not set");
        return;
    };
    let config_path = format!("{crate_dir}/cbindgen.toml");
    let out_header = std::path::PathBuf::from(&crate_dir)
        .parent()
        .and_then(std::path::Path::parent)
        .map(|p| p.join("target").join("include").join("bmu_core.h"));

    let Some(out_header) = out_header else {
        println!("cargo:warning=cbindgen: could not derive output header path");
        return;
    };

    if let Some(parent) = out_header.parent() {
        let _ = std::fs::create_dir_all(parent);
    }

    let config = cbindgen::Config::from_file(&config_path).unwrap_or_default();
    match cbindgen::Builder::new()
        .with_crate(&crate_dir)
        .with_config(config)
        .generate()
    {
        Ok(bindings) => {
            bindings.write_to_file(&out_header);
            println!("cargo:rerun-if-changed=src");
            println!("cargo:rerun-if-changed={config_path}");
            println!("cargo:warning=Generated {}", out_header.display());
        }
        Err(e) => {
            println!("cargo:warning=cbindgen generation failed: {e}");
        }
    }
}
