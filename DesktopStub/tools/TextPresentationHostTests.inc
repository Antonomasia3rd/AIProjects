static void TestTextPresentationHost()
{
    using namespace aip::content;
    std::wstring original,error;ReadIniFileForMutation(original);
    auto text=BuildInitialIniTemplate();aip::WriteIniValueToText(text,L"Content",L"Enabled",L"0");
    Check(WriteContentFixture(text),"presentation fixture owns only the private host profile");
    Configuration cfg;SecondaryTileConfiguration tiles;
    Check(ReadContentConfiguration(cfg,error,&tiles)&&!cfg.presentation.Active(),"new presentation defaults do not opt existing profiles into changed pixels");
    Check(DispatchTextPresentationCommand(nullptr,ID_TEXT_PRESENTATION+1)&&DispatchTextPresentationCommand(nullptr,ID_TEXT_PRESENTATION+16+8)&&
        ReadContentConfiguration(cfg,error,&tiles)&&cfg.presentation.Active()&&cfg.presentation.primaryStyle==L"title",
        "named style and preset tray actions use canonical shared configuration");
    Check(SaveContentMenuSettings(nullptr,{{L"Tile.prayer",L"TextStyle",L"body"}})&&ReadContentConfiguration(cfg,error,&tiles)&&
        tiles.tiles.back().presentation.primaryStyle==L"body"&&cfg.presentation.primaryStyle==L"title","source text style overrides inherit global settings without replacing them");
    const auto saved=cfg.presentation;
    Check(!SaveContentMenuSettings(nullptr,{{L"TileText",L"TextMaxLines",L"99"}})&&ReadContentConfiguration(cfg,error)&&
        TextPresentationKey(cfg.presentation)==TextPresentationKey(saved),"invalid line limit applies no partial presentation change");
    Snapshot frame;frame.active=true;frame.text={L"Visible title",L"Description\nsecond line",L"Paused"};frame.presentation=cfg.presentation;
    {ScopedGdiPlus gdi;Check(gdi.ok(),"presentation fixture uses only offscreen GDI+");if(gdi.ok()){
        Gdiplus::Bitmap canvas(150,150,PixelFormat32bppARGB);Gdiplus::Graphics graphics(&canvas);graphics.Clear(Gdiplus::Color(0,0,0,0));
        ContentRenderScope scope(&frame);Gdiplus::Font font(L"Segoe UI",12,Gdiplus::FontStyleRegular,Gdiplus::UnitPixel);Gdiplus::REAL y=10;
        Check(DrawTopSourceField(graphics,frame.text.primary,font,10,y,130,130,2,255,0)&&y>10,"opt-in named size renders within bounded bitmap fields");
    }}
    Check(WriteContentFixture(original),"presentation fixture restores prior private configuration");
}
