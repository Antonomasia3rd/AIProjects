#include "ga_tray_helpers.inc"
static bool RequestDesktopHostSetup(HWND owner);
static void CompleteDesktopHostSetup(HWND owner);
static bool RecoverInterruptedDesktopHostSetup();
#include "ga_content_tray.inc"
#include "ga_smtc_settings_tray.inc"
#include "ga_secondary_tile_tray.inc"
#include "ga_quick_actions.inc"
#include "ga_gallery_tray.inc"
#include "ga_tile_instance_tray.inc"
#include "ga_desktop_host_setup.inc"
#include "ga_menu_wording.inc"
#include "ga_tray_menu.inc"
#include "ga_tray_dispatch.inc"
