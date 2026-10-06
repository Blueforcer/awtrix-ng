fn main() {
    println!("cargo:rerun-if-env-changed=AWTRIX_INSTALLER_ASSETS");
    let assets = std::env::var("AWTRIX_INSTALLER_ASSETS").unwrap_or_else(|_| {
        std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
            .join("../dist")
            .to_string_lossy()
            .into_owned()
    });
    println!("cargo:rustc-env=AWTRIX_INSTALLER_ASSETS={assets}");
    println!("cargo:rerun-if-changed={assets}");
    #[cfg(feature = "gui")]
    tauri_build::build();
}
