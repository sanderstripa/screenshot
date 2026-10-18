# Screenshot v0.6.5 — remember window position

- The application remembers where you move its settings window and restores that position on the next launch, including after a full process restart.
- Moving saves immediately; minimizing does not overwrite the normal position.
- If a saved monitor is unavailable, the window returns to a visible work area. Window dimensions and current monitor DPI remain respected.
- No appearance, capture, shortcut or installer behavior changed.

Verified on Windows: move persistence, close/reopen, full worker restart after minimize, and recovery from an unavailable monitor position. Installation/update and PNG checks passed in Windows CI. A physical PC reboot was not performed.

Tested installer SHA-256: fc313115396dd9299aec4fac0a3560bc2a3acf9a391a9a96a03b376758933dda.