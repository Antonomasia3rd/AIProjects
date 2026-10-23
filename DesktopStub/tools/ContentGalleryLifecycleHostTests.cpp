// Keep gallery recovery/idle work within its own unchanged 45-second watchdog.
// The complementary revision fixture covers Prayer, instances and Registration.
#define DESKTOPSTUB_REVISION_HOST_ONLY
#define DESKTOPSTUB_GALLERY_LIFECYCLE_HOST_ONLY
#include "ContentRuntimeTests.cpp"
