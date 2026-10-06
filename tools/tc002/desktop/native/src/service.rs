use crate::{download, usb::UsbManager};
use futures_util::future::{AbortHandle, AbortRegistration, Abortable};
use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use std::{
    future::Future,
    path::PathBuf,
    sync::{Arc, Mutex},
};
use tokio::sync::{Mutex as AsyncMutex, OnceCell};

#[derive(Clone, Debug, Serialize)]
pub struct Error {
    pub code: String,
    pub message: String,
}

impl Error {
    pub fn new(code: &str, message: impl Into<String>) -> Self {
        Self {
            code: code.into(),
            message: message.into(),
        }
    }
}
impl From<crate::usb::UsbError> for Error {
    fn from(error: crate::usb::UsbError) -> Self {
        Self::new(error.code, error.message)
    }
}
impl std::fmt::Display for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", self.message)
    }
}
impl std::error::Error for Error {}

pub enum Reply {
    Json(Value),
    Bytes(Vec<u8>),
}
pub type Emit = Arc<dyn Fn(&str, Value) + Send + Sync>;

pub struct Service {
    usb: OnceCell<Arc<UsbManager>>,
    firmware: Mutex<Firmware>,
    picker: AsyncMutex<()>,
    pub phase: Mutex<String>,
    emit: Emit,
}

struct Firmware {
    selected: Option<PathBuf>,
    active: Option<AbortHandle>,
    generation: u64,
}

fn firmware_error(message: String) -> Error {
    Error::new(
        if message.starts_with("Local firmware:") {
            "local-firmware"
        } else if message.starts_with("No TC002 installation firmware") {
            "no-compatible-release"
        } else {
            "download"
        },
        message,
    )
}

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
struct DeviceArgs {
    id: String,
    #[serde(default)]
    session_id: String,
    #[serde(default)]
    number: u8,
    #[serde(default)]
    value: u8,
    #[serde(default)]
    alternate: u8,
    #[serde(default)]
    endpoint: u8,
    #[serde(default)]
    length: usize,
}

pub fn guarded(phase: &str) -> bool {
    matches!(
        phase,
        "installing" | "recovery" | "reconnecting" | "restarting"
    )
}

pub fn setup_url(ip: &str) -> Result<String, Error> {
    let addr: std::net::Ipv4Addr = ip
        .parse()
        .map_err(|_| Error::new("invalid", "Invalid clock address."))?;
    if !addr.is_private() || addr.to_string() != ip {
        return Err(Error::new(
            "invalid",
            "The clock must be on your local network.",
        ));
    }
    Ok(format!("http://{addr}/"))
}

impl Service {
    pub fn new(emit: Emit) -> Self {
        Self::with_firmware(emit, None)
    }

    pub fn with_firmware(emit: Emit, selected: Option<PathBuf>) -> Self {
        Self {
            usb: OnceCell::new(),
            firmware: Mutex::new(Firmware {
                selected,
                active: None,
                generation: 0,
            }),
            picker: AsyncMutex::new(()),
            phase: Mutex::new("loading".into()),
            emit,
        }
    }

    pub fn current_phase(&self) -> String {
        self.phase.lock().unwrap_or_else(|e| e.into_inner()).clone()
    }

    fn firmware_available(&self) -> Result<(), Error> {
        if matches!(
            self.current_phase().as_str(),
            "loading" | "waiting" | "failed"
        ) {
            Ok(())
        } else {
            Err(Error::new(
                "busy",
                "Firmware cannot be changed while the clock is being prepared or installed.",
            ))
        }
    }

    pub async fn choose_firmware(
        &self,
        choose: impl Future<Output = Result<Option<PathBuf>, Error>>,
    ) -> Result<bool, Error> {
        let _picker = self
            .picker
            .try_lock()
            .map_err(|_| Error::new("busy", "A firmware selection dialog is already open."))?;
        self.firmware_available()?;
        let Some(path) = choose.await? else {
            return Ok(false);
        };
        self.firmware_available()?;
        self.firmware
            .lock()
            .unwrap_or_else(|e| e.into_inner())
            .selected = Some(path);
        Ok(true)
    }

    fn begin_firmware(&self) -> Result<(u64, Option<PathBuf>, AbortRegistration), Error> {
        self.firmware_available()?;
        let mut firmware = self.firmware.lock().unwrap_or_else(|e| e.into_inner());
        if let Some(previous) = firmware.active.take() {
            previous.abort();
        }
        let (abort, registration) = AbortHandle::new_pair();
        firmware.active = Some(abort);
        firmware.generation = firmware.generation.wrapping_add(1);
        Ok((firmware.generation, firmware.selected.clone(), registration))
    }

    async fn load_firmware(&self, args: Value) -> Result<Vec<u8>, Error> {
        let request_id = match args.get("requestId") {
            None | Some(Value::Null) => None,
            Some(Value::String(value)) if value.len() <= 128 => Some(value.clone()),
            _ => {
                return Err(Error::new(
                    "invalid",
                    "Invalid firmware request identifier.",
                ))
            }
        };
        let (generation, selected, registration) = self.begin_firmware()?;
        let progress = |progress| {
            if self
                .firmware
                .lock()
                .unwrap_or_else(|e| e.into_inner())
                .generation
                != generation
            {
                return;
            }
            if let Ok(mut value) = serde_json::to_value(progress) {
                value["requestId"] = json!(request_id);
                (self.emit)("firmware-progress", value);
            }
        };
        Abortable::new(
            async {
                match selected {
                    Some(path) => download::local_firmware(&path, progress),
                    None => download::firmware(progress).await,
                }
            },
            registration,
        )
        .await
        .map_err(|_| {
            Error::new(
                "cancelled",
                "Firmware loading was replaced by a newer selection.",
            )
        })?
        .map_err(firmware_error)
    }

    async fn usb(&self) -> Result<&Arc<UsbManager>, Error> {
        self.usb
            .get_or_try_init(|| async {
                let manager = UsbManager::new().await?;
                let mut events = manager.subscribe();
                let emit = self.emit.clone();
                tokio::spawn(async move {
                    loop {
                        match events.recv().await {
                            Ok(event) => {
                                if let Ok(value) = serde_json::to_value(event) {
                                    emit("usb-event", value);
                                }
                            }
                            Err(tokio::sync::broadcast::error::RecvError::Lagged(_)) => {
                                emit("usb-event", json!({"kind":"rescan"}))
                            }
                            Err(_) => break,
                        }
                    }
                });
                Ok(manager)
            })
            .await
    }

    pub async fn call(&self, command: &str, args: Value, bytes: Vec<u8>) -> Result<Reply, Error> {
        #[cfg(feature = "qualification")]
        if let Some(reply) = crate::qualification::intercept(command) {
            return reply;
        }
        match command {
            "firmware" => {
                return Ok(Reply::Bytes(self.load_firmware(args).await?));
            }
            "phase" => {
                let value = args.get("value").and_then(Value::as_str).unwrap_or("");
                if !matches!(
                    value,
                    "loading"
                        | "waiting"
                        | "preparing"
                        | "ready"
                        | "installing"
                        | "recovery"
                        | "reconnecting"
                        | "verified"
                        | "restarting"
                        | "provisioning"
                        | "done"
                        | "failed"
                ) {
                    return Err(Error::new("invalid", "Invalid installation state."));
                }
                *self.phase.lock().unwrap_or_else(|e| e.into_inner()) = value.into();
                return Ok(Reply::Json(Value::Null));
            }
            "usb_list" => return Ok(Reply::Json(json!(self.usb().await?.list().await?))),
            _ => {}
        }
        if !matches!(
            command,
            "usb_open"
                | "usb_select_configuration"
                | "usb_claim_interface"
                | "usb_select_alternate_interface"
                | "usb_transfer_in"
                | "usb_transfer_out"
                | "usb_close"
        ) {
            return Err(Error::new("invalid", "Unknown installer command."));
        }
        let args: DeviceArgs = serde_json::from_value(args)
            .map_err(|_| Error::new("invalid", "Invalid USB request."))?;
        if args.id.len() > 128 || args.session_id.len() > 128 || bytes.len() > 4096 {
            return Err(Error::new(
                "invalid",
                "USB request exceeds its permitted size.",
            ));
        }
        let usb = self.usb().await?;
        match command {
            "usb_open" => return Ok(Reply::Json(json!(usb.open(&args.id).await?))),
            "usb_select_configuration" => {
                usb.select_configuration(&args.id, &args.session_id, args.value)
                    .await?
            }
            "usb_claim_interface" => {
                usb.claim_interface(&args.id, &args.session_id, args.number)
                    .await?
            }
            "usb_select_alternate_interface" => {
                usb.select_alternate_interface(
                    &args.id,
                    &args.session_id,
                    args.number,
                    args.alternate,
                )
                .await?
            }
            "usb_transfer_in" => {
                return Ok(Reply::Bytes(
                    usb.transfer_in(&args.id, &args.session_id, args.endpoint, args.length)
                        .await?,
                ))
            }
            "usb_transfer_out" => {
                return Ok(Reply::Json(json!(
                    usb.transfer_out(&args.id, &args.session_id, args.endpoint, bytes)
                        .await?
                )))
            }
            "usb_close" => usb.close(&args.id, &args.session_id).await?,
            _ => unreachable!(),
        }
        Ok(Reply::Json(Value::Null))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[tokio::test]
    async fn selected_file_is_reloaded_and_progress_belongs_to_its_request() {
        let directory = tempfile::tempdir().unwrap();
        let path = directory.path().join("selected firmware.zip");
        std::fs::write(&path, b"selected bytes").unwrap();
        let events = Arc::new(Mutex::new(Vec::new()));
        let recorded = events.clone();
        let service = Service::with_firmware(
            Arc::new(move |name, value| {
                recorded.lock().unwrap().push((name.to_owned(), value));
            }),
            Some(path.clone()),
        );
        let bytes = service
            .load_firmware(json!({"requestId":"first"}))
            .await
            .unwrap();
        assert_eq!(bytes, b"selected bytes");
        assert!(events
            .lock()
            .unwrap()
            .iter()
            .all(|(name, value)| name == "firmware-progress"
                && value["requestId"] == "first"
                && value["stage"] == "local"));
        std::fs::write(&path, b"replacement").unwrap();
        assert_eq!(
            service
                .load_firmware(json!({"requestId":"second"}))
                .await
                .unwrap(),
            b"replacement"
        );
        std::fs::remove_file(path).unwrap();
        assert_eq!(
            service.load_firmware(Value::Null).await.unwrap_err().code,
            "local-firmware"
        );
        assert!(service.usb.get().is_none());
    }

    #[tokio::test]
    async fn cancelling_picker_keeps_selection_and_busy_phases_cannot_change_it() {
        let service =
            Service::with_firmware(Arc::new(|_, _| {}), Some(PathBuf::from("original.zip")));
        assert!(!service.choose_firmware(async { Ok(None) }).await.unwrap());
        assert_eq!(
            service.firmware.lock().unwrap().selected,
            Some(PathBuf::from("original.zip"))
        );
        let gate = service.picker.lock().await;
        assert_eq!(
            service
                .choose_firmware(async { panic!("Second dialog must not open") })
                .await
                .unwrap_err()
                .code,
            "busy"
        );
        drop(gate);
        assert!(service
            .choose_firmware(async { Ok(Some(PathBuf::from("chosen.zip"))) })
            .await
            .unwrap());
        assert_eq!(
            service.firmware.lock().unwrap().selected,
            Some(PathBuf::from("chosen.zip"))
        );
        for phase in [
            "preparing",
            "ready",
            "installing",
            "recovery",
            "verified",
            "restarting",
            "provisioning",
            "done",
        ] {
            service
                .call("phase", json!({"value":phase}), vec![])
                .await
                .unwrap();
            assert_eq!(
                service
                    .choose_firmware(async { panic!("Busy phase must not open a dialog") })
                    .await
                    .unwrap_err()
                    .code,
                "busy"
            );
            assert_eq!(
                service.load_firmware(Value::Null).await.unwrap_err().code,
                "busy"
            );
        }
    }

    #[tokio::test]
    async fn changed_phase_while_picking_cannot_overwrite_selection() {
        let service = Service::new(Arc::new(|_, _| {}));
        let result = service
            .choose_firmware(async {
                service
                    .call("phase", json!({"value":"preparing"}), vec![])
                    .await?;
                Ok(Some(PathBuf::from("too-late.zip")))
            })
            .await;
        assert_eq!(result.unwrap_err().code, "busy");
        assert!(service.firmware.lock().unwrap().selected.is_none());
    }

    #[tokio::test]
    async fn newer_firmware_request_aborts_inflight_work() {
        let service = Service::new(Arc::new(|_, _| {}));
        let (_, _, first) = service.begin_firmware().unwrap();
        let pending = Abortable::new(std::future::pending::<()>(), first);
        let (_, _, second) = service.begin_firmware().unwrap();
        assert!(pending.await.is_err());
        assert!(Abortable::new(async { 42 }, second).await.is_ok());
        assert_eq!(
            service
                .load_firmware(json!({"requestId":123}))
                .await
                .unwrap_err()
                .code,
            "invalid"
        );
    }

    #[test]
    fn setup_only_opens_private_ipv4() {
        for address in ["192.168.4.1", "10.0.0.1", "172.16.0.1", "172.31.255.254"] {
            assert!(setup_url(address).is_ok());
        }
        for address in [
            "localhost",
            "127.0.0.1",
            "8.8.8.8",
            "172.32.0.1",
            "192.168.1.1:80",
            "192.168.01.1",
            "10.0.0.1/path",
            "[::1]",
        ] {
            assert!(setup_url(address).is_err());
        }
    }
    #[tokio::test]
    async fn rejects_unknown_calls_without_initializing_usb() {
        let service = Service::new(Arc::new(|_, _| {}));
        assert!(service.call("shell", json!({}), vec![]).await.is_err());
        assert!(service
            .call("phase", json!({"value":"unsafe"}), vec![])
            .await
            .is_err());
        assert!(service
            .call("phase", json!({"value":"installing"}), vec![])
            .await
            .is_ok());
        assert!(guarded(&service.current_phase()));
        assert!(service.usb.get().is_none());
    }
}
