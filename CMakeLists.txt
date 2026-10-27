cmake_minimum_required(VERSION 3.21)
project(Screenshot VERSION 0.7.5 LANGUAGES CXX RC)
add_executable(Screenshot WIN32 src/main.cpp src/settings.cpp assets/screenshot.rc)
target_compile_features(Screenshot PRIVATE cxx_std_17)
target_compile_definitions(Screenshot PRIVATE UNICODE _UNICODE _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_link_libraries(Screenshot PRIVATE user32 gdi32 d2d1 dwrite dwmapi windowscodecs ole32 advapi32 shell32)
# The custom setup executable embeds Screenshot.exe directly. No nested installer.
file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/assets/Screenshot.ico" APP_ICON_PATH)
file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/assets/Screenshot.png" APP_MASTER_PATH)
file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/Montserrat.ttf" APP_FONT_PATH)
file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/installer-preview/assets/ribbon.png" SETUP_RIBBON_PATH)
file(TO_CMAKE_PATH "${CMAKE_CURRENT_BINARY_DIR}/Release/Screenshot.exe" PAYLOAD_PATH)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/setup_payload.rc" "101 ICON \"${APP_ICON_PATH}\"\n201 RCDATA \"${APP_MASTER_PATH}\"\n202 RCDATA \"${APP_FONT_PATH}\"\n203 RCDATA \"${SETUP_RIBBON_PATH}\"\n301 RCDATA \"${PAYLOAD_PATH}\"\n")
include(FetchContent)
FetchContent_Declare(webview2 URL https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/1.0.4258.31/microsoft.web.webview2.1.0.4258.31.nupkg URL_HASH SHA256=56f7f4b8bf9aee4b8efefbbdd4f67d5f74ebd1b100ed0806da71bf76af481aa9 DOWNLOAD_NAME webview2.zip)
FetchContent_MakeAvailable(webview2)
set(resource_id 203)
foreach(resource IN ITEMS index.html connect.css screenshot.css setup.js)
 math(EXPR resource_id "${resource_id}+1")
 file(APPEND "${CMAKE_CURRENT_BINARY_DIR}/setup_payload.rc" "${resource_id} RCDATA \"${CMAKE_CURRENT_SOURCE_DIR}/installer-web/${resource}\"\n")
endforeach()
add_executable(ScreenshotSetup WIN32 src/setup.cpp "${CMAKE_CURRENT_BINARY_DIR}/setup_payload.rc")
add_dependencies(ScreenshotSetup Screenshot)
target_compile_features(ScreenshotSetup PRIVATE cxx_std_17)
target_compile_definitions(ScreenshotSetup PRIVATE UNICODE _UNICODE _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_link_libraries(ScreenshotSetup PRIVATE d2d1 dwrite user32 gdi32 shell32 ole32 dwmapi advapi32 windowscodecs)
target_include_directories(ScreenshotSetup PRIVATE "${webview2_SOURCE_DIR}/build/native/include")
target_link_libraries(ScreenshotSetup PRIVATE "${webview2_SOURCE_DIR}/build/native/x64/WebView2LoaderStatic.lib" shlwapi version)
set_target_properties(ScreenshotSetup PROPERTIES OUTPUT_NAME "Screenshot-Setup")
if(MSVC)
    foreach(target IN ITEMS Screenshot ScreenshotSetup)
        target_compile_options(${target} PRIVATE /W4 /EHsc /O2 /utf-8)
        target_link_options(${target} PRIVATE /OPT:REF /OPT:ICF)
    endforeach()
endif()








