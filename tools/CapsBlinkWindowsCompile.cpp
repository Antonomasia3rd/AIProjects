// Syntax-only adapter check. Never execute WindowsBackend or keyboard APIs.
#include "../dependencies/hardware/caps_blink_windows.inc"
static_assert(sizeof(aip::caps::Indicators) == 4, "indicator ABI");
