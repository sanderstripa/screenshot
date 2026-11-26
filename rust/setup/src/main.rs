#![windows_subsystem = "windows"]

mod install;

use install::{Installer, PROGRESS, VERSION, wide};
use serde_json::json;
use std::{
    borrow::Cow,
    sync::{
        Arc, Mutex,
        atomic::{AtomicBool, Ordering},
    },
};
use tao::{
    dpi::LogicalSize,
    event::{Event, WindowEvent},
    event_loop::{ControlFlow, EventLoopBuilder},
    window::{Icon, WindowBuilder},
};
use windows::Win32::{System::Com::*, UI::Shell::*};
use windows_sys::Win32::{
    Foundation::{CloseHandle, ERROR_ALREADY_EXISTS, GetLastError},
    System::Threading::CreateMutexW,
};
use wry::{
    WebViewBuilder, WebViewBuilderExtWindows,
    http::{Response, StatusCode},
};

enum Message {
    Close,
    Minimize,
    Drag,
    Folder(wry::RequestAsyncResponder),
}

fn main() {
    let lock = unsafe {
        CreateMutexW(
            std::ptr::null(),
            0,
            wide(r"Local\com.sanderstripa.installer.screenshot").as_ptr(),
        )
    };
    if lock.is_null() {
        return;
    }
    if unsafe { GetLastError() } == ERROR_ALREADY_EXISTS {
        unsafe {
            CloseHandle(lock);
        }
        return;
    }
    if let Err(error) = run() {
        unsafe {
            windows_sys::Win32::UI::WindowsAndMessaging::MessageBoxW(
                std::ptr::null_mut(),
                wide(error.to_string()).as_ptr(),
                wide("Screenshot").as_ptr(),
                0x10,
            );
        }
    }
    unsafe {
        CloseHandle(lock);
    }
}

fn run() -> Result<(), Box<dyn std::error::Error>> {
    let uninstall = std::env::args_os().any(|arg| arg == "--uninstall");
    let installer = Installer::new(uninstall);
    let config = json!({
        "id": "screenshot", "name": "Screenshot", "version": VERSION,
        "chrome": false, "postInstall": "hotkey", "path": installer.directory,
        "repository": "https://github.com/sanderstripa/screenshot",
        "installScope": "user", "engine": "Rust", "ribbonBounds": [119,48,1459,982]
    })
    .to_string()
    .replace('<', "\\u003c");
    let state = Arc::new(Mutex::new(installer));
    let events = EventLoopBuilder::<Message>::with_user_event().build();
    let window = WindowBuilder::new()
        .with_title("Screenshot")
        .with_decorations(false)
        .with_resizable(false)
        .with_inner_size(LogicalSize::new(840.0, 540.0))
        .with_window_icon(icon()?)
        .build(&events)?;
    if let Some(monitor) = window.current_monitor() {
        let origin = monitor.position();
        let size = monitor.size();
        let bounds = window.outer_size();
        window.set_outer_position(tao::dpi::PhysicalPosition::new(
            origin.x + (size.width as i32 - bounds.width as i32) / 2,
            origin.y + (size.height as i32 - bounds.height as i32) / 2,
        ));
    }
    let proxy = events.create_proxy();
    let ipc = proxy.clone();
    let backend = Arc::clone(&state);
    let busy = Arc::new(AtomicBool::new(false));
    let requests = Arc::clone(&busy);
    let page = include_str!("../payload/web/index.html")
        .replace("__MODE__", if uninstall { "true" } else { "false" })
        .replace("__CONFIG__", &config)
        .replace("__TOKEN__", "screenshot-setup");
    let mut context =
        wry::WebContext::new(std::env::var_os("WEBVIEW2_USER_DATA_FOLDER").map(Into::into));
    let web = WebViewBuilder::with_web_context(&mut context)
        .with_background_color((242, 240, 235, 255))
        .with_additional_browser_args(std::env::var("WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS").unwrap_or_default())
        .with_navigation_handler(|url| url.starts_with("http://setup.localhost/") || url.starts_with("setup://localhost/"))
        .with_initialization_script(r#"
           window.wails={Window:{Minimise:()=>window.ipc.postMessage('minimize')}};
           window.addEventListener('pointerdown',event=>{
             if(event.button===0 && !event.target.closest('button,input,select,textarea,label,.tools'))
               window.ipc.postMessage('drag');
           });
        "#)
        .with_ipc_handler(move |request| {
            let message = match request.body().as_str() {
                "minimize" => Message::Minimize,
                "drag" => Message::Drag,
                _ => return,
            };
            let _ = ipc.send_event(message);
        })
        .with_asynchronous_custom_protocol("setup".into(), move |_id, request, responder| {
            let path = request.uri().path();
            if !path.starts_with("/api/") {
                let data: (&str, &[u8]) = match path {
                    "/" => ("text/html; charset=utf-8", page.as_bytes()),
                    "/style.css" => ("text/css", include_bytes!("../payload/web/style.css")),
                    "/setup.js" => ("application/javascript", include_bytes!("../payload/web/setup.js")),
                    "/stages.js" => ("application/javascript", include_bytes!("../payload/web/stages.js")),
                    "/wails/runtime.js" => ("application/javascript", b""),
                    "/assets/mobius.png" => ("image/png", include_bytes!("../payload/web/assets/mobius.png")),
                    "/assets/icon.png" => ("image/png", include_bytes!("../../../assets/Screenshot-installer.png")),
                    "/assets/Montserrat.ttf" => ("font/ttf", include_bytes!("../payload/web/assets/Montserrat.ttf")),
                    _ => { responder.respond(response(StatusCode::NOT_FOUND, "text/plain", b"not found".to_vec())); return; }
                };
                responder.respond(response(StatusCode::OK, data.0, data.1.to_vec()));
                return;
            }
            let action = &path[5..];
            if request.headers().get("X-Setup-Token").and_then(|v| v.to_str().ok()) != Some("screenshot-setup")
                || !(request.method() == "POST" || (request.method() == "GET" && action == "progress")) {
                responder.respond(response(StatusCode::FORBIDDEN, "text/plain", b"forbidden".to_vec()));
                return;
            }
            if action == "progress" {
                responder.respond(reply(Ok(json!({"progress": PROGRESS.load(Ordering::Relaxed)}))));
                return;
            }
            if requests.swap(true, Ordering::Acquire) {
                responder.respond(response(StatusCode::CONFLICT, "application/json", br#"{"error":"operation in progress"}"#.to_vec()));
                return;
            }
            match action {
                "folder" if !uninstall => { let _ = proxy.send_event(Message::Folder(responder)); }
                "close" => {
                    responder.respond(reply(Ok(json!({"ok":true}))));
                    requests.store(false, Ordering::Release);
                    let _ = proxy.send_event(Message::Close);
                }
                _ => {
                    let (backend, requests, action, data) = (Arc::clone(&backend), Arc::clone(&requests), action.to_owned(), request.body().clone());
                    std::thread::spawn(move || {
                        let result = with_com(|| backend.lock().map_err(|e| e.to_string())?
                            .dispatch(&action, &data).map_err(|e| e.to_string()));
                        requests.store(false, Ordering::Release);
                        responder.respond(reply(result));
                    });
                }
            }
        })
        .with_url("setup://localhost/")
        .build(&window)?;
    events.run(move |event, _, flow| {
        *flow = ControlFlow::Wait;
        match event {
            Event::WindowEvent {
                event: WindowEvent::CloseRequested,
                ..
            }
            | Event::UserEvent(Message::Close) => {
                if !busy.load(Ordering::Acquire) {
                    if let Ok(state) = state.lock() {
                        if state.removed {
                            let _ = state.cleanup();
                        }
                    }
                    *flow = ControlFlow::Exit;
                }
            }
            Event::UserEvent(Message::Minimize) => window.set_minimized(true),
            Event::UserEvent(Message::Drag) => {
                let _ = window.drag_window();
            }
            Event::UserEvent(Message::Folder(responder)) => {
                use tao::platform::windows::WindowExtWindows;
                let result = choose_folder(window.hwnd()).map_err(|e| e.to_string());
                busy.store(false, Ordering::Release);
                responder.respond(reply(result));
            }
            _ => {}
        }
        let _ = &web;
    });
}

fn response(status: StatusCode, mime: &str, body: Vec<u8>) -> Response<Cow<'static, [u8]>> {
    Response::builder()
        .status(status)
        .header("Content-Type", mime)
        .header("Cache-Control", "no-store")
        .header("X-Content-Type-Options", "nosniff")
        .body(Cow::Owned(body))
        .expect("valid response")
}

fn reply(result: Result<serde_json::Value, String>) -> Response<Cow<'static, [u8]>> {
    let (code, value) = match result {
        Ok(value) => (StatusCode::OK, value),
        Err(error) => (StatusCode::INTERNAL_SERVER_ERROR, json!({"error":error})),
    };
    response(code, "application/json", value.to_string().into_bytes())
}

fn with_com<T>(operation: impl FnOnce() -> Result<T, String>) -> Result<T, String> {
    unsafe {
        CoInitializeEx(None, COINIT_APARTMENTTHREADED)
            .ok()
            .map_err(|error| error.to_string())?;
    }
    let result = operation();
    unsafe {
        CoUninitialize();
    }
    result
}

fn choose_folder(hwnd: isize) -> windows::core::Result<serde_json::Value> {
    unsafe {
        let dialog: IFileOpenDialog =
            CoCreateInstance(&FileOpenDialog, None, CLSCTX_INPROC_SERVER)?;
        dialog.SetOptions(dialog.GetOptions()? | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM)?;
        if let Err(error) = dialog.Show(windows::Win32::Foundation::HWND(hwnd as _)) {
            if error.code().0 as u32 == 0x800704c7 {
                return Ok(json!({"path":"","cancelled":true}));
            }
            return Err(error);
        }
        let name = dialog.GetResult()?.GetDisplayName(SIGDN_FILESYSPATH)?;
        let value = name.to_string();
        CoTaskMemFree(Some(name.0.cast()));
        Ok(json!({"path":value?,"cancelled":false}))
    }
}

fn icon() -> Result<Option<Icon>, Box<dyn std::error::Error>> {
    let source = include_bytes!("../../../assets/Screenshot-installer.png");
    let image = image::load_from_memory(source)?
        .resize_exact(256, 256, image::imageops::FilterType::Lanczos3)
        .into_rgba8();
    Ok(Some(Icon::from_rgba(image.into_raw(), 256, 256)?))
}
