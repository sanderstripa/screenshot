# Screenshot 0.7.2

- Единый установщик CONNECT Installer на Go/Wails; общий исходник хранится в `sanderstripa/connect/installer-core`.
- Кнопка «Принять и установить» находится на том же уровне, что в VPN, в обеих темах и на обоих языках.
- Сохранены установочные адаптеры, настройки и особенности продукта из проверенных пользователем локальных сборок.
- Изменения интерфейса и функциональности самого приложения не вносились, кроме номера версии.

Пользователь подтвердил установку всех пяти предыдущих локальных сборок. Автоматические проверки текущей сборки указаны в GitHub Actions; повторная ручная установка каждого финального SHA не заявляется.

# Screenshot v0.7.1

The installer now renders the original CONNECT installer HTML/CSS components in WebView2, using the same bundled Montserrat font. Language/theme controls, folder input, Browse button, switches, borders and colors use the CONNECT definitions directly. The application UI and capture engine are unchanged.

The settings action and final title/Done/launch switch follow CONNECT's vertical alignment. The folder field keeps its DOM node, selection and geometry during state updates. Installer theme/language choices continue to transfer to the application.

Windows checks cover payload validity, installation/update over a running version, autostart, shortcuts, uninstall, PNG output/clipboard, actual WebView2 component parity, hotkey setup and launch with inherited appearance.

Stable release reuses the exact Windows-tested v0.7.1-rc.3 installer. SHA-256: `8866e42d1cc22552b8b1674f626ce90ee0be4bf2161a696c0933a1eb6d8edcb4`.
