#ifndef AIP_TILE_NOTIFICATION_WINRT_INC
#define AIP_TILE_NOTIFICATION_WINRT_INC
#include "tile_notification_payload.h"
#include <winrt/Windows.Data.Xml.Dom.h>
#include <winrt/Windows.UI.Notifications.h>

namespace aip { namespace tile_notification {
// The caller owns its existing WinRT apartment and exception reporting policy.
inline bool DeliverWinrt(const std::wstring& transport, std::wstring& error)
{
    using namespace winrt::Windows::Data::Xml::Dom;
    using namespace winrt::Windows::UI::Notifications;
    return Deliver(transport,
        [](const std::wstring& text) { XmlDocument xml; xml.LoadXml(winrt::hstring(text)); return xml; },
        [](const XmlDocument& xml) { return TileNotification(xml); },
        [](TileNotification& notification, const std::wstring& tag) { notification.Tag(winrt::hstring(tag)); },
        [](const std::wstring& target) {
            return target.empty() ? TileUpdateManager::CreateTileUpdaterForApplication()
                : TileUpdateManager::CreateTileUpdaterForSecondaryTile(winrt::hstring(target));
        }, error);
}
} }
#endif
