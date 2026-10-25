#define NOMINMAX
#include "../dependencies/content_sources/battery_win32.inc"
// Native compile/link witness. This function is never executed against hardware.
void CompileBatteryNative(const aip::battery::native::CollectOptions& options){
    const auto result=aip::battery::native::Collect(options);(void)result;
}
// The link fixture deliberately retains CompileBatteryNative with /OPT:NOREF,
// but its entry point never calls the collector or any hardware API.
int main(){return 0;}
