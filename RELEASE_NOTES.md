# Screenshot v0.6.10 — remove installer button outer outline

Removed the external focus outline from installer primary and secondary action buttons, including Continue. Pointer hover/press feedback and keyboard activation remain functional. No application or installation behavior changed.

Verified native Windows rendering in light/dark themes on welcome, installed, hotkey, Print Screen confirmation and ready stages, including keyboard-focused actions. Pixels outside primary and secondary button bounds match the background. Windows CI installation/update/uninstall and PNG checks pass.

Exact tested installer SHA-256: a3297d8dc5a32dbe5ed06f5923dca968ec8dbb21db5ff5e73f358141b4b39eda. Stable release reuses the tested v0.6.10-rc.1 bytes.