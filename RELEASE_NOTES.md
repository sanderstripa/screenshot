# Screenshot v0.7.1

The installer now renders the original CONNECT installer HTML/CSS components in WebView2, using the same bundled Montserrat font. Language/theme controls, folder input, Browse button, switches, borders and colors use the CONNECT definitions directly. The application UI and capture engine are unchanged.

The settings action and final title/Done/launch switch follow CONNECT's vertical alignment. The folder field keeps its DOM node, selection and geometry during state updates. Installer theme/language choices continue to transfer to the application.

Windows checks cover payload validity, installation/update over a running version, autostart, shortcuts, uninstall, PNG output/clipboard, actual WebView2 component parity, hotkey setup and launch with inherited appearance.
