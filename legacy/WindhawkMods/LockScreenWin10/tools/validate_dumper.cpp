// Exercise the production serializer without installing or attaching the dumper.
#include "../builds/lockapp-xaml-dumper.wh.cpp"
#include <cstdio>
#include <stdexcept>
#include <filesystem>
#include <fstream>
#include <winrt/Windows.UI.Xaml.Markup.h>
#include <windows.ui.xaml.hosting.desktopwindowxamlsource.h>

void PumpFixture(unsigned milliseconds=150) {
    const auto end=GetTickCount64()+milliseconds;
    do {
        MSG message;
        while (PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
        Sleep(1);
    } while (GetTickCount64()<end);
}

void ValidateContinuousCapture() {
    wchar_t executable[32768]; GetModuleFileNameW(nullptr,executable,32768);
    auto directory=std::filesystem::path(executable).parent_path();
    auto suffix=std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
    auto stream=directory/(L"live-test-"+suffix+L".jsonl");
    auto overflow=directory/(L"overflow-test-"+suffix+L".jsonl");
    auto quota=directory/(L"quota-test-"+suffix+L".jsonl");
    if (!g_writer.Open(stream.wstring(),20*1024*1024)) throw std::runtime_error("Could not open isolated stream");
    g_writer.Write(L"{\"event\":\"header\",\"schema\":5}");
    g_writer.WriteBatch({L"{\"event\":\"snapshotBegin\",\"sequence\":1}",
        L"{\"event\":\"snapshot\",\"sequence\":1,\"handle\":11,\"name\":\"TestText\",\"runtimeType\":\"Windows.UI.Xaml.Controls.TextBlock\",\"properties\":[]}",
        L"{\"event\":\"snapshotEnd\",\"sequence\":1,\"captured\":1,\"failures\":0}"});
    auto start=GetTickCount64();
    wuxc::TextBlock text; text.Name(L"TestText"); text.Text(L"initial");
    auto observer=LiveElement::Create(text,11,start);
    g_writer.Write(observer->Baseline(0,0,L"TextBlock#TestText"));
    for (unsigned i=0;i<200;++i) text.Text(L"event-"+std::to_wstring(i));
    text.Margin({1,2,3,4});
    wuxc::Grid card; card.Name(L"TestCard");
    wuxm::AcrylicBrush first,second; first.TintOpacity(0.7); card.Background(first);
    auto cardObserver=LiveElement::Create(card,12,start);
    g_writer.Write(cardObserver->Baseline(0,1,L"Grid#TestCard"));
    first.TintOpacity(0.4); // Same brush instance: parent Background does not change.
    card.Background(second); second.TintOpacity(0.2);
    g_writer.Write(L"{\"event\":\"oldBrushDetached\"}");
    first.TintOpacity(0.123); // This must not trigger a current-background record.
    auto button=wux::Markup::XamlReader::Load(LR"(
        <Button xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
                xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml">
          <Button.Template><ControlTemplate TargetType="Button"><Grid x:Name="StateRoot">
            <VisualStateManager.VisualStateGroups><VisualStateGroup x:Name="CommonStates">
              <VisualState x:Name="Normal"/><VisualState x:Name="PointerOver"><VisualState.Setters>
                <Setter Target="StateRoot.Opacity" Value="0.5"/>
              </VisualState.Setters></VisualState>
            </VisualStateGroup></VisualStateManager.VisualStateGroups>
          </Grid></ControlTemplate></Button.Template>
        </Button>)").as<wuxc::Button>();
    // State-change events require a connected XAML tree. Use our own offscreen,
    // nonactivating test window; no real application or Secure Desktop is touched.
    HWND fixtureWindow=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"STATIC",L"XAML dumper test",
        WS_POPUP,-32000,-32000,320,200,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    if (!fixtureWindow) throw std::runtime_error("Fixture window failed");
    wux::Hosting::DesktopWindowXamlSource island;
    auto interop=island.as<IDesktopWindowXamlSourceNative>();
    winrt::check_hresult(interop->AttachToWindow(fixtureWindow));
    HWND islandWindow=nullptr; winrt::check_hresult(interop->get_WindowHandle(&islandWindow));
    SetWindowPos(islandWindow,nullptr,0,0,320,200,SWP_NOACTIVATE|SWP_SHOWWINDOW);
    island.Content(button); ShowWindow(fixtureWindow,SW_SHOWNOACTIVATE); PumpFixture();
    button.ApplyTemplate();
    auto buttonRoot=wuxm::VisualTreeHelper::GetChild(button,0).as<wux::FrameworkElement>();
    printf("STATE fixture groups before initial state: %u\n",wux::VisualStateManager::GetVisualStateGroups(buttonRoot).Size());
    wux::VisualStateManager::GoToState(button,L"Normal",false);
    auto stateObserver=LiveElement::Create(buttonRoot,13,start);
    printf("STATE fixture subscribed groups: %zu\n",stateObserver->states.size());
    g_writer.Write(stateObserver->Baseline(0,2,L"Grid#StateRoot"));
    if (!wux::VisualStateManager::GoToState(button,L"PointerOver",false)) throw std::runtime_error("Hover fixture failed");
    PumpFixture();
    if (!wux::VisualStateManager::GoToState(button,L"Normal",false)) throw std::runtime_error("Normal fixture failed");
    PumpFixture();
    observer->Stop(); cardObserver->Stop(); stateObserver->Stop();
    g_writer.Write(L"{\"event\":\"observersStopped\"}");
    text.Text(L"must-not-be-recorded"); second.TintOpacity(0.999);
    wux::VisualStateManager::GoToState(button,L"PointerOver",false);
    PumpFixture();
    island.Content(nullptr); island.Close(); DestroyWindow(fixtureWindow);
    for (unsigned i=0;i<100;++i) { auto cycle=LiveElement::Create(text,100+i,start); cycle->Stop(); cycle->Stop(); }
    g_writer.Close();
    { DumpWriter writer; if (!writer.Open(overflow.wstring(),4096,16)) throw std::runtime_error("Overflow fixture open failed");
      writer.Write(std::wstring(100,L'x')); writer.Close(); }
    { DumpWriter writer; if (!writer.Open(quota.wstring(),256)) throw std::runtime_error("Quota fixture open failed");
      writer.WriteBatch({std::wstring(100,L'x'),std::wstring(100,L'y')}); writer.Close(); }
    auto quote=[](std::filesystem::path const& path) { return Utf8FromWide(L"\""+JsonEscape(path.wstring())+L"\""); };
    std::ofstream manifest(directory/"live-test-manifest.json");
    manifest<<"{\"stream\":"<<quote(stream)<<",\"overflow\":"<<quote(overflow)<<",\"quota\":"<<quote(quota)<<"}\n";
    puts("PASS: real DP/brush/state callbacks, 100 detach cycles and background writer fixtures completed");
}

int main() {
    try {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        auto manager=wux::Hosting::WindowsXamlManager::InitializeForCurrentThread();
        wuxm::AcrylicBrush acrylic;
        acrylic.TintColor({255,23,24,25}); acrylic.TintOpacity(0.7);
        acrylic.Opacity(0.8); acrylic.TintLuminosityOpacity(0.25);
        acrylic.BackgroundSource(wuxm::AcrylicBackgroundSource::Backdrop);
        acrylic.AlwaysUseFallback(true);
        wuxc::Grid card; card.Background(acrylic); card.CornerRadius({6,6,6,6});
        auto details=RuntimeDumpDetails(card);
        fwprintf(stdout,L"JSON %ls\n",details.c_str()); fflush(stdout);
        for (auto expected:{L"TintOpacity=0.7",L"Opacity=0.8",L"TintLuminosityOpacity=0.25",
                            L"AlwaysUseFallback=true",L"BackgroundSource=1",L"6,6,6,6"})
            if (details.find(expected)==std::wstring::npos) throw std::runtime_error("Acrylic details missing");
        wuxc::ContentPresenter presenter;
        presenter.Background(wuxm::SolidColorBrush({13,255,255,255}));
        presenter.CornerRadius({8,8,8,8}); presenter.IsHitTestVisible(false);
        details=RuntimeDumpDetails(presenter);
        for (auto expected:{L"#0DFFFFFF",L"8,8,8,8",L"\"IsHitTestVisible\":false"})
            if (details.find(expected)==std::wstring::npos) throw std::runtime_error("Presenter details missing");
        fwprintf(stdout,L"JSON %ls\n",details.c_str());
        wuxc::Button button; button.IsEnabled(false); button.Background(acrylic);
        details=RuntimeDumpDetails(button);
        if (details.find(L"\"IsEnabled\":false")==std::wstring::npos)
            throw std::runtime_error("Evaluated IsEnabled missing");
        fwprintf(stdout,L"JSON %ls\n",details.c_str());
        puts("PASS: real acrylic, presenter paint/corners, and evaluated interaction flags serialized");
        ValidateContinuousCapture();
        fflush(stdout); ExitProcess(0);
    } catch (winrt::hresult_error const& e) {
        fwprintf(stderr,L"FAIL %08X %ls\n",static_cast<unsigned>(e.code().value),e.message().c_str()); return 1;
    } catch (std::exception const& e) { fprintf(stderr,"FAIL %s\n",e.what()); return 1; }
}
