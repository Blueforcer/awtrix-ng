use futures_util::StreamExt;
use sha2::{Digest, Sha256};
use std::{
    fs,
    io::{Cursor, Read, Write},
    path::{Path, PathBuf},
    time::Duration,
};
use tokio::process::Command;

const VERSION: &str = "v24.21.0";
const MAX_ARCHIVE: usize = 100 * 1024 * 1024;
const MAX_BINARY: u64 = 200 * 1024 * 1024;

fn distribution(os: &str, arch: &str) -> Result<(&'static str, &'static str), String> {
    match (os, arch) {
        ("windows", "x86_64") => Ok((
            "win-x64.zip",
            "158f7685b44de51f6c0df1d153526cbcd3e1bc739a8dfc607721cef75de9e541",
        )),
        ("windows", "aarch64") => Ok((
            "win-arm64.zip",
            "8779b1bde1d39f8d420e3b57aa657b39891af434d3de44a919044cec06785921",
        )),
        ("linux", "x86_64") => Ok((
            "linux-x64.tar.gz",
            "6e1db87ef58b8819e5d5402eff1536491b18edd8eb7bee5ef7897876e88dc5ff",
        )),
        ("linux", "aarch64") => Ok((
            "linux-arm64.tar.gz",
            "724282c3b43aec998aa9527380465b45d229e021b58035f5f4f63095eabfe5d5",
        )),
        ("macos", "x86_64") => Ok((
            "darwin-x64.tar.gz",
            "1462cb3b3046b815cf8ea436d3da450ec1a9f11dac7e5a46b0ada5305d7e8097",
        )),
        ("macos", "aarch64") => Ok((
            "darwin-arm64.tar.gz",
            "bed7eea5325e1108f32ce5228ddd6a5f0f08a499ee42aa7442aea583702f6057",
        )),
        _ => Err(
            "This terminal platform is not supported. Use Windows, macOS or Linux on x64 or ARM64."
                .into(),
        ),
    }
}

fn valid_version(value: &str) -> bool {
    value
        .trim()
        .strip_prefix('v')
        .and_then(|v| v.split('.').next())
        .and_then(|major| major.parse::<u32>().ok())
        .is_some_and(|major| major >= 24)
}

async fn existing() -> Option<PathBuf> {
    let mut command = Command::new("node");
    command
        .args(["--version"])
        .env_remove("NODE_OPTIONS")
        .env_remove("NODE_PATH")
        .kill_on_drop(true);
    let result = tokio::time::timeout(Duration::from_secs(5), command.output())
        .await
        .ok()?
        .ok()?;
    (result.status.success() && valid_version(&String::from_utf8_lossy(&result.stdout)))
        .then(|| PathBuf::from("node"))
}

fn verify(bytes: &[u8], hash: &str) -> Result<(), String> {
    if bytes.is_empty()
        || bytes.len() > MAX_ARCHIVE
        || format!("{:x}", Sha256::digest(bytes)) != hash
    {
        return Err("The terminal runtime failed SHA-256 verification.".into());
    }
    Ok(())
}

fn sidecar_archive(directory: &Path, suffix: &str, hash: &str) -> Result<Option<Vec<u8>>, String> {
    let name = format!("node-{VERSION}-{suffix}");
    let Some(bytes) = crate::sidecar::read(directory, &name, MAX_ARCHIVE)? else {
        return Ok(None);
    };
    verify(&bytes, hash).map_err(|_| {
        format!("The offline terminal runtime {name} failed SHA-256 verification. Replace it with the official Node.js {VERSION} archive.")
    })?;
    Ok(Some(bytes))
}

fn write_member(mut input: impl Read, path: &Path, maximum: u64) -> Result<(), String> {
    let mut out = fs::OpenOptions::new()
        .create_new(true)
        .write(true)
        .open(path)
        .map_err(|_| "Cannot create terminal runtime file.")?;
    let mut bytes = Vec::new();
    input
        .by_ref()
        .take(maximum + 1)
        .read_to_end(&mut bytes)
        .map_err(|_| "Cannot read terminal runtime archive.")?;
    if bytes.is_empty() || bytes.len() as u64 > maximum {
        return Err("Invalid terminal runtime file size.".into());
    }
    out.write_all(&bytes)
        .map_err(|_| "Cannot write terminal runtime file.")?;
    Ok(())
}

fn extract(bytes: &[u8], suffix: &str, directory: &Path) -> Result<PathBuf, String> {
    let root = format!(
        "node-{VERSION}-{}",
        suffix.split('.').next().ok_or("Invalid runtime name")?
    );
    let windows = suffix.ends_with(".zip");
    let binary_name = if windows { "node.exe" } else { "node" };
    let binary_member = format!("{root}/{}", if windows { "node.exe" } else { "bin/node" });
    let license_member = format!("{root}/LICENSE");
    let executable = directory.join(binary_name);
    if windows {
        let mut archive =
            zip::ZipArchive::new(Cursor::new(bytes)).map_err(|_| "Invalid runtime ZIP archive.")?;
        for (name, path, maximum) in [
            (&binary_member, executable.clone(), MAX_BINARY),
            (
                &license_member,
                directory.join("NODE-LICENSE"),
                2 * 1024 * 1024,
            ),
        ] {
            let entry = archive
                .by_name(name)
                .map_err(|_| "The runtime archive is incomplete.")?;
            if !entry.is_file() || entry.is_symlink() {
                return Err("Invalid runtime archive entry.".into());
            }
            write_member(entry, &path, maximum)?;
        }
    } else {
        let decoder = flate2::read::GzDecoder::new(bytes);
        let mut archive = tar::Archive::new(decoder.take(512 * 1024 * 1024));
        for entry in archive
            .entries()
            .map_err(|_| "Invalid runtime tar archive.")?
        {
            let entry = entry.map_err(|_| "Invalid runtime archive entry.")?;
            let name = entry.path().map_err(|_| "Invalid runtime path.")?;
            let selection = if name == Path::new(&binary_member) {
                Some((executable.clone(), MAX_BINARY))
            } else if name == Path::new(&license_member) {
                Some((directory.join("NODE-LICENSE"), 2 * 1024 * 1024))
            } else {
                None
            };
            if let Some((path, maximum)) = selection {
                if !entry.header().entry_type().is_file() {
                    return Err("Invalid runtime archive entry type.".into());
                }
                write_member(entry, &path, maximum)?;
            }
        }
    }
    if !executable.is_file() || !directory.join("NODE-LICENSE").is_file() {
        return Err("The runtime archive is incomplete.".into());
    }
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        fs::set_permissions(&executable, fs::Permissions::from_mode(0o700))
            .map_err(|_| "Cannot make terminal runtime executable.")?;
    }
    Ok(executable)
}

pub async fn node(directory: &Path) -> Result<PathBuf, String> {
    if let Some(node) = existing().await {
        return Ok(node);
    }
    let (suffix, hash) = distribution(std::env::consts::OS, std::env::consts::ARCH)?;
    let name = format!("node-{VERSION}-{suffix}");
    let cache = dirs::cache_dir().map(|dir| dir.join("awtrix-ng/installer/runtime").join(&name));
    let mut bytes = sidecar_archive(&crate::sidecar::directory()?, suffix, hash)?;
    if bytes.is_none() {
        bytes = cache
            .as_ref()
            .and_then(|path| {
                fs::metadata(path)
                    .ok()
                    .filter(|m| m.len() <= MAX_ARCHIVE as u64)
                    .and_then(|_| fs::read(path).ok())
            })
            .filter(|bytes| verify(bytes, hash).is_ok());
    }
    if bytes.is_none() {
        eprintln!("Downloading the terminal runtime from nodejs.org (first use only)…");
        let client = reqwest::Client::builder()
            .https_only(true)
            .redirect(reqwest::redirect::Policy::none())
            .connect_timeout(Duration::from_secs(15))
            .timeout(Duration::from_secs(300))
            .build()
            .map_err(|_| "Cannot initialize the runtime download.")?;
        let response = client.get(format!("https://nodejs.org/dist/{VERSION}/{name}")).send().await
            .map_err(|_| "Cannot download the terminal runtime. Check your internet connection, or install Node.js 24 LTS.")?;
        if !response.status().is_success()
            || response
                .content_length()
                .is_some_and(|n| n > MAX_ARCHIVE as u64)
        {
            return Err("The terminal runtime download is unavailable or too large.".into());
        }
        let mut stream = response.bytes_stream();
        let mut data = Vec::new();
        let mut reported = 0;
        while let Some(chunk) = stream.next().await {
            let chunk = chunk.map_err(|_| "The terminal runtime download was interrupted.")?;
            if chunk.len() > MAX_ARCHIVE.saturating_sub(data.len()) {
                return Err("The terminal runtime download is too large.".into());
            }
            data.extend_from_slice(&chunk);
            if data.len() / (5 * 1024 * 1024) > reported {
                reported = data.len() / (5 * 1024 * 1024);
                eprintln!("  {} MB received", data.len() / (1024 * 1024));
            }
        }
        verify(&data, hash)?;
        if let Some(cache) = &cache {
            if let Some(parent) = cache.parent() {
                if fs::create_dir_all(parent).is_ok() {
                    if let Ok(mut file) = tempfile::NamedTempFile::new_in(parent) {
                        if file.write_all(&data).is_ok() {
                            let _ = file.persist(cache);
                        }
                    }
                }
            }
        }
        bytes = Some(data);
    }
    extract(&bytes.unwrap(), suffix, directory)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn verified_sidecar_archive_is_available_without_network() {
        let sidecar = tempfile::tempdir().unwrap();
        let mut archive = zip::ZipWriter::new(Cursor::new(Vec::new()));
        for (name, bytes) in [("node.exe", b"binary".as_slice()), ("LICENSE", b"license")] {
            archive
                .start_file(
                    format!("node-{VERSION}-win-x64/{name}"),
                    zip::write::SimpleFileOptions::default(),
                )
                .unwrap();
            archive.write_all(bytes).unwrap();
        }
        let bytes = archive.finish().unwrap().into_inner();
        fs::write(
            sidecar.path().join(format!("node-{VERSION}-win-x64.zip")),
            &bytes,
        )
        .unwrap();
        let hash = format!("{:x}", Sha256::digest(&bytes));
        let verified = sidecar_archive(sidecar.path(), "win-x64.zip", &hash)
            .unwrap()
            .unwrap();
        let extracted = tempfile::tempdir().unwrap();
        let executable = extract(&verified, "win-x64.zip", extracted.path()).unwrap();
        assert_eq!(fs::read(executable).unwrap(), b"binary");
        assert_eq!(
            fs::read(extracted.path().join("NODE-LICENSE")).unwrap(),
            b"license"
        );
    }

    #[test]
    fn missing_sidecar_ignores_other_runtime_versions_and_platforms() {
        let directory = tempfile::tempdir().unwrap();
        for name in ["node-v22.0.0-win-x64.zip", "node-v24.21.0-win-arm64.zip"] {
            fs::write(directory.path().join(name), b"unrelated runtime").unwrap();
        }
        let (suffix, hash) = distribution("windows", "x86_64").unwrap();
        assert_eq!(
            sidecar_archive(directory.path(), suffix, hash).unwrap(),
            None
        );
    }

    #[test]
    fn corrupt_sidecar_is_an_error_instead_of_a_download_fallback() {
        let directory = tempfile::tempdir().unwrap();
        let (suffix, hash) = distribution("windows", "x86_64").unwrap();
        fs::write(
            directory.path().join(format!("node-{VERSION}-{suffix}")),
            b"corrupted archive",
        )
        .unwrap();
        let error = sidecar_archive(directory.path(), suffix, hash).unwrap_err();
        assert!(error.contains("node-v24.21.0-win-x64.zip"));
        assert!(error.contains("SHA-256 verification"));
    }

    #[test]
    fn unusable_sidecar_is_an_error_instead_of_a_download_fallback() {
        let directory = tempfile::tempdir().unwrap();
        let (suffix, hash) = distribution("windows", "x86_64").unwrap();
        let path = directory.path().join(format!("node-{VERSION}-{suffix}"));
        fs::write(&path, b"").unwrap();
        assert!(sidecar_archive(directory.path(), suffix, hash).is_err());
        fs::remove_file(&path).unwrap();
        fs::create_dir(path).unwrap();
        assert!(sidecar_archive(directory.path(), suffix, hash).is_err());
    }
    #[test]
    fn all_supported_targets_have_pinned_hashes() {
        for os in ["windows", "linux", "macos"] {
            for arch in ["x86_64", "aarch64"] {
                let (_, hash) = distribution(os, arch).unwrap();
                assert_eq!(hash.len(), 64);
            }
        }
        assert!(distribution("linux", "arm").is_err());
        assert!(!valid_version("v22.0.0"));
        assert!(valid_version("v24.21.0\n"));
    }
    #[test]
    fn runtime_checksum_detects_corruption() {
        assert!(verify(b"archive", &format!("{:x}", Sha256::digest(b"archive"))).is_ok());
        assert!(verify(b"corrupted", &format!("{:x}", Sha256::digest(b"archive"))).is_err());
    }
    #[test]
    fn extract_ignores_unselected_paths_and_refuses_missing_binary() {
        let mut encoded = Vec::new();
        {
            let gzip = flate2::write::GzEncoder::new(&mut encoded, flate2::Compression::fast());
            let mut archive = tar::Builder::new(gzip);
            let prefix = format!("node-{VERSION}-linux-x64");
            for (path, contents) in [
                (format!("{prefix}/bin/node"), b"binary".as_slice()),
                (format!("{prefix}/LICENSE"), b"license"),
                ("unselected".into(), b"ignored"),
            ] {
                let mut header = tar::Header::new_gnu();
                header.set_size(contents.len() as u64);
                header.set_mode(0o644);
                header.set_cksum();
                archive.append_data(&mut header, path, contents).unwrap();
            }
            archive.into_inner().unwrap().finish().unwrap();
        }
        let directory = tempfile::tempdir().unwrap();
        let node = extract(&encoded, "linux-x64.tar.gz", directory.path()).unwrap();
        assert_eq!(fs::read(node).unwrap(), b"binary");
        assert!(!directory.path().join("unselected").exists());
        let directory = tempfile::tempdir().unwrap();
        assert!(extract(&encoded, "darwin-x64.tar.gz", directory.path()).is_err());
    }
}
