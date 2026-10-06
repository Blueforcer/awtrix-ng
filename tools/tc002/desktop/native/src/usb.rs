use futures_util::{future::BoxFuture, StreamExt};
use nusb::{
    descriptors::{ConfigurationDescriptor, TransferType},
    hotplug::HotplugEvent,
    transfer::{Buffer, Bulk, Direction, In, Out, TransferError},
    Device, DeviceId, DeviceInfo, Endpoint, Interface,
};
use serde::Serialize;
use std::{
    collections::{BTreeMap, HashMap, VecDeque},
    sync::{Arc, Mutex, MutexGuard},
    time::Duration,
};
use tokio::sync::{broadcast, watch, Mutex as AsyncMutex};

const VENDOR_ID: u16 = 0x18d1;
const PRODUCT_ID: u16 = 0xd002;
const MAX_TRANSFER: usize = 4096;
const OPERATION_TIMEOUT: Duration = Duration::from_secs(10);
const TRANSFER_TIMEOUT: Duration = Duration::from_secs(600);

#[derive(Clone, Debug, Serialize)]
pub struct UsbError {
    pub code: &'static str,
    pub message: String,
}

impl UsbError {
    fn new(code: &'static str, message: impl Into<String>) -> Self {
        Self {
            code,
            message: message.into(),
        }
    }

    fn disconnected() -> Self {
        Self::new("disconnected", "The USB connection has closed.")
    }
}

impl std::fmt::Display for UsbError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", self.message)
    }
}

impl std::error::Error for UsbError {}

impl From<nusb::Error> for UsbError {
    fn from(error: nusb::Error) -> Self {
        let code = match error.kind() {
            nusb::ErrorKind::PermissionDenied => "denied",
            nusb::ErrorKind::Busy => "claim",
            nusb::ErrorKind::Disconnected => "disconnected",
            nusb::ErrorKind::Unsupported => "unsupported",
            _ => "transport",
        };
        let message = match code {
            "claim" => "USB is in use. Close other ADB, phone-management or clock-installation tools, then try again.".to_owned(),
            "denied" if cfg!(target_os = "linux") => "USB access is denied. Install the included TC002 USB permission rule. For terminal use, join the plugdev group and log in again; see the installation guide.".to_owned(),
            "denied" => "USB access is denied. Close other USB tools and check the TC002 USB driver and device permissions.".to_owned(),
            "unsupported" => "The current USB driver does not support this connection. On Windows, the TC002 ADB interface needs WinUSB.".to_owned(),
            _ => error.to_string(),
        };
        Self::new(code, message)
    }
}

impl From<TransferError> for UsbError {
    fn from(error: TransferError) -> Self {
        let code = match error {
            TransferError::Disconnected => "disconnected",
            TransferError::InvalidArgument => "invalid",
            TransferError::Cancelled => "timeout",
            _ => "transport",
        };
        Self::new(code, error.to_string())
    }
}

#[derive(Clone, Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct DeviceMetadata {
    pub id: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub session_id: Option<String>,
    pub vendor_id: u16,
    pub product_id: u16,
    pub serial_number: String,
    pub configurations: Vec<ConfigurationMetadata>,
    pub configuration: Option<ConfigurationMetadata>,
}

#[derive(Clone, Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ConfigurationMetadata {
    pub configuration_value: u8,
    pub interfaces: Vec<InterfaceMetadata>,
}

#[derive(Clone, Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct InterfaceMetadata {
    pub interface_number: u8,
    pub alternates: Vec<AlternateMetadata>,
    pub alternate: Option<AlternateMetadata>,
}

#[derive(Clone, Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct AlternateMetadata {
    pub alternate_setting: u8,
    pub interface_class: u8,
    pub interface_subclass: u8,
    pub interface_protocol: u8,
    pub endpoints: Vec<EndpointMetadata>,
}

#[derive(Clone, Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct EndpointMetadata {
    pub endpoint_number: u8,
    pub direction: &'static str,
    #[serde(rename = "type")]
    pub transfer_type: &'static str,
    pub packet_size: usize,
}

#[derive(Clone, Debug, Serialize)]
pub struct UsbEvent {
    pub kind: &'static str,
    pub device: DeviceMetadata,
}

fn configuration_metadata(descriptor: ConfigurationDescriptor<'_>) -> ConfigurationMetadata {
    let mut interfaces: BTreeMap<u8, InterfaceMetadata> = BTreeMap::new();
    for alternate in descriptor.interface_alt_settings() {
        if (
            alternate.class(),
            alternate.subclass(),
            alternate.protocol(),
        ) != (255, 66, 1)
        {
            continue;
        }
        let endpoints: Vec<_> = alternate
            .endpoints()
            .filter(|ep| {
                ep.transfer_type() == TransferType::Bulk
                    && ep.max_packet_size().is_power_of_two()
                    && ep.max_packet_size() <= 1024
                    && ep.address() & 0x0f != 0
                    && ep.address() & 0x70 == 0
            })
            .map(|ep| EndpointMetadata {
                endpoint_number: ep.address() & 0x0f,
                direction: if ep.direction() == Direction::In {
                    "in"
                } else {
                    "out"
                },
                transfer_type: "bulk",
                packet_size: ep.max_packet_size(),
            })
            .collect();
        if !endpoints.iter().any(|ep| ep.direction == "in")
            || !endpoints.iter().any(|ep| ep.direction == "out")
        {
            continue;
        }
        let metadata = AlternateMetadata {
            alternate_setting: alternate.alternate_setting(),
            interface_class: alternate.class(),
            interface_subclass: alternate.subclass(),
            interface_protocol: alternate.protocol(),
            endpoints,
        };
        let interface = interfaces
            .entry(alternate.interface_number())
            .or_insert_with(|| InterfaceMetadata {
                interface_number: alternate.interface_number(),
                alternates: Vec::new(),
                alternate: None,
            });
        interface.alternates.push(metadata);
    }
    ConfigurationMetadata {
        configuration_value: descriptor.configuration_value(),
        interfaces: interfaces.into_values().collect(),
    }
}

#[derive(Default)]
struct ReadBuffer(VecDeque<u8>);

impl ReadBuffer {
    fn request_length(length: usize, packet_size: usize) -> Result<usize, UsbError> {
        if length == 0
            || length > MAX_TRANSFER
            || !packet_size.is_power_of_two()
            || packet_size > 1024
        {
            return Err(UsbError::new("invalid", "Invalid USB read size."));
        }
        Ok(length.div_ceil(packet_size) * packet_size)
    }

    fn take(&mut self, length: usize) -> Vec<u8> {
        self.0.drain(..length.min(self.0.len())).collect()
    }

    fn receive(&mut self, bytes: &[u8], length: usize) -> Result<Vec<u8>, UsbError> {
        if !self.0.is_empty() || bytes.len() > MAX_TRANSFER {
            return Err(UsbError::new("transport", "Unexpected USB response size."));
        }
        self.0.extend(bytes);
        Ok(self.take(length))
    }
}

#[derive(Default)]
struct DeviceState {
    device: Option<Device>,
    interface: Option<Interface>,
    input: Option<Endpoint<Bulk, In>>,
    output: Option<Endpoint<Bulk, Out>>,
    metadata: Option<DeviceMetadata>,
    received: ReadBuffer,
}

impl DeviceState {
    fn clear(&mut self) {
        self.input = None;
        self.output = None;
        self.interface = None;
        self.device = None;
        self.metadata = None;
        self.received = ReadBuffer::default();
    }

    fn configuration(&self) -> Result<&ConfigurationMetadata, UsbError> {
        self.metadata
            .as_ref()
            .and_then(|m| m.configuration.as_ref())
            .ok_or_else(|| UsbError::new("invalid", "Select a USB configuration first."))
    }

    fn set_endpoints(&mut self) -> Result<(), UsbError> {
        let interface = self
            .interface
            .as_ref()
            .ok_or_else(|| UsbError::new("invalid", "Claim the USB interface first."))?;
        let selected = self
            .configuration()?
            .interfaces
            .iter()
            .find(|i| i.interface_number == interface.interface_number())
            .and_then(|i| {
                i.alternates
                    .iter()
                    .find(|a| a.alternate_setting == interface.get_alt_setting())
            })
            .ok_or_else(|| {
                UsbError::new(
                    "unsupported",
                    "The selected interface is not a supported ADB interface.",
                )
            })?;
        let input = selected
            .endpoints
            .iter()
            .find(|ep| ep.direction == "in")
            .unwrap()
            .endpoint_number;
        let output = selected
            .endpoints
            .iter()
            .find(|ep| ep.direction == "out")
            .unwrap()
            .endpoint_number;
        self.input = Some(interface.endpoint::<Bulk, In>(input | 0x80)?);
        self.output = Some(interface.endpoint::<Bulk, Out>(output)?);
        Ok(())
    }
}

struct Session {
    device_id: String,
    cancelled: watch::Sender<bool>,
    state: AsyncMutex<DeviceState>,
}

async fn cancelled(receiver: &mut watch::Receiver<bool>) {
    while !*receiver.borrow_and_update() {
        if receiver.changed().await.is_err() {
            return;
        }
    }
}

impl Session {
    fn new(device_id: String) -> Self {
        let (cancelled, _) = watch::channel(false);
        Self {
            device_id,
            cancelled,
            state: AsyncMutex::new(DeviceState::default()),
        }
    }

    fn stop(&self) {
        self.cancelled.send_replace(true);
    }

    async fn close(&self) {
        self.stop();
        self.state.lock().await.clear();
    }

    async fn transfer<T: Send>(
        &self,
        operation: impl for<'a> FnOnce(&'a mut DeviceState) -> BoxFuture<'a, Result<T, UsbError>>,
    ) -> Result<T, UsbError> {
        self.execute_for(TRANSFER_TIMEOUT, operation).await
    }

    async fn execute<T: Send>(
        &self,
        operation: impl for<'a> FnOnce(&'a mut DeviceState) -> BoxFuture<'a, Result<T, UsbError>>,
    ) -> Result<T, UsbError> {
        self.execute_for(OPERATION_TIMEOUT, operation).await
    }

    async fn execute_for<T: Send>(
        &self,
        timeout: Duration,
        operation: impl for<'a> FnOnce(&'a mut DeviceState) -> BoxFuture<'a, Result<T, UsbError>>,
    ) -> Result<T, UsbError> {
        let mut receiver = self.cancelled.subscribe();
        let mut state = tokio::select! {
            biased;
            _ = cancelled(&mut receiver) => return Err(UsbError::disconnected()),
            state = self.state.lock() => state,
        };
        let result = tokio::select! {
            biased;
            _ = cancelled(&mut receiver) => Err(UsbError::disconnected()),
            result = tokio::time::timeout(timeout, operation(&mut state)) => {
                result.unwrap_or_else(|_| Err(UsbError::new("timeout", "The USB operation timed out.")))
            }
        };
        if result.is_err() {
            self.stop();
            state.clear();
        }
        result
    }
}

struct DeviceEntry {
    info: DeviceInfo,
    metadata: DeviceMetadata,
}

#[derive(Default)]
struct Registry {
    generation: u64,
    devices: HashMap<DeviceId, DeviceEntry>,
    sessions: HashMap<String, Arc<Session>>,
}

impl Registry {
    fn token(&mut self, prefix: &str) -> String {
        self.generation = self
            .generation
            .checked_add(1)
            .expect("USB generation exhausted");
        format!("{prefix}-{}", self.generation)
    }

    fn connect(&mut self, info: DeviceInfo) -> Option<DeviceMetadata> {
        if info.vendor_id() != VENDOR_ID || info.product_id() != PRODUCT_ID {
            return None;
        }
        if let Some(entry) = self.devices.get_mut(&info.id()) {
            entry.info = info;
            return None;
        }
        let metadata = DeviceMetadata {
            id: self.token("device"),
            session_id: None,
            vendor_id: info.vendor_id(),
            product_id: info.product_id(),
            serial_number: info.serial_number().unwrap_or_default().to_owned(),
            configurations: Vec::new(),
            configuration: None,
        };
        self.devices.insert(
            info.id(),
            DeviceEntry {
                info,
                metadata: metadata.clone(),
            },
        );
        Some(metadata)
    }

    fn remove_sessions(&mut self, device_id: &str) -> Vec<Arc<Session>> {
        let keys: Vec<_> = self
            .sessions
            .iter()
            .filter(|(_, session)| session.device_id == device_id)
            .map(|(key, _)| key.clone())
            .collect();
        keys.into_iter()
            .filter_map(|key| self.sessions.remove(&key))
            .inspect(|session| session.stop())
            .collect()
    }
}

pub struct UsbManager {
    registry: Mutex<Registry>,
    events: broadcast::Sender<UsbEvent>,
    stop: watch::Sender<bool>,
}

impl UsbManager {
    pub async fn new() -> Result<Arc<Self>, UsbError> {
        let mut watcher = nusb::watch_devices()?;
        let initial: Vec<_> = nusb::list_devices().await?.collect();
        let (events, _) = broadcast::channel(64);
        let (stop, mut stopped) = watch::channel(false);
        let manager = Arc::new(Self {
            registry: Mutex::new(Registry::default()),
            events,
            stop,
        });
        for info in initial {
            manager.registry().connect(info);
        }
        let weak = Arc::downgrade(&manager);
        tokio::spawn(async move {
            loop {
                let event = tokio::select! {
                    biased;
                    _ = cancelled(&mut stopped) => break,
                    event = watcher.next() => match event { Some(event) => event, None => break },
                };
                let Some(manager) = weak.upgrade() else {
                    break;
                };
                manager.hotplug(event);
            }
        });
        Ok(manager)
    }

    fn registry(&self) -> MutexGuard<'_, Registry> {
        self.registry
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner())
    }

    fn hotplug(&self, event: HotplugEvent) {
        let mut registry = self.registry();
        match event {
            HotplugEvent::Connected(info) => {
                if let Some(device) = registry.connect(info) {
                    let _ = self.events.send(UsbEvent {
                        kind: "connect",
                        device,
                    });
                }
            }
            HotplugEvent::Disconnected(id) => {
                if let Some(entry) = registry.devices.remove(&id) {
                    let sessions = registry.remove_sessions(&entry.metadata.id);
                    let _ = self.events.send(UsbEvent {
                        kind: "disconnect",
                        device: entry.metadata,
                    });
                    drop(registry);
                    for session in sessions {
                        tokio::spawn(async move {
                            session.close().await;
                        });
                    }
                }
            }
        }
    }

    pub fn subscribe(&self) -> broadcast::Receiver<UsbEvent> {
        self.events.subscribe()
    }

    pub async fn list(&self) -> Result<Vec<DeviceMetadata>, UsbError> {
        let mut result: Vec<_> = self
            .registry()
            .devices
            .values()
            .map(|entry| entry.metadata.clone())
            .collect();
        result.sort_by(|a, b| a.id.cmp(&b.id));
        Ok(result)
    }

    fn session(&self, id: &str, session_id: &str) -> Result<Arc<Session>, UsbError> {
        self.registry()
            .sessions
            .get(session_id)
            .filter(|s| s.device_id == id)
            .cloned()
            .ok_or_else(UsbError::disconnected)
    }

    pub async fn open(&self, id: &str) -> Result<DeviceMetadata, UsbError> {
        let (info, mut metadata, token, session) = {
            let mut registry = self.registry();
            if registry.devices.len() > 1 {
                return Err(UsbError::new("invalid", "Connect only one TC002 clock."));
            }
            let entry = registry
                .devices
                .values()
                .find(|entry| entry.metadata.id == id)
                .ok_or_else(UsbError::disconnected)?;
            let (info, metadata) = (entry.info.clone(), entry.metadata.clone());
            if registry
                .sessions
                .values()
                .any(|s| s.device_id == id && !*s.cancelled.borrow())
            {
                return Err(UsbError::new(
                    "claim",
                    "This USB device already has an open connection.",
                ));
            }
            registry.remove_sessions(id);
            let token = registry.token("session");
            let session = Arc::new(Session::new(id.to_owned()));
            registry.sessions.insert(token.clone(), session.clone());
            (info, metadata, token, session)
        };
        metadata.session_id = Some(token.clone());
        let result = session
            .execute(move |state| {
                Box::pin(async move {
                    let device = info.open().await?;
                    if device.device_descriptor().vendor_id() != VENDOR_ID
                        || device.device_descriptor().product_id() != PRODUCT_ID
                    {
                        return Err(UsbError::new(
                            "unsupported",
                            "The USB device identity changed.",
                        ));
                    }
                    let configurations: Vec<_> = device.configurations().collect();
                    if configurations.is_empty() {
                        return Err(UsbError::new(
                            "transport",
                            "The USB configuration descriptors are not ready yet.",
                        ));
                    }
                    metadata.configurations = configurations
                        .into_iter()
                        .map(configuration_metadata)
                        .filter(|c| !c.interfaces.is_empty())
                        .collect();
                    if metadata.configurations.is_empty() {
                        return Err(UsbError::new(
                            "unsupported",
                            "This USB device has no supported ADB interface.",
                        ));
                    }
                    metadata.configuration = device
                        .active_configuration()
                        .ok()
                        .map(configuration_metadata);
                    state.device = Some(device);
                    state.metadata = Some(metadata.clone());
                    Ok(metadata)
                })
            })
            .await;
        if result.is_err() {
            self.close(id, &token).await?;
        }
        result
    }

    pub async fn select_configuration(
        &self,
        id: &str,
        session_id: &str,
        value: u8,
    ) -> Result<(), UsbError> {
        self.session(id, session_id)?
            .execute(move |state| {
                Box::pin(async move {
                    if state.interface.is_some() {
                        return Err(UsbError::new(
                            "invalid",
                            "Cannot change a claimed USB configuration.",
                        ));
                    }
                    let configuration = state
                        .metadata
                        .as_ref()
                        .and_then(|m| {
                            m.configurations
                                .iter()
                                .find(|c| c.configuration_value == value)
                        })
                        .cloned()
                        .ok_or_else(|| {
                            UsbError::new("invalid", "Unsupported USB configuration.")
                        })?;
                    let device = state.device.as_ref().ok_or_else(UsbError::disconnected)?;
                    if device
                        .active_configuration()
                        .ok()
                        .map(|c| c.configuration_value())
                        != Some(value)
                    {
                        device.set_configuration(value).await?;
                    }
                    state.metadata.as_mut().unwrap().configuration = Some(configuration);
                    Ok(())
                })
            })
            .await
    }

    pub async fn claim_interface(
        &self,
        id: &str,
        session_id: &str,
        number: u8,
    ) -> Result<(), UsbError> {
        self.session(id, session_id)?
            .execute(move |state| {
                Box::pin(async move {
                    if state.interface.is_some() {
                        return Err(UsbError::new(
                            "invalid",
                            "A USB interface is already claimed.",
                        ));
                    }
                    if !state
                        .configuration()?
                        .interfaces
                        .iter()
                        .any(|i| i.interface_number == number)
                    {
                        return Err(UsbError::new("invalid", "Unsupported USB interface."));
                    }
                    state.interface = Some(
                        state
                            .device
                            .as_ref()
                            .ok_or_else(UsbError::disconnected)?
                            .claim_interface(number)
                            .await?,
                    );
                    let alternate = state.interface.as_ref().unwrap().get_alt_setting();
                    let metadata = state
                        .metadata
                        .as_mut()
                        .unwrap()
                        .configuration
                        .as_mut()
                        .unwrap()
                        .interfaces
                        .iter_mut()
                        .find(|i| i.interface_number == number)
                        .unwrap();
                    metadata.alternate = metadata
                        .alternates
                        .iter()
                        .find(|a| a.alternate_setting == alternate)
                        .cloned();
                    Ok(())
                })
            })
            .await
    }

    pub async fn select_alternate_interface(
        &self,
        id: &str,
        session_id: &str,
        number: u8,
        alternate: u8,
    ) -> Result<(), UsbError> {
        self.session(id, session_id)?
            .execute(move |state| {
                Box::pin(async move {
                    let metadata = state
                        .configuration()?
                        .interfaces
                        .iter()
                        .find(|i| i.interface_number == number)
                        .and_then(|i| {
                            i.alternates
                                .iter()
                                .find(|a| a.alternate_setting == alternate)
                        })
                        .cloned()
                        .ok_or_else(|| {
                            UsbError::new("invalid", "Unsupported USB alternate setting.")
                        })?;
                    let interface = state
                        .interface
                        .as_ref()
                        .filter(|i| i.interface_number() == number)
                        .ok_or_else(|| {
                            UsbError::new("invalid", "Claim the USB interface first.")
                        })?;
                    state.input = None;
                    state.output = None;
                    state.received = ReadBuffer::default();
                    if interface.get_alt_setting() != alternate {
                        interface.set_alt_setting(alternate).await?;
                    }
                    state
                        .metadata
                        .as_mut()
                        .unwrap()
                        .configuration
                        .as_mut()
                        .unwrap()
                        .interfaces
                        .iter_mut()
                        .find(|i| i.interface_number == number)
                        .unwrap()
                        .alternate = Some(metadata);
                    state.set_endpoints()
                })
            })
            .await
    }

    pub async fn transfer_in(
        &self,
        id: &str,
        session_id: &str,
        endpoint: u8,
        length: usize,
    ) -> Result<Vec<u8>, UsbError> {
        if length == 0 || length > MAX_TRANSFER {
            return Err(UsbError::new("invalid", "Invalid USB read size."));
        }
        self.session(id, session_id)?
            .transfer(move |state| {
                Box::pin(async move {
                    let input = state
                        .input
                        .as_mut()
                        .filter(|ep| {
                            ep.endpoint_address() == endpoint | 0x80
                                && endpoint > 0
                                && endpoint < 16
                        })
                        .ok_or_else(|| {
                            UsbError::new("invalid", "Unsupported USB input endpoint.")
                        })?;
                    if !state.received.0.is_empty() {
                        return Ok(state.received.take(length));
                    }
                    let requested = ReadBuffer::request_length(length, input.max_packet_size())?;
                    input.submit(Buffer::new(requested));
                    let completion = input.next_complete().await;
                    completion.status?;
                    state.received.receive(&completion.buffer, length)
                })
            })
            .await
    }

    pub async fn transfer_out(
        &self,
        id: &str,
        session_id: &str,
        endpoint: u8,
        bytes: Vec<u8>,
    ) -> Result<usize, UsbError> {
        if bytes.len() > MAX_TRANSFER {
            return Err(UsbError::new("invalid", "Invalid USB write size."));
        }
        self.session(id, session_id)?
            .transfer(move |state| {
                Box::pin(async move {
                    let output = state
                        .output
                        .as_mut()
                        .filter(|ep| {
                            ep.endpoint_address() == endpoint && endpoint > 0 && endpoint < 16
                        })
                        .ok_or_else(|| {
                            UsbError::new("invalid", "Unsupported USB output endpoint.")
                        })?;
                    let length = bytes.len();
                    output.submit(bytes.into());
                    let completion = output.next_complete().await;
                    completion.status?;
                    if completion.actual_len != length {
                        return Err(UsbError::new("transport", "Incomplete USB write."));
                    }
                    Ok(length)
                })
            })
            .await
    }

    pub async fn close(&self, id: &str, session_id: &str) -> Result<(), UsbError> {
        let session = {
            let mut registry = self.registry();
            if registry
                .sessions
                .get(session_id)
                .is_some_and(|s| s.device_id == id)
            {
                registry.sessions.remove(session_id)
            } else {
                None
            }
        };
        if let Some(session) = session {
            session.close().await;
        }
        Ok(())
    }
}

impl Drop for UsbManager {
    fn drop(&mut self) {
        self.stop.send_replace(true);
        for session in self.registry().sessions.values() {
            session.stop();
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::atomic::{AtomicBool, Ordering};

    fn manager_without_hardware() -> UsbManager {
        let (events, _) = broadcast::channel(4);
        let (stop, _) = watch::channel(false);
        UsbManager {
            registry: Mutex::new(Registry::default()),
            events,
            stop,
        }
    }

    fn descriptors() -> Vec<u8> {
        let mut bytes = vec![9, 2, 0, 0, 2, 1, 0, 0x80, 50];
        bytes.extend([9, 4, 0, 0, 2, 8, 6, 80, 0]);
        bytes.extend([7, 5, 0x83, 2, 0, 2, 0]);
        bytes.extend([7, 5, 0x04, 2, 0, 2, 0]);
        bytes.extend([9, 4, 1, 0, 2, 255, 66, 1, 0]);
        bytes.extend([7, 5, 0x81, 2, 0, 2, 0]);
        bytes.extend([7, 5, 0x02, 2, 0, 2, 0]);
        let length = (bytes.len() as u16).to_le_bytes();
        bytes[2..4].copy_from_slice(&length);
        bytes
    }

    #[test]
    fn only_complete_adb_interfaces_are_exposed() {
        let mut bytes = descriptors();
        let configuration = configuration_metadata(ConfigurationDescriptor::new(&bytes).unwrap());
        assert_eq!(configuration.interfaces.len(), 1);
        let interface = &configuration.interfaces[0];
        assert_eq!(interface.interface_number, 1);
        assert!(interface.alternate.is_none());
        assert_eq!(interface.alternates[0].endpoints[0].endpoint_number, 1);
        assert_eq!(interface.alternates[0].endpoints[0].direction, "in");
        assert_eq!(interface.alternates[0].endpoints[1].endpoint_number, 2);
        assert_eq!(interface.alternates[0].endpoints[1].direction, "out");
        let output = bytes.len() - 5;
        bytes[output] |= 0x80;
        assert!(
            configuration_metadata(ConfigurationDescriptor::new(&bytes).unwrap())
                .interfaces
                .is_empty()
        );
    }

    #[test]
    fn invalid_bulk_endpoint_descriptors_are_not_exposed() {
        let mut bytes = descriptors();
        let length = bytes.len();
        bytes[length - 3] = 0xff;
        bytes[length - 2] = 3;
        assert!(
            configuration_metadata(ConfigurationDescriptor::new(&bytes).unwrap())
                .interfaces
                .is_empty()
        );
        bytes[length - 3] = 0;
        bytes[length - 2] = 2;
        bytes[length - 5] = 0;
        assert!(
            configuration_metadata(ConfigurationDescriptor::new(&bytes).unwrap())
                .interfaces
                .is_empty()
        );
    }

    struct DropSignal(Arc<AtomicBool>);

    impl Drop for DropSignal {
        fn drop(&mut self) {
            self.0.store(true, Ordering::SeqCst);
        }
    }

    #[tokio::test]
    async fn close_drops_an_inflight_transfer_and_releases_queued_operations() {
        let session = Arc::new(Session::new("device-1".into()));
        let (entered, ready) = tokio::sync::oneshot::channel();
        let dropped = Arc::new(AtomicBool::new(false));
        let pending_session = session.clone();
        let pending_drop = dropped.clone();
        let operation = tokio::spawn(async move {
            pending_session
                .execute(move |_| {
                    Box::pin(async move {
                        let _guard = DropSignal(pending_drop);
                        entered.send(()).unwrap();
                        std::future::pending::<Result<(), UsbError>>().await
                    })
                })
                .await
        });
        ready.await.unwrap();
        let queued_session = session.clone();
        let queued = tokio::spawn(async move {
            queued_session
                .execute(|_| Box::pin(async { panic!("closed queued operation started") }))
                .await as Result<(), UsbError>
        });
        tokio::task::yield_now().await;
        tokio::time::timeout(Duration::from_secs(1), session.close())
            .await
            .unwrap();
        assert_eq!(operation.await.unwrap().unwrap_err().code, "disconnected");
        assert_eq!(queued.await.unwrap().unwrap_err().code, "disconnected");
        assert!(dropped.load(Ordering::SeqCst));
    }

    #[tokio::test]
    async fn a_closed_session_never_starts_another_operation() {
        let session = Session::new("device-1".into());
        session.close().await;
        let result: Result<(), UsbError> = session
            .execute(|_| Box::pin(async { panic!("closed operation started") }))
            .await;
        assert_eq!(result.unwrap_err().code, "disconnected");
    }

    #[tokio::test(start_paused = true)]
    async fn transfers_allow_long_silent_flash_commands_but_remain_cancellable() {
        let session = Arc::new(Session::new("device-1".into()));
        let (entered, ready) = tokio::sync::oneshot::channel();
        let pending_session = session.clone();
        let operation = tokio::spawn(async move {
            pending_session
                .transfer(move |_| {
                    Box::pin(async move {
                        entered.send(()).unwrap();
                        std::future::pending::<Result<(), UsbError>>().await
                    })
                })
                .await
        });
        ready.await.unwrap();
        tokio::time::advance(Duration::from_secs(12)).await;
        tokio::task::yield_now().await;
        assert!(!operation.is_finished());
        session.close().await;
        assert_eq!(operation.await.unwrap().unwrap_err().code, "disconnected");
    }

    #[tokio::test(start_paused = true)]
    async fn the_transfer_fuse_drops_orphaned_io_after_ten_minutes() {
        let session = Arc::new(Session::new("device-1".into()));
        let (entered, ready) = tokio::sync::oneshot::channel();
        let dropped = Arc::new(AtomicBool::new(false));
        let pending_session = session.clone();
        let pending_drop = dropped.clone();
        let operation = tokio::spawn(async move {
            pending_session
                .transfer(move |_| {
                    Box::pin(async move {
                        let _guard = DropSignal(pending_drop);
                        entered.send(()).unwrap();
                        std::future::pending::<Result<(), UsbError>>().await
                    })
                })
                .await
        });
        ready.await.unwrap();
        tokio::time::advance(Duration::from_secs(601)).await;
        assert_eq!(operation.await.unwrap().unwrap_err().code, "timeout");
        assert!(dropped.load(Ordering::SeqCst));
        assert!(*session.cancelled.borrow());
    }

    #[tokio::test]
    async fn stale_close_and_wrong_device_tokens_cannot_close_a_new_session() {
        let manager = manager_without_hardware();
        let session = Arc::new(Session::new("device-2".into()));
        manager
            .registry()
            .sessions
            .insert("session-3".into(), session.clone());
        manager.close("device-2", "session-1").await.unwrap();
        manager.close("device-1", "session-3").await.unwrap();
        assert!(!*session.cancelled.borrow());
        assert!(manager.session("device-2", "session-3").is_ok());
        assert!(manager.session("device-1", "session-3").is_err());
        manager.close("device-2", "session-3").await.unwrap();
        assert!(*session.cancelled.borrow());
        assert!(manager.session("device-2", "session-3").is_err());
        manager.close("device-2", "session-3").await.unwrap();
    }

    #[tokio::test]
    async fn native_bounds_reject_oversized_transfers_before_accessing_usb() {
        let manager = manager_without_hardware();
        assert_eq!(
            manager
                .transfer_in("missing", "missing", 1, 0)
                .await
                .unwrap_err()
                .code,
            "invalid"
        );
        assert_eq!(
            manager
                .transfer_in("missing", "missing", 1, 4097)
                .await
                .unwrap_err()
                .code,
            "invalid"
        );
        assert_eq!(
            manager
                .transfer_out("missing", "missing", 2, vec![0; 4097])
                .await
                .unwrap_err()
                .code,
            "invalid"
        );
        assert_eq!(
            manager
                .transfer_out("missing", "missing", 2, vec![])
                .await
                .unwrap_err()
                .code,
            "disconnected"
        );
    }

    #[test]
    fn generated_ids_do_not_reuse_removed_sessions() {
        let mut registry = Registry::default();
        let device = registry.token("device");
        let first = registry.token("session");
        let session = Arc::new(Session::new(device.clone()));
        registry.sessions.insert(first.clone(), session.clone());
        registry.remove_sessions(&device);
        let second = registry.token("session");
        assert_ne!(first, second);
        assert!(*session.cancelled.borrow());
        assert!(registry.sessions.is_empty());
    }

    #[test]
    fn invalid_packet_sizes_never_expand_native_reads_past_the_limit() {
        for (length, packet) in [(0, 512), (4097, 512), (4096, 1023), (24, 0), (24, 8192)] {
            assert!(
                ReadBuffer::request_length(length, packet).is_err(),
                "{length}/{packet}"
            );
        }
    }

    #[test]
    fn transfer_failures_preserve_actionable_error_categories() {
        assert_eq!(
            UsbError::from(TransferError::Disconnected).code,
            "disconnected"
        );
        assert_eq!(
            UsbError::from(TransferError::InvalidArgument).code,
            "invalid"
        );
        assert_eq!(UsbError::from(TransferError::Cancelled).code, "timeout");
        assert_eq!(UsbError::from(TransferError::Stall).code, "transport");
    }

    #[test]
    fn packet_aligned_reads_preserve_every_surplus_byte() {
        assert_eq!(ReadBuffer::request_length(24, 512).unwrap(), 512);
        assert_eq!(ReadBuffer::request_length(4096, 512).unwrap(), 4096);
        let mut buffer = ReadBuffer::default();
        let bytes: Vec<u8> = (0..128).collect();
        let mut received = buffer.receive(&bytes, 24).unwrap();
        received.extend(buffer.take(90));
        received.extend(buffer.take(24));
        assert_eq!(received, bytes);
        assert!(buffer.take(1).is_empty());
    }
}
