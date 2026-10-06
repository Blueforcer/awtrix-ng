use crate::service::{Error, Reply};
use serde_json::{json, Value};

fn fixture(variable: &str, limit: u64) -> Result<Vec<u8>, Error> {
    let path = std::env::var_os(variable)
        .ok_or_else(|| Error::new("fixture", format!("Missing {variable}")))?;
    let metadata = std::fs::metadata(&path)
        .map_err(|_| Error::new("fixture", "Missing qualification fixture."))?;
    if !metadata.is_file() || metadata.len() > limit {
        return Err(Error::new("fixture", "Invalid qualification fixture."));
    }
    std::fs::read(path).map_err(|_| Error::new("fixture", "Cannot read qualification fixture."))
}

pub fn intercept(command: &str) -> Option<Result<Reply, Error>> {
    if command == "firmware" && std::env::var("AWTRIX_QUALIFICATION_SIDECAR").as_deref() != Ok("1")
    {
        return Some(fixture("AWTRIX_QUALIFICATION_FIRMWARE", crate::download::MAX_FIRMWARE as u64).map(Reply::Bytes));
    }
    if command == "usb_list" {
        return Some(Ok(Reply::Json(json!([]))));
    }
    if command.starts_with("usb_") {
        return Some(Err(Error::new(
            "qualification",
            "Hardware is disabled in the qualification build.",
        )));
    }
    None
}

pub fn command(action: &str, report: Option<Value>) -> Result<Reply, Error> {
    match action {
        "stock" => fixture("AWTRIX_QUALIFICATION_STOCK", 8 * 1024 * 1024).map(Reply::Bytes),
        "report" => {
            let report = report.ok_or_else(|| Error::new("fixture", "Missing report."))?;
            let bytes = serde_json::to_vec_pretty(&report)
                .map_err(|_| Error::new("fixture", "Invalid report."))?;
            if bytes.len() > 32768 {
                return Err(Error::new("fixture", "Report too large."));
            }
            let path = std::env::var_os("AWTRIX_QUALIFICATION_REPORT")
                .ok_or_else(|| Error::new("fixture", "Missing report path."))?;
            std::fs::write(path, bytes)
                .map_err(|_| Error::new("fixture", "Cannot save report."))?;
            std::process::exit(if report.get("ok") == Some(&Value::Bool(true)) {
                0
            } else {
                1
            });
        }
        _ => Err(Error::new("fixture", "Unknown qualification action.")),
    }
}
