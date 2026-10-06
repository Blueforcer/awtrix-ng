use futures_util::StreamExt;
use reqwest::{redirect::Policy, Client, StatusCode, Url};
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use std::{future::Future, path::Path, time::Duration};

pub use crate::constants::MAX_ARCHIVE_BYTES as MAX_FIRMWARE;
const MAX_METADATA: usize = 1024 * 1024;
const RELEASE_API: &str = "https://api.github.com/repos/Blueforcer/awtrix-ng/releases/latest";
const ASSET: &str = "usb-awtrix-ng-tc002.zip";
const UNAVAILABLE: &str = "No TC002 installation firmware is available in the latest published AWTRIX NG release. Please try again after a TC002 release is published.";

#[derive(Clone, Debug, Serialize)]
pub struct Progress {
    pub stage: &'static str,
    pub received: usize,
    pub total: Option<usize>,
}

#[derive(Deserialize)]
struct Release {
    tag_name: String,
    draft: bool,
    prerelease: bool,
    assets: Vec<Asset>,
}

#[derive(Deserialize)]
struct Asset {
    name: String,
    size: usize,
    state: String,
    browser_download_url: String,
    digest: Option<String>,
}

#[derive(Debug)]
struct Download {
    url: Url,
    size: usize,
    sha256: String,
}

fn allowed_redirect(url: &Url) -> bool {
    url.scheme() == "https"
        && url.username().is_empty()
        && url.password().is_none()
        && url.port().is_none()
        && url.fragment().is_none()
        && matches!(
            url.host_str(),
            Some(
                "github.com"
                    | "release-assets.githubusercontent.com"
                    | "objects.githubusercontent.com"
            )
        )
}

pub fn client() -> Result<Client, String> {
    Client::builder()
        .user_agent(concat!(
            "AWTRIX-NG-TC002-Installer/",
            env!("CARGO_PKG_VERSION")
        ))
        .connect_timeout(Duration::from_secs(15))
        .timeout(Duration::from_secs(180))
        .redirect(Policy::custom(|attempt| {
            if attempt.previous().len() >= 5 || !allowed_redirect(attempt.url()) {
                attempt.error("Untrusted download redirect")
            } else {
                attempt.follow()
            }
        }))
        .build()
        .map_err(|_| "Cannot initialize secure downloads.".into())
}

async fn bytes_limited(
    response: reqwest::Response,
    maximum: usize,
    mut progress: impl FnMut(usize),
) -> Result<Vec<u8>, String> {
    if response
        .content_length()
        .is_some_and(|length| length > maximum as u64)
    {
        return Err("The download exceeds its permitted size.".into());
    }
    let mut result = Vec::new();
    let mut stream = response.bytes_stream();
    while let Some(chunk) = stream.next().await {
        let chunk = chunk.map_err(|_| {
            "The download was interrupted. Check your internet connection and try again."
        })?;
        if chunk.len() > maximum.saturating_sub(result.len()) {
            return Err("The download exceeds its permitted size.".into());
        }
        result.extend_from_slice(&chunk);
        progress(result.len());
    }
    Ok(result)
}

fn select_asset(bytes: &[u8]) -> Result<Download, String> {
    let release: Release = serde_json::from_slice(bytes)
        .map_err(|_| "GitHub returned invalid release information.")?;
    if release.draft || release.prerelease {
        return Err(UNAVAILABLE.into());
    }
    let tag = &release.tag_name;
    if tag.is_empty()
        || tag.len() > 100
        || !tag
            .bytes()
            .all(|b| b.is_ascii_alphanumeric() || b"._+-".contains(&b))
    {
        return Err("The release has an unsupported version name.".into());
    }
    let mut matches = release
        .assets
        .into_iter()
        .filter(|asset| asset.name == ASSET);
    let asset = matches.next().ok_or(UNAVAILABLE)?;
    if matches.next().is_some()
        || asset.state != "uploaded"
        || asset.size == 0
        || asset.size > MAX_FIRMWARE
    {
        return Err("The published TC002 firmware has invalid metadata.".into());
    }
    let expected =
        format!("https://github.com/Blueforcer/awtrix-ng/releases/download/{tag}/{ASSET}");
    if asset.browser_download_url != expected {
        return Err("The firmware download does not belong to this release.".into());
    }
    let hash = asset
        .digest
        .as_deref()
        .and_then(|digest| digest.strip_prefix("sha256:"))
        .filter(|hash| {
            hash.len() == 64
                && hash
                    .bytes()
                    .all(|b| b.is_ascii_digit() || (b'a'..=b'f').contains(&b))
        })
        .ok_or("The published TC002 firmware has no valid SHA-256 checksum.")?;
    Ok(Download {
        url: Url::parse(&expected).map_err(|_| "Invalid firmware URL.")?,
        size: asset.size,
        sha256: hash.into(),
    })
}

fn verify(bytes: &[u8], expected: &Download) -> Result<(), String> {
    if bytes.len() != expected.size || format!("{:x}", Sha256::digest(bytes)) != expected.sha256 {
        return Err("Firmware verification failed. Please download it again.".into());
    }
    Ok(())
}

fn check_status(status: StatusCode, release: bool) -> Result<(), String> {
    if release && status == StatusCode::NOT_FOUND {
        return Err(UNAVAILABLE.into());
    }
    if status == StatusCode::FORBIDDEN || status == StatusCode::TOO_MANY_REQUESTS {
        return Err(
            "GitHub is temporarily limiting downloads from this network. Please try again later."
                .into(),
        );
    }
    if !status.is_success() {
        return Err(format!(
            "GitHub could not provide the download (HTTP {}). Please try again later.",
            status.as_u16()
        ));
    }
    Ok(())
}

pub async fn firmware(progress: impl Fn(Progress)) -> Result<Vec<u8>, String> {
    let directory = crate::sidecar::directory()?;
    firmware_at(&directory, &progress, || release_firmware(&progress)).await
}

fn local_progress(bytes: &[u8], progress: &impl Fn(Progress)) {
    progress(Progress {
        stage: "local",
        received: 0,
        total: Some(bytes.len()),
    });
    progress(Progress {
        stage: "local",
        received: bytes.len(),
        total: Some(bytes.len()),
    });
}

pub fn local_firmware(path: &Path, progress: impl Fn(Progress)) -> Result<Vec<u8>, String> {
    let bytes = crate::sidecar::read_path(path, MAX_FIRMWARE)
        .map_err(|error| format!("Local firmware: {error}"))?;
    local_progress(&bytes, &progress);
    Ok(bytes)
}

async fn firmware_at<F: Future<Output = Result<Vec<u8>, String>>>(
    directory: &Path,
    progress: &impl Fn(Progress),
    online: impl FnOnce() -> F,
) -> Result<Vec<u8>, String> {
    if let Some(bytes) = crate::sidecar::read(directory, ASSET, MAX_FIRMWARE)
        .map_err(|error| format!("Local firmware: {error}"))?
    {
        local_progress(&bytes, progress);
        return Ok(bytes);
    }
    online().await
}

pub async fn release_firmware(progress: impl Fn(Progress)) -> Result<Vec<u8>, String> {
    let client = client()?;
    progress(Progress {
        stage: "checking",
        received: 0,
        total: None,
    });
    let response = client
        .get(RELEASE_API)
        .header("Accept", "application/vnd.github+json")
        .header("X-GitHub-Api-Version", "2022-11-28")
        .timeout(Duration::from_secs(30))
        .send()
        .await
        .map_err(|_| "Cannot reach GitHub. Check your internet connection and try again.")?;
    check_status(response.status(), true)?;
    let asset = select_asset(&bytes_limited(response, MAX_METADATA, |_| {}).await?)?;
    progress(Progress {
        stage: "downloading",
        received: 0,
        total: Some(asset.size),
    });
    let response = client.get(asset.url.clone()).send().await.map_err(|_| {
        "Cannot download the firmware. Check your internet connection and try again."
    })?;
    check_status(response.status(), false)?;
    let bytes = bytes_limited(response, asset.size, |received| {
        progress(Progress {
            stage: "downloading",
            received,
            total: Some(asset.size),
        })
    })
    .await?;
    verify(&bytes, &asset)?;
    progress(Progress {
        stage: "verified",
        received: bytes.len(),
        total: Some(bytes.len()),
    });
    Ok(bytes)
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::{json, Value};

    #[tokio::test]
    async fn local_firmware_precedes_network_and_reports_its_source() {
        let directory = tempfile::tempdir().unwrap();
        let bytes = b"local package passed to the shared ZIP verifier";
        std::fs::write(directory.path().join(ASSET), bytes).unwrap();
        let events = std::sync::Mutex::new(Vec::new());
        let result = firmware_at(
            directory.path(),
            &|progress| events.lock().unwrap().push(progress),
            || async { panic!("Local firmware must never request GitHub") },
        )
        .await
        .unwrap();
        assert_eq!(result, bytes);
        let events = events.lock().unwrap();
        assert_eq!(events.len(), 2);
        assert!(events.iter().all(|event| event.stage == "local"));
        assert_eq!(events.last().unwrap().received, bytes.len());
    }

    #[tokio::test]
    async fn only_absent_sidecar_uses_online_firmware() {
        let directory = tempfile::tempdir().unwrap();
        let online = || async { Ok(b"online firmware".to_vec()) };
        assert_eq!(
            firmware_at(directory.path(), &|_| {}, online)
                .await
                .unwrap(),
            b"online firmware"
        );
        std::fs::write(directory.path().join(ASSET), b"").unwrap();
        let error = firmware_at(directory.path(), &|_| {}, || async {
            panic!("An invalid local file must not be replaced silently")
        })
        .await
        .unwrap_err();
        assert!(error.starts_with("Local firmware:"));
    }

    fn release() -> Value {
        json!({"tag_name":"v1.2.0", "draft":false, "prerelease":false, "assets":[{
            "name":ASSET, "state":"uploaded", "size":3,
            "browser_download_url":format!("https://github.com/Blueforcer/awtrix-ng/releases/download/v1.2.0/{ASSET}"),
            "digest":format!("sha256:{:x}",Sha256::digest(b"zip"))
        }]})
    }
    fn select(value: Value) -> Result<Download, String> {
        select_asset(&serde_json::to_vec(&value).unwrap())
    }
    #[test]
    fn verifies_exact_latest_asset() {
        let asset = select(release()).unwrap();
        assert!(verify(b"zip", &asset).is_ok());
        assert!(verify(b"ZIP", &asset).is_err());
        assert!(verify(b"zi", &asset).is_err());
    }
    #[test]
    fn refuses_missing_incomplete_and_ambiguous_assets() {
        for assets in [
            json!([]),
            json!([release()["assets"][0], release()["assets"][0]]),
        ] {
            let mut value = release();
            value["assets"] = assets;
            assert!(select(value).is_err());
        }
        for (field, invalid) in [
            ("size", json!(0)),
            ("size", json!(MAX_FIRMWARE + 1)),
            ("state", json!("new")),
            ("digest", Value::Null),
            ("digest", json!("sha256:bad")),
            (
                "browser_download_url",
                json!("https://example.com/firmware.zip"),
            ),
        ] {
            let mut value = release();
            value["assets"][0][field] = invalid;
            assert!(select(value).is_err(), "{field}");
        }
    }
    #[test]
    fn refuses_prerelease_draft_and_unsafe_tag() {
        for field in ["draft", "prerelease"] {
            let mut v = release();
            v[field] = json!(true);
            assert!(select(v).is_err());
        }
        let mut v = release();
        v["tag_name"] = json!("../latest");
        assert!(select(v).is_err());
    }
    #[test]
    fn bounds_redirects_to_secure_github_asset_hosts() {
        for url in [
            "https://release-assets.githubusercontent.com/a?token=abc",
            "https://github.com/a",
            "https://objects.githubusercontent.com/a",
        ] {
            assert!(allowed_redirect(&Url::parse(url).unwrap()));
        }
        for url in [
            "http://github.com/a",
            "https://github.com.evil.example/a",
            "https://user@github.com/a",
            "https://127.0.0.1/a",
            "https://github.com:444/a",
        ] {
            assert!(!allowed_redirect(&Url::parse(url).unwrap()));
        }
    }
}
