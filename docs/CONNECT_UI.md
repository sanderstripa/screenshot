# Screenshot 0.8.0 — Connect / Soulu

The main control window is 460 × 636 logical pixels. It contains shortcut assignment, the existing screenshot folder action, appearance, language and About. There is no separate preferences page, back button, gear or Save action. The embedded Montserrat font and capture engine are unchanged.

## Palette source

Colors and caption geometry come from `sanderstripa/torrent`, `src/visual.h` and `drawSettings()` in `src/main.cpp`, revision `2189073eb8d90a5594c56bb80c71ae59c356689d`.

| Token | Light | Dark |
| --- | --- | --- |
| Main window | #FFFFFF | #08090B |
| Text | #242628 | #EEEEEF |
| Secondary text | #73767B | #96979D |
| Settings field | #F3F4F5 | #28292D |
| Field outline | #E6E7E9 | #35363B |
| Confirmation sheet / selected segment | #FCFCFD | #202124 |
| Close hover | #F6D1D9 | #54242F |

Caption controls use a 32 px circular hover area and 1.25 px strokes. Only minimize and close are exposed. The shortcut field and controls use 36 px height and 8 px corners. Language is an inline dropdown. Theme and language save on selection; System mode follows Windows theme notifications.

## Hotkey transaction

The shortcut field displays the committed runtime value. A candidate is registered with the existing application callback before preferences are committed. Failed registration, persistence errors and canceled consent retain the previous shortcut. On confirmed Print Screen failure the prior Windows Snipping preference is restored as well. An unavailable configured shortcut is explicitly labeled as unavailable.

Application-owned Print Screen consent attaches to the top edge and animates in/out over a three-pass blurred, tinted snapshot. Windows animation preferences are respected. Required system permissions remain under Windows control; consent alone never counts as successful hotkey registration.

## About and links

Version comes from the running EXE VERSIONINFO resource. Update opens the project releases. The small centered `by connect.` link opens https://connect.sanderstripa.com/.

## Installation and verification

The pinned shared installer receives a Screenshot-only launch correction during its build. Its Finish action uses `--settings`, the installation working directory and Explorer ShellExecute to retain the ordinary user context. It checks that the main window became visible before reporting success. If the desktop Shell accepts the command but does not open the window, a native launch with the exact executable and working directory retries through the existing single-instance IPC. Login autostart still uses `--background`.

Windows CI builds the native application, checks PNG output and clipboard, installs/updates/removes through the shared installer adapter, exercises its actual Finish launch method with cold and existing workers, verifies icon resources and captures both themes and Print Screen consent. The UI regression checks live registration, conflicting bindings, cancellation, failure after consent, automatic persistence and full process restart. Tests restore the registry preferences they modify.
