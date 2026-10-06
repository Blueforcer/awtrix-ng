#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use awtrix_installer::service::{guarded, setup_url, Error, Reply, Service};
use serde_json::{json, Value};
use std::sync::{
    atomic::{AtomicBool, Ordering},
    Arc,
};
use tauri::{
    ipc::{InvokeBody, Request, Response},
    Emitter, Manager, State, WebviewWindow,
};
use tauri_plugin_dialog::{
    DialogExt, MessageDialogButtons, MessageDialogKind, MessageDialogResult,
};

fn trusted(window: &WebviewWindow) -> Result<(), Error> {
    let url = window
        .url()
        .map_err(|_| Error::new("denied", "Unknown installer window."))?;
    if window.label() != "main" || !local_entry(&url) {
        return Err(Error::new("denied", "Untrusted installer request."));
    }
    Ok(())
}

fn local_entry(url: &tauri::Url) -> bool {
    url.username().is_empty()
        && url.password().is_none()
        && url.port().is_none()
        && url.path() == "/ui/index.html"
        && url.query().is_none()
        && url.fragment().is_none()
        && matches!(
            (url.scheme(), url.host_str()),
            ("tauri", Some("localhost"))
                | ("http", Some("tauri.localhost"))
                | ("https", Some("tauri.localhost"))
        )
}

async fn call(
    window: &WebviewWindow,
    service: &Service,
    command: &str,
    args: Value,
    bytes: Vec<u8>,
) -> Result<Response, Error> {
    trusted(window)?;
    match service.call(command, args, bytes).await? {
        Reply::Bytes(bytes) => Ok(Response::new(bytes)),
        Reply::Json(value) => Ok(Response::new(value.to_string())),
    }
}

#[tauri::command]
async fn firmware(
    window: WebviewWindow,
    state: State<'_, Service>,
    request_id: Option<String>,
) -> Result<Response, Error> {
    call(
        &window,
        &state,
        "firmware",
        json!({"requestId":request_id}),
        vec![],
    )
    .await
}
#[tauri::command]
async fn choose_firmware(window: WebviewWindow, state: State<'_, Service>) -> Result<bool, Error> {
    trusted(&window)?;
    state
        .choose_firmware(async {
            let (send, receive) = tokio::sync::oneshot::channel();
            window
                .dialog()
                .file()
                .set_parent(&window)
                .set_title("Select AWTRIX TC002 firmware ZIP")
                .add_filter("AWTRIX firmware ZIP", &["zip"])
                .pick_file(move |selection| {
                    let _ = send.send(selection);
                });
            match receive
                .await
                .map_err(|_| Error::new("dialog", "File selection closed."))?
            {
                None => Ok(None),
                Some(tauri_plugin_dialog::FilePath::Path(path)) => Ok(Some(path)),
                Some(_) => Err(Error::new("invalid", "Select a local firmware ZIP.")),
            }
        })
        .await
}
#[tauri::command]
async fn phase(
    window: WebviewWindow,
    state: State<'_, Service>,
    value: String,
) -> Result<Response, Error> {
    call(&window, &state, "phase", json!({"value":value}), vec![]).await
}
#[tauri::command]
async fn usb_list(window: WebviewWindow, state: State<'_, Service>) -> Result<Response, Error> {
    call(&window, &state, "usb_list", Value::Null, vec![]).await
}
#[tauri::command]
async fn usb_open(
    window: WebviewWindow,
    state: State<'_, Service>,
    id: String,
) -> Result<Response, Error> {
    call(&window, &state, "usb_open", json!({"id":id}), vec![]).await
}
#[tauri::command]
async fn usb_close(
    window: WebviewWindow,
    state: State<'_, Service>,
    id: String,
    session_id: String,
) -> Result<Response, Error> {
    call(
        &window,
        &state,
        "usb_close",
        json!({"id":id,"sessionId":session_id}),
        vec![],
    )
    .await
}
#[tauri::command]
async fn usb_select_configuration(
    window: WebviewWindow,
    state: State<'_, Service>,
    id: String,
    session_id: String,
    value: u8,
) -> Result<Response, Error> {
    call(
        &window,
        &state,
        "usb_select_configuration",
        json!({"id":id,"sessionId":session_id,"value":value}),
        vec![],
    )
    .await
}
#[tauri::command]
async fn usb_claim_interface(
    window: WebviewWindow,
    state: State<'_, Service>,
    id: String,
    session_id: String,
    number: u8,
) -> Result<Response, Error> {
    call(
        &window,
        &state,
        "usb_claim_interface",
        json!({"id":id,"sessionId":session_id,"number":number}),
        vec![],
    )
    .await
}
#[tauri::command]
async fn usb_select_alternate_interface(
    window: WebviewWindow,
    state: State<'_, Service>,
    id: String,
    session_id: String,
    number: u8,
    alternate: u8,
) -> Result<Response, Error> {
    call(
        &window,
        &state,
        "usb_select_alternate_interface",
        json!({"id":id,"sessionId":session_id,"number":number,"alternate":alternate}),
        vec![],
    )
    .await
}
#[tauri::command]
async fn usb_transfer_in(
    window: WebviewWindow,
    state: State<'_, Service>,
    id: String,
    session_id: String,
    endpoint: u8,
    length: usize,
) -> Result<Response, Error> {
    call(
        &window,
        &state,
        "usb_transfer_in",
        json!({"id":id,"sessionId":session_id,"endpoint":endpoint,"length":length}),
        vec![],
    )
    .await
}
#[tauri::command]
async fn usb_transfer_out(
    window: WebviewWindow,
    state: State<'_, Service>,
    request: Request<'_>,
) -> Result<Response, Error> {
    trusted(&window)?;
    let header = |name: &str| {
        request
            .headers()
            .get(name)
            .and_then(|value| value.to_str().ok())
            .filter(|value| value.len() <= 128)
            .ok_or_else(|| Error::new("invalid", "Missing USB request header."))
    };
    let id = header("x-device-id")?;
    let session_id = header("x-session-id")?;
    let endpoint: u8 = header("x-endpoint")?
        .parse()
        .map_err(|_| Error::new("invalid", "Invalid USB endpoint."))?;
    let InvokeBody::Raw(bytes) = request.body() else {
        return Err(Error::new("invalid", "USB data must be binary."));
    };
    if bytes.len() > 4096 {
        return Err(Error::new("invalid", "USB transfer is too large."));
    }
    call(
        &window,
        &state,
        "usb_transfer_out",
        json!({"id":id,"sessionId":session_id,"endpoint":endpoint}),
        bytes.clone(),
    )
    .await
}

#[tauri::command]
async fn open_setup(
    window: WebviewWindow,
    state: State<'_, Service>,
    ip: String,
) -> Result<(), Error> {
    trusted(&window)?;
    if state.current_phase() != "done" {
        return Err(Error::new(
            "invalid",
            "Finish installation before opening setup.",
        ));
    }
    open::that_detached(setup_url(&ip)?)
        .map_err(|_| Error::new("open", "Open the clock's address in your browser."))
}

#[cfg(feature = "qualification")]
#[tauri::command]
fn qualification(
    window: WebviewWindow,
    action: String,
    report: Option<Value>,
) -> Result<Response, Error> {
    trusted(&window)?;
    match awtrix_installer::qualification::command(&action, report)? {
        Reply::Bytes(bytes) => Ok(Response::new(bytes)),
        Reply::Json(value) => Ok(Response::new(value.to_string())),
    }
}

fn main() {
    let exiting = Arc::new(AtomicBool::new(false));
    let prompting = Arc::new(AtomicBool::new(false));
    tauri::Builder::default()
        .plugin(tauri_plugin_single_instance::init(|app, _, _| {
            if let Some(window) = app.get_webview_window("main") { let _ = window.unminimize(); let _ = window.show(); let _ = window.set_focus(); }
        }))
        .plugin(tauri_plugin_dialog::init())
        .invoke_handler(tauri::generate_handler![firmware, choose_firmware, phase, open_setup, usb_list, usb_open, usb_close,
            usb_select_configuration, usb_claim_interface, usb_select_alternate_interface, usb_transfer_in, usb_transfer_out,
            #[cfg(feature = "qualification")] qualification])
        .setup(|app| {
            let handle = app.handle().clone();
            app.manage(Service::new(Arc::new(move |event, payload| { let _ = handle.emit_to("main", event, payload); })));
            let config = app.config().app.windows.first().ok_or("Missing installer window")?;
            let window = tauri::WebviewWindowBuilder::from_config(app, config)?
                .incognito(true)
                .on_navigation(local_entry)
                .on_new_window(|_, _| tauri::webview::NewWindowResponse::Deny);
            #[cfg(feature = "qualification")]
            let window = window.initialization_script(include_str!("../../test/tauri-smoke.js"));
            window.build()?;
            Ok(())
        })
        .on_window_event(move |window, event| {
            if let tauri::WindowEvent::CloseRequested {api, ..} = event {
                let service = window.state::<Service>();
                if !exiting.load(Ordering::Relaxed) && guarded(&service.current_phase()) {
                    api.prevent_close();
                    if prompting.swap(true, Ordering::Relaxed) { return; }
                    let window = window.clone();
                    let exiting = exiting.clone();
                    let prompting = prompting.clone();
                    window.dialog().message("Keep the clock powered and this installer open. Closing now can leave the installation incomplete. You can retry in this window.")
                        .title("Installation is still in progress")
                        .kind(MessageDialogKind::Warning)
                        .buttons(MessageDialogButtons::YesNoCancelCustom("Keep installer open".into(), "Quit anyway".into(), "Cancel".into()))
                        .show_with_result(move |answer| {
                            prompting.store(false, Ordering::Relaxed);
                            if answer == MessageDialogResult::No || answer == MessageDialogResult::Custom("Quit anyway".into()) {
                                exiting.store(true, Ordering::Relaxed); let _ = window.close();
                            }
                        });
                }
            }
        })
        .run(tauri::generate_context!())
        .expect("AWTRIX NG installer could not start");
}
