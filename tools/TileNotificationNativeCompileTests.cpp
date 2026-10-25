#include "../dependencies/tile_notification_winrt.inc"
// Compile-only native witness. No notification API is executed by a fixture.
void CompileNumericTileBadge(const std::wstring& transport){std::wstring tileError,badgeError;aip::tile_notification::DeliverWinrt(transport,tileError,&badgeError);}
