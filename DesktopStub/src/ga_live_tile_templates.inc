// Windows 8/8.1 preset-template catalog support for Windows 10 notifications.
static void AppendPresetLiveTileText(std::wstring& xml, int id, const std::wstring& text)
{
    xml += L"      <text id=\"";
    xml += IntToWString(id);
    xml += L"\">";
    xml += XmlEscape(text);
    xml += L"</text>\r\n";
}

static void AppendPresetLiveTileImage(std::wstring& xml, const std::wstring& imageUri)
{
    xml += L"      <image id=\"1\" src=\"";
    xml += XmlEscape(imageUri);
    xml += L"\"/>\r\n";
}

static void AppendPresetLiveTileBindingStart(
    std::wstring& xml,
    const wchar_t* templateName,
    const wchar_t* fallbackName = nullptr)
{
    xml += L"    <binding template=\"";
    xml += templateName;
    xml += L"\"";
    if (fallbackName && *fallbackName)
    {
        xml += L" fallback=\"";
        xml += fallbackName;
        xml += L"\"";
    }
    xml += L">\r\n";
}

static bool AppendWindows81PresetLiveTileBinding(
    std::wstring& xml,
    const wchar_t* bindingName,
    const wchar_t* assetName,
    const std::wstring& imageUri)
{
    std::wstring primaryText;
    std::wstring secondaryText;
    std::wstring badgeText;
    if (TileTextAppliesToTileName(assetName))
    {
        primaryText = TileTextXmlText();
        secondaryText = TileTextXmlSecondaryText();
        badgeText = TileTextXmlBadgeText();
    }
    if (primaryText.empty() && !secondaryText.empty())
    {
        primaryText = secondaryText;
        secondaryText.clear();
    }

    bool hasText = !primaryText.empty() || !secondaryText.empty() || !badgeText.empty();
    if (IEquals(bindingName ? bindingName : L"", L"TileMedium"))
    {
        if (!badgeText.empty())
        {
            AppendPresetLiveTileBindingStart(
                xml,
                L"TileSquare150x150Block",
                L"TileSquareBlock");
            AppendPresetLiveTileText(xml, 1, badgeText);
            AppendPresetLiveTileText(xml, 2, primaryText);
        }
        else if (hasText)
        {
            AppendPresetLiveTileBindingStart(
                xml,
                L"TileSquare150x150Text02",
                L"TileSquareText02");
            AppendPresetLiveTileText(xml, 1, primaryText);
            AppendPresetLiveTileText(xml, 2, secondaryText);
        }
        else
        {
            if (imageUri.empty())
                return false;
            AppendPresetLiveTileBindingStart(
                xml,
                L"TileSquare150x150Image",
                L"TileSquareImage");
            AppendPresetLiveTileImage(xml, imageUri);
        }
    }
    else if (IEquals(bindingName ? bindingName : L"", L"TileWide"))
    {
        if (!badgeText.empty())
        {
            std::wstring detailText = primaryText;
            if (!secondaryText.empty())
                detailText += (detailText.empty() ? L"" : L"\r\n") + secondaryText;

            AppendPresetLiveTileBindingStart(
                xml,
                L"TileWide310x150BlockAndText02",
                L"TileWideBlockAndText02");
            AppendPresetLiveTileText(xml, 1, detailText);
            AppendPresetLiveTileText(xml, 2, badgeText);
            AppendPresetLiveTileText(xml, 3, L"");
        }
        else if (hasText)
        {
            if (imageUri.empty())
                return false;
            AppendPresetLiveTileBindingStart(
                xml,
                L"TileWide310x150ImageAndText02",
                L"TileWideImageAndText02");
            AppendPresetLiveTileImage(xml, imageUri);
            AppendPresetLiveTileText(xml, 1, primaryText);
            AppendPresetLiveTileText(xml, 2, secondaryText);
        }
        else
        {
            if (imageUri.empty())
                return false;
            AppendPresetLiveTileBindingStart(
                xml,
                L"TileWide310x150Image",
                L"TileWideImage");
            AppendPresetLiveTileImage(xml, imageUri);
        }
    }
    else if (IEquals(bindingName ? bindingName : L"", L"TileLarge"))
    {
        if (imageUri.empty())
            return false;

        if (!badgeText.empty())
        {
            AppendPresetLiveTileBindingStart(xml, L"TileSquare310x310BlockAndText02");
            AppendPresetLiveTileImage(xml, imageUri);
            AppendPresetLiveTileText(xml, 1, badgeText);
            AppendPresetLiveTileText(xml, 2, primaryText);
            AppendPresetLiveTileText(xml, 3, secondaryText);
            for (int id = 4; id <= 7; ++id)
                AppendPresetLiveTileText(xml, id, L"");
        }
        else if (hasText)
        {
            AppendPresetLiveTileBindingStart(xml, L"TileSquare310x310ImageAndTextOverlay02");
            AppendPresetLiveTileImage(xml, imageUri);
            AppendPresetLiveTileText(xml, 1, primaryText);
            AppendPresetLiveTileText(xml, 2, secondaryText);
        }
        else
        {
            AppendPresetLiveTileBindingStart(xml, L"TileSquare310x310Image");
            AppendPresetLiveTileImage(xml, imageUri);
        }
    }
    else
    {
        return false;
    }

    xml += L"    </binding>\r\n";
    return true;
}
