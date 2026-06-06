static constexpr UINT ID_WIDGET_REFRESH = 3001;
static constexpr UINT ID_WIDGET_TOPMOST = 3002;
static constexpr UINT ID_WIDGET_EXIT = 3003;

struct WidgetContext
{
    RuntimeContext runtime;
    bool alwaysOnTop = true;
    bool dragging = false;
    POINT dragOffset = {};
};

static void DrawWidgetText(Graphics& graphics, const std::wstring& text, Font& font, Brush& brush, const RectF& rect)
{
    StringFormat format;
    format.SetTrimming(StringTrimmingEllipsisCharacter);
    format.SetFormatFlags(StringFormatFlagsNoWrap);
    graphics.DrawString(text.empty() ? L" " : text.c_str(), -1, &font, rect, &format, &brush);
}

static void PaintWidget(HWND hwnd, WidgetContext* ctx)
{
    PAINTSTRUCT ps = {};
    HDC hdc = BeginPaint(hwnd, &ps);
    if (hdc == nullptr)
    {
        return;
    }

    RECT rc = {};
    GetClientRect(hwnd, &rc);
    int width = std::max(1L, rc.right - rc.left);
    int height = std::max(1L, rc.bottom - rc.top);

    Graphics graphics(hdc);
    graphics.SetSmoothingMode(SmoothingModeAntiAlias);
    graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

    Rect full(0, 0, width, height);
    LinearGradientBrush background(full, Color(255, 28, 31, 36), Color(255, 11, 13, 17), 35.0f);
    graphics.FillRectangle(&background, full);

    int artSize = std::min(138, std::max(48, height - 36));
    Rect artRect(18, 18, artSize, artSize);
    bool drewArtwork = false;

    if (ctx != nullptr && !ctx->runtime.current.artworkPath.empty() && FileExists(ctx->runtime.current.artworkPath))
    {
        Image image(ctx->runtime.current.artworkPath.c_str());
        if (image.GetLastStatus() == Ok)
        {
            graphics.DrawImage(&image, artRect);
            drewArtwork = true;
        }
    }

    if (!drewArtwork)
    {
        SolidBrush cardBrush(Color(255, 43, 48, 56));
        SolidBrush noteBrush(Color(255, 235, 238, 245));
        graphics.FillRectangle(&cardBrush, artRect);
        Font noteFont(L"Segoe UI Symbol", static_cast<REAL>(artSize / 2), FontStyleRegular, UnitPixel);
        StringFormat centered;
        centered.SetAlignment(StringAlignmentCenter);
        centered.SetLineAlignment(StringAlignmentCenter);
        graphics.DrawString(L"NP", -1, &noteFont, RectF(static_cast<REAL>(artRect.X), static_cast<REAL>(artRect.Y), static_cast<REAL>(artRect.Width), static_cast<REAL>(artRect.Height)), &centered, &noteBrush);
    }

    int textLeft = artRect.X + artRect.Width + 18;
    int textWidth = std::max(40, width - textLeft - 18);
    int bottom = std::max(0, height - 34);

    MediaSnapshot snapshot = ctx != nullptr ? ctx->runtime.current : MediaSnapshot::Empty(L"Starting...");
    std::wstring status = snapshot.status;
    if (!snapshot.lastUpdated.empty())
    {
        status += L"  |  ";
        status += snapshot.lastUpdated;
    }

    Font sourceFont(L"Segoe UI", 12.0f, FontStyleRegular, UnitPixel);
    Font titleFont(L"Segoe UI", 25.0f, FontStyleBold, UnitPixel);
    Font artistFont(L"Segoe UI", 16.0f, FontStyleRegular, UnitPixel);
    Font statusFont(L"Segoe UI", 12.0f, FontStyleRegular, UnitPixel);
    SolidBrush sourceBrush(Color(180, 220, 226, 235));
    SolidBrush titleBrush(Color(255, 255, 255, 255));
    SolidBrush artistBrush(Color(220, 226, 232, 241));
    SolidBrush statusBrush(Color(160, 220, 226, 235));

    DrawWidgetText(graphics, snapshot.source, sourceFont, sourceBrush, RectF(static_cast<REAL>(textLeft), 20.0f, static_cast<REAL>(textWidth), 18.0f));
    DrawWidgetText(graphics, snapshot.title, titleFont, titleBrush, RectF(static_cast<REAL>(textLeft), 48.0f, static_cast<REAL>(textWidth), 34.0f));
    DrawWidgetText(graphics, snapshot.artist, artistFont, artistBrush, RectF(static_cast<REAL>(textLeft), 91.0f, static_cast<REAL>(textWidth), 24.0f));
    DrawWidgetText(graphics, status, statusFont, statusBrush, RectF(static_cast<REAL>(textLeft), static_cast<REAL>(bottom), static_cast<REAL>(textWidth), 18.0f));

    EndPaint(hwnd, &ps);
}

static void ShowWidgetMenu(HWND hwnd, WidgetContext* ctx)
{
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr)
    {
        return;
    }

    AppendMenuW(menu, MF_STRING, ID_WIDGET_REFRESH, L"Refresh now");
    AppendMenuW(menu, MF_STRING | (ctx != nullptr && ctx->alwaysOnTop ? MF_CHECKED : 0), ID_WIDGET_TOPMOST, L"Always on top");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_WIDGET_EXIT, L"Exit");

    POINT pt = {};
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd, nullptr);
    PostMessageW(hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

static LRESULT CALLBACK WidgetWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    WidgetContext* ctx = reinterpret_cast<WidgetContext*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (msg == WM_NCCREATE)
    {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        ctx = reinterpret_cast<WidgetContext*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ctx));
        if (ctx != nullptr)
        {
            ctx->runtime.hwnd = hwnd;
        }
    }

    switch (msg)
    {
    case WM_CREATE:
        if (ctx != nullptr)
        {
            SetTimer(hwnd, UPDATE_TIMER_ID, static_cast<UINT>(std::max(1, ctx->runtime.settings.updateIntervalSeconds) * 1000), nullptr);
            BeginUpdate(&ctx->runtime);
        }
        return 0;

    case WM_TIMER:
        if (wParam == UPDATE_TIMER_ID && ctx != nullptr)
        {
            BeginUpdate(&ctx->runtime);
            return 0;
        }
        break;

    case WM_NPT_UPDATE_DONE:
        if (ctx != nullptr)
        {
            std::unique_ptr<MediaSnapshot> snapshot(reinterpret_cast<MediaSnapshot*>(lParam));
            if (ctx->runtime.updateThread.joinable())
            {
                ctx->runtime.updateThread.join();
            }
            ctx->runtime.updateRunning = false;
            ctx->runtime.current = *snapshot;
            MaybeUpdateLiveTile(&ctx->runtime, *snapshot);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_PAINT:
        PaintWidget(hwnd, ctx);
        return 0;

    case WM_CONTEXTMENU:
        ShowWidgetMenu(hwnd, ctx);
        return 0;

    case WM_LBUTTONDOWN:
        if (ctx != nullptr)
        {
            RECT wr = {};
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ClientToScreen(hwnd, &pt);
            GetWindowRect(hwnd, &wr);
            ctx->dragging = true;
            ctx->dragOffset.x = pt.x - wr.left;
            ctx->dragOffset.y = pt.y - wr.top;
            SetCapture(hwnd);
        }
        return 0;

    case WM_MOUSEMOVE:
        if (ctx != nullptr && ctx->dragging && (wParam & MK_LBUTTON) != 0)
        {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ClientToScreen(hwnd, &pt);
            SetWindowPos(hwnd, nullptr, pt.x - ctx->dragOffset.x, pt.y - ctx->dragOffset.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return 0;

    case WM_LBUTTONUP:
        if (ctx != nullptr)
        {
            ctx->dragging = false;
            ReleaseCapture();
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case ID_WIDGET_REFRESH:
            if (ctx != nullptr)
            {
                BeginUpdate(&ctx->runtime);
            }
            return 0;
        case ID_WIDGET_TOPMOST:
            if (ctx != nullptr)
            {
                ctx->alwaysOnTop = !ctx->alwaysOnTop;
                SetWindowPos(hwnd, ctx->alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            }
            return 0;
        case ID_WIDGET_EXIT:
            DestroyWindow(hwnd);
            return 0;
        default:
            break;
        }
        break;

    case WM_CLOSE:
        if (ctx != nullptr)
        {
            StopUpdates(&ctx->runtime);
        }
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        if (ctx != nullptr)
        {
            StopUpdates(&ctx->runtime);
        }
        KillTimer(hwnd, UPDATE_TIMER_ID);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static int RunWidget(const AppOptions& options)
{
    (void)options;

    WidgetContext ctx;
    ctx.runtime.settings = LoadSettings();

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WidgetWndProc;
    wc.hInstance = g_hInst;
    wc.lpszClassName = WIDGET_CLASS_NAME;
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        WIDGET_CLASS_NAME,
        APP_DISPLAY_NAME,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        440,
        220,
        nullptr,
        nullptr,
        g_hInst,
        &ctx);

    if (hwnd == nullptr)
    {
        Log(L"Could not create widget window: %s", GetLastErrorText(GetLastError()).c_str());
        return 1;
    }

    ShowWindow(hwnd, SW_SHOWNORMAL);
    UpdateWindow(hwnd);

    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return static_cast<int>(msg.wParam);
}
