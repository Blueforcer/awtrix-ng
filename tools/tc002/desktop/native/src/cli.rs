use awtrix_installer::runtime;
use awtrix_installer::service::{guarded, Error, Reply, Service};
use base64::{engine::general_purpose::STANDARD, Engine};
use serde::Deserialize;
use serde_json::{json, Value};
use std::{
    ffi::OsString,
    io::{IsTerminal, Write},
    path::PathBuf,
    process::Stdio,
    sync::Arc,
};
use tokio::{
    io::{AsyncBufReadExt, AsyncReadExt, AsyncWriteExt, BufReader},
    process::Command,
    sync::{mpsc, Semaphore},
};

#[cfg(not(test))]
static ASSETS: include_dir::Dir<'_> = include_dir::include_dir!("$AWTRIX_INSTALLER_ASSETS");
const MAX_REQUEST: usize = 16 * 1024;

#[derive(Debug, PartialEq, Eq)]
enum Arguments {
    Install(Option<PathBuf>),
    CheckRelease,
    Help,
    Version,
}

fn parse_arguments(args: impl IntoIterator<Item = OsString>) -> Result<Arguments, String> {
    let args: Vec<_> = args.into_iter().collect();
    match args.as_slice() {
        [] => Ok(Arguments::Install(None)),
        [flag] if flag == "--check-release" => Ok(Arguments::CheckRelease),
        [flag] if flag == "--help" || flag == "-h" => Ok(Arguments::Help),
        [flag] if flag == "--version" => Ok(Arguments::Version),
        [flag] if flag == "--firmware" => Err("Missing file path after --firmware. Use --help.".into()),
        [flag, path] if flag == "--firmware" => {
            if path.is_empty() || path.as_encoded_bytes().starts_with(b"-") {
                return Err("Specify a file path after --firmware. Use ./ before a filename that starts with '-'.".into());
            }
            let path = std::path::absolute(path)
                .map_err(|_| "Cannot resolve the firmware file path from the current directory.")?;
            Ok(Arguments::Install(Some(path)))
        }
        _ => Err("Unknown or incompatible arguments. Use --firmware <path>, --check-release, --help or --version separately.".into()),
    }
}

#[derive(Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
struct Request {
    request_id: u64,
    command: String,
    #[serde(default)]
    args: Value,
    #[serde(default)]
    bytes: Option<String>,
}

fn parse_request(line: &[u8]) -> Result<Request, Error> {
    if line.len() > MAX_REQUEST {
        return Err(Error::new("invalid", "Terminal request is too large."));
    }
    let request: Request = serde_json::from_slice(line)
        .map_err(|_| Error::new("invalid", "Invalid terminal request."))?;
    if request.command.len() > 64 {
        return Err(Error::new("invalid", "Invalid terminal command."));
    }
    Ok(request)
}

async fn console_prompt(args: Value) -> Result<Value, Error> {
    let message = args
        .get("message")
        .and_then(Value::as_str)
        .filter(|v| v.len() <= 1024)
        .ok_or_else(|| Error::new("invalid", "Invalid terminal prompt."))?
        .to_owned();
    let secret = args.get("secret").and_then(Value::as_bool).unwrap_or(false);
    tokio::task::spawn_blocking(move || {
        if !std::io::stdin().is_terminal() {
            return Err(Error::new(
                "cancelled",
                "Interactive installation requires a terminal.",
            ));
        }
        if secret {
            return rpassword::prompt_password(message)
                .map(Value::String)
                .map_err(|_| Error::new("cancelled", "Password input cancelled."));
        }
        print!("{message}");
        std::io::stdout()
            .flush()
            .map_err(|_| Error::new("terminal", "Cannot write to the terminal."))?;
        let mut answer = String::new();
        if std::io::stdin()
            .read_line(&mut answer)
            .map_err(|_| Error::new("cancelled", "Terminal input cancelled."))?
            == 0
        {
            return Err(Error::new("cancelled", "Terminal input ended."));
        }
        if answer.len() > 4096 {
            return Err(Error::new("invalid", "Terminal input is too long."));
        }
        Ok(Value::String(answer.trim_end_matches(['\r', '\n']).into()))
    })
    .await
    .map_err(|_| Error::new("terminal", "Terminal input failed."))?
}

async fn dispatch(request: Request, service: Arc<Service>) -> Value {
    let id = request.request_id;
    let result = async {
        match request.command.as_str() {
            "console_prompt" => return console_prompt(request.args).await.map(Reply::Json),
            "console_message" => {
                let message = request
                    .args
                    .get("message")
                    .and_then(Value::as_str)
                    .filter(|m| m.len() <= 4096)
                    .ok_or_else(|| Error::new("invalid", "Invalid terminal message."))?;
                println!("{message}");
                return Ok(Reply::Json(Value::Null));
            }
            _ => {}
        }
        let bytes = request
            .bytes
            .map(|s| {
                STANDARD
                    .decode(s)
                    .map_err(|_| Error::new("invalid", "Invalid binary terminal data."))
            })
            .transpose()?
            .unwrap_or_default();
        service.call(&request.command, request.args, bytes).await
    }
    .await;
    match result {
        Ok(Reply::Bytes(bytes)) => json!({"requestId":id, "bytes":STANDARD.encode(bytes)}),
        Ok(Reply::Json(result)) => json!({"requestId":id, "result":result}),
        Err(error) => json!({"requestId":id, "error":error}),
    }
}

async fn run(check_release: bool, firmware: Option<PathBuf>) -> Result<i32, String> {
    if check_release {
        match awtrix_installer::download::release_firmware(|progress| {
            if progress.received == 0 {
                println!("{}…", progress.stage);
            }
        })
        .await
        {
            Ok(bytes) => {
                println!("Verified TC002 download: {} bytes", bytes.len());
                return Ok(0);
            }
            Err(error) => return Err(error),
        }
    }
    if let Some(path) = firmware.as_deref() {
        awtrix_installer::sidecar::validate_file(path, awtrix_installer::download::MAX_FIRMWARE)
            .map_err(|error| format!("Selected firmware: {error}"))?;
    }
    if !std::io::stdin().is_terminal() {
        return Err("Start this installer in an interactive terminal. Use --check-release to check GitHub without USB access.".into());
    }
    let directory =
        tempfile::tempdir().map_err(|_| "Cannot create private installer workspace.")?;
    prepare_assets(directory.path())?;
    let node = runtime::node(directory.path()).await?;
    let mut child = Command::new(node)
        .arg(directory.path().join("cli/main.mjs"))
        .env_remove("NODE_OPTIONS")
        .env_remove("NODE_PATH")
        .current_dir(directory.path())
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(Stdio::inherit())
        .kill_on_drop(true)
        .spawn()
        .map_err(|_| "Cannot start the terminal runtime.")?;
    let input = child.stdin.take().ok_or("Cannot open terminal bridge")?;
    let output = child.stdout.take().ok_or("Cannot open terminal bridge")?;
    let (send, mut receive) = mpsc::unbounded_channel::<Value>();
    let event_send = send.clone();
    let service = Arc::new(Service::with_firmware(
        Arc::new(move |name, payload| {
            let _ = event_send.send(json!({"event":name,"payload":payload}));
        }),
        firmware,
    ));
    let writer = tokio::spawn(async move {
        let mut input = input;
        while let Some(value) = receive.recv().await {
            let mut bytes =
                serde_json::to_vec(&value).map_err(|_| "Cannot serialize terminal response")?;
            bytes.push(b'\n');
            input
                .write_all(&bytes)
                .await
                .map_err(|_| "The terminal connection closed")?;
        }
        Ok::<(), &str>(())
    });
    let mut output = BufReader::new(output);
    let permits = Arc::new(Semaphore::new(16));
    let mut pending = tokio::task::JoinSet::new();
    let mut line = Vec::new();
    let mut cancelled = false;
    loop {
        tokio::select! {
            signal = tokio::signal::ctrl_c() => {
                if signal.is_err() { continue; }
                if guarded(&service.current_phase()) {
                    eprintln!("Installation is incomplete. Keep the clock powered and use the retry option in this terminal.");
                } else { cancelled = true; let _ = child.kill().await; break; }
            }
            read = async { (&mut output).take((MAX_REQUEST + 1 - line.len()) as u64).read_until(b'\n', &mut line).await } => {
                match read {
                    Ok(0) => break,
                    Ok(_) => {
                        let request = parse_request(&line).map_err(|error| error.message)?;
                        line.clear();
                        let permit = permits.clone().try_acquire_owned().map_err(|_| "Too many concurrent terminal requests.")?;
                        let service = service.clone();
                        let send = send.clone();
                        pending.spawn(async move { let _permit = permit; let _ = send.send(dispatch(request, service).await); });
                    }
                    Err(_) => return Err("The terminal bridge was interrupted.".into()),
                }
            }
            Some(_) = pending.join_next(), if !pending.is_empty() => {}
        }
    }
    pending.abort_all();
    writer.abort();
    let status = child
        .wait()
        .await
        .map_err(|_| "Cannot read terminal installer status.")?;
    if guarded(&service.current_phase()) {
        eprintln!("The installation did not finish. Keep the clock powered; do not restart it.");
    }
    Ok(if cancelled {
        130
    } else {
        status.code().unwrap_or(1)
    })
}

#[tokio::main]
async fn main() {
    let arguments = match parse_arguments(std::env::args_os().skip(1)) {
        Ok(arguments) => arguments,
        Err(error) => {
            eprintln!("{error}");
            std::process::exit(2);
        }
    };
    let result = match arguments {
        Arguments::Help => {
            println!("AWTRIX NG TC002 terminal installer\n\nUsage: awtrix-tc002-installer-cli [--firmware <path> | --check-release | --help | --version]\n\nRun without arguments for guided USB installation and optional Wi-Fi setup.\nUse --firmware <path> to select a local firmware ZIP; it takes precedence over adjacent firmware and GitHub.\nPlace usb-awtrix-ng-tc002.zip beside this executable to use local firmware offline.\n--check-release checks the latest GitHub firmware without accessing USB.\nNode.js 24+ is reused when installed; otherwise a matching local runtime archive is used or a verified runtime is downloaded automatically.");
            return;
        }
        Arguments::Version => {
            println!("{}", env!("CARGO_PKG_VERSION"));
            return;
        }
        Arguments::CheckRelease => run(true, None).await,
        Arguments::Install(firmware) => run(false, firmware).await,
    };
    match result {
        Ok(code) => std::process::exit(code),
        Err(error) => {
            eprintln!("{error}");
            std::process::exit(1);
        }
    }
}

#[cfg(not(test))]
fn prepare_assets(path: &std::path::Path) -> Result<(), String> {
    ASSETS
        .extract(path)
        .map_err(|_| "Cannot prepare the terminal installer.".into())
}

#[cfg(test)]
fn prepare_assets(_: &std::path::Path) -> Result<(), String> {
    Err("Test harness does not extract application assets.".into())
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn firmware_argument_preserves_spaces_and_unicode_and_resolves_the_path() {
        let path = PathBuf::from("firmware downloads/Clock crème 東京.zip");
        assert_eq!(
            parse_arguments([OsString::from("--firmware"), path.clone().into_os_string()]).unwrap(),
            Arguments::Install(Some(std::path::absolute(path).unwrap()))
        );
    }

    #[test]
    fn no_argument_installation_and_standalone_options_remain_available() {
        assert_eq!(parse_arguments([]).unwrap(), Arguments::Install(None));
        for (flag, expected) in [
            ("--check-release", Arguments::CheckRelease),
            ("--help", Arguments::Help),
            ("-h", Arguments::Help),
            ("--version", Arguments::Version),
        ] {
            assert_eq!(parse_arguments([flag.into()]).unwrap(), expected);
        }
    }

    #[test]
    fn rejects_missing_unknown_multiple_and_mixed_firmware_arguments() {
        for args in [
            vec!["--firmware"],
            vec!["--firmware", ""],
            vec!["--firmware", "--help"],
            vec!["--firmware", "--check-release"],
            vec!["--firmware=firmware.zip"],
            vec!["--unknown"],
            vec!["firmware.zip"],
            vec!["--firmware", "one.zip", "two.zip"],
            vec!["--firmware", "one.zip", "--firmware", "two.zip"],
            vec!["--firmware", "one.zip", "--check-release"],
            vec!["--check-release", "--firmware", "one.zip"],
            vec!["--help", "--firmware", "one.zip"],
            vec!["--firmware", "one.zip", "--help"],
            vec!["--version", "--firmware", "one.zip"],
            vec!["--firmware", "one.zip", "--version"],
        ] {
            assert!(
                parse_arguments(args.iter().map(OsString::from)).is_err(),
                "{args:?}"
            );
        }
    }

    #[test]
    fn accepts_absolute_paths_and_explicit_dash_prefixed_filenames() {
        let directory = tempfile::tempdir().unwrap();
        let absolute = directory.path().join("firmware.zip");
        assert_eq!(
            parse_arguments(["--firmware".into(), absolute.clone().into_os_string()]).unwrap(),
            Arguments::Install(Some(absolute))
        );
        let path = PathBuf::from("./--firmware.zip");
        assert_eq!(
            parse_arguments(["--firmware".into(), path.clone().into_os_string()]).unwrap(),
            Arguments::Install(Some(std::path::absolute(path).unwrap()))
        );
    }

    #[cfg(unix)]
    #[test]
    fn firmware_paths_preserve_non_utf8_os_bytes() {
        use std::os::unix::ffi::OsStringExt;
        let path = OsString::from_vec(b"firmware-\xff.zip".to_vec());
        assert_eq!(
            parse_arguments(["--firmware".into(), path.clone()]).unwrap(),
            Arguments::Install(Some(std::path::absolute(path).unwrap()))
        );
        assert!(parse_arguments([OsString::from_vec(b"--invalid-\xff".to_vec())]).is_err());
    }

    #[tokio::test]
    async fn a_missing_selected_file_fails_before_starting_the_runtime() {
        let directory = tempfile::tempdir().unwrap();
        let error = run(false, Some(directory.path().join("missing.zip")))
            .await
            .unwrap_err();
        assert!(error.starts_with("Selected firmware:"));
        assert!(error.contains("missing.zip"));
    }
    #[test]
    fn bridge_rejects_malformed_or_oversized_requests() {
        assert!(parse_request(b"not json").is_err());
        assert!(parse_request(&vec![b'x'; MAX_REQUEST + 1]).is_err());
        assert!(parse_request(br#"{"requestId":1,"command":"phase","unexpected":true}"#).is_err());
    }
    #[tokio::test]
    async fn bridge_preserves_error_and_null_responses() {
        let service = Arc::new(Service::new(Arc::new(|_, _| {})));
        let req = parse_request(br#"{"requestId":4,"command":"phase","args":{"value":"ready"}}"#)
            .unwrap();
        assert_eq!(
            dispatch(req, service.clone()).await,
            json!({"requestId":4,"result":null})
        );
        let req = parse_request(br#"{"requestId":5,"command":"unsupported"}"#).unwrap();
        assert_eq!(dispatch(req, service).await["error"]["code"], "invalid");
    }
}
