#include "ga_tray_helpers.inc"
static constexpr UINT ID_DESKTOPSTUB_SETTINGS_WINDOW=64900;
static bool OpenDesktopStubSettings(HWND owner);
static bool RequestDesktopHostSetup(HWND owner);
static void CompleteDesktopHostSetup(HWND owner);
static bool RecoverInterruptedDesktopHostSetup();
#include "ga_content_tray.inc"
#include "ga_smtc_settings_tray.inc"
#include "ga_secondary_tile_tray.inc"
#include "ga_quick_actions.inc"
#include "ga_gallery_tray.inc"
#include "ga_tile_instance_tray.inc"
#include "ga_kuro_signin.inc"
#include "ga_google_calendar_setup.inc"
#include "ga_gmail_setup.inc"
#include "ga_discord_reader_ui.inc"
#include "ga_desktop_host_setup.inc"
#include "ga_menu_wording.inc"
#include "ga_properties_options.inc"
#include "ga_tray_menu.inc"
#include "ga_tray_dispatch.inc"
#include "ga_settings_window.inc"
