#pragma once
#include <windows.h>
#include <functional>
constexpr UINT WM_SCREENSHOT_SETTINGS = WM_APP + 42;
void showScreenshotSettings(HINSTANCE instance, UINT vk, UINT modifiers,
    bool shortcutAvailable, std::function<bool(UINT, UINT)> apply, int previewTheme=-1);
void closeScreenshotSettings();
bool screenshotSettingsFocused();
