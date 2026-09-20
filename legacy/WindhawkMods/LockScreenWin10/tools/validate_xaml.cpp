// Isolated XAML parser/setter validation. Does not attach to any process.
#include <windows.h>
#undef GetCurrentTime
#include <winrt/base.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Xaml.Hosting.h>
#include <winrt/Windows.UI.Xaml.Markup.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Text.h>
#include <cstdio>
#include <cmath>
#include "../build/xaml_cases.inc"

namespace x = winrt::Windows::UI::Xaml;

void Apply(x::FrameworkElement const& element, Case const& item) {
    auto resources = x::Markup::XamlReader::Load(item.style).as<x::ResourceDictionary>();
    auto style = resources.First().Current().Value().as<x::Style>();
    for (auto const& base : style.Setters()) {
        auto setter = base.as<x::Setter>();
        auto value = setter.Value();
        // Match the supplied styler's existing SetOrClearValue workaround:
        // XamlReader boxes FontWeight as int, but SetValue expects FontWeight.
        if (setter.Property() == x::Controls::TextBlock::FontWeightProperty() ||
            setter.Property() == x::Controls::Control::FontWeightProperty()) {
            if (auto weight = value.try_as<int>())
                value = winrt::box_value(winrt::Windows::UI::Text::FontWeight{static_cast<uint16_t>(*weight)});
        }
        element.SetValue(setter.Property(), value);
    }
}

void ApplyNamed(x::FrameworkElement const& element, const wchar_t* target) {
    for (auto const& item : cases) if (wcscmp(item.target, target) == 0) { Apply(element, item); return; }
    throw std::runtime_error("Missing fixture rule");
}

void PrintRect(const wchar_t* name, x::FrameworkElement const& element, x::UIElement const& root) {
    auto p = element.TransformToVisual(root).TransformPoint({0,0});
    fwprintf(stdout,L"FIXTURE %ls x=%g y=%g width=%g height=%g\n",name,p.X,p.Y,element.ActualWidth(),element.ActualHeight());
}

void CheckButtonStates(x::Controls::Control const& control) {
    control.Measure({48,48}); control.Arrange({0,0,48,48}); control.ApplyTemplate();
    auto root = x::Media::VisualTreeHelper::GetChild(control,0).as<x::Controls::Grid>();
    auto backdrop = root.Children().GetAt(0).as<x::Controls::Border>();
    auto presenter = root.Children().GetAt(1).as<x::Controls::ContentPresenter>();
    for (auto const& state : {std::pair{L"PointerOver",0x1A}, {L"Pressed",0x33}, {L"Normal",0x00}}) {
        if (!x::VisualStateManager::GoToState(control,state.first,false))
            throw std::runtime_error("Media visual state missing");
        auto brush = backdrop.Background().as<x::Media::SolidColorBrush>();
        if (brush.Color().A != state.second || backdrop.CornerRadius().TopLeft != 0)
            throw std::runtime_error("Media visual state produced wrong paint");
    }
    if (auto button = control.try_as<x::Controls::Button>()) {
        button.Content(winrt::box_value(L"\uE768"));
        if (winrt::unbox_value<winrt::hstring>(presenter.Content()) != L"\uE768")
            throw std::runtime_error("Play icon binding did not update");
        button.Content(winrt::box_value(L"\uE769"));
        if (winrt::unbox_value<winrt::hstring>(presenter.Content()) != L"\uE769")
            throw std::runtime_error("Pause icon binding did not update");
    }
    fwprintf(stdout,L"PASS media pointer/press/normal paint and live content\n");
}

void CheckLayoutFixture() {
    // Reproduce the provided Win11 outer layout with a 146-DIP widget payload.
    // This tests the outer geometry only, not the private LockCanvas implementation.
    auto root = x::Markup::XamlReader::Load(LR"(
      <Grid xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
            xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" Width="1920" Height="1080">
        <StackPanel x:Name="TimeAndDatePanel">
          <StackPanel x:Name="TimePanel" Orientation="Horizontal"><TextBlock x:Name="Time" Text="3:27"/></StackPanel>
          <TextBlock x:Name="Date" Text="Friday, September 11"/>
        </StackPanel>
        <Grid x:Name="Footer" VerticalAlignment="Bottom">
          <Grid.RowDefinitions><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/></Grid.RowDefinitions>
          <Grid x:Name="WidgetCanvasPanel"><Grid x:Name="WidgetCanvasContainer">
            <StackPanel x:Name="WidgetsContainer" Width="1316"><StackPanel x:Name="WidgetGroupPanel" Height="146" Width="1236"/></StackPanel>
          </Grid></Grid>
          <Grid x:Name="MediaControlsContainer"><Grid x:Name="MediaTransportControls"/></Grid>
          <StackPanel x:Name="StatusProviderPanel" Height="24" Width="80"/>
        </Grid>
      </Grid>)").as<x::Controls::Grid>();
    ApplyNamed(root, L"Grid#LockScreenTextContent");
    for (auto const& pair : {std::pair{L"TimeAndDatePanel",L"Grid#LockScreenTextContent > StackPanel#TimeAndDatePanel"},
          {L"Footer",L"Grid#LockScreenTextContent > Grid"},{L"TimePanel",L"StackPanel#TimePanel"},
          {L"Time",L"TextBlock#Time"},{L"Date",L"TextBlock#Date"},
          {L"WidgetCanvasPanel",L"Grid#WidgetCanvasPanel"},{L"WidgetCanvasContainer",L"Grid#WidgetCanvasContainer"},
          {L"WidgetsContainer",L"StackPanel#WidgetsContainer"},{L"WidgetGroupPanel",L"StackPanel#WidgetGroupPanel"},
          {L"MediaControlsContainer",L"Grid#MediaControlsContainer"},{L"MediaTransportControls",L"Grid#MediaTransportControls"},
          {L"StatusProviderPanel",L"StackPanel#StatusProviderPanel"}})
        ApplyNamed(root.FindName(pair.first).as<x::FrameworkElement>(), pair.second);
    root.Measure({1920,1080}); root.Arrange({0,0,1920,1080}); root.UpdateLayout();
    for (auto name : {L"Time",L"Date",L"WidgetGroupPanel",L"MediaTransportControls",L"StatusProviderPanel"})
        PrintRect(name, root.FindName(name).as<x::FrameworkElement>(), root);
    for (auto const& expected : {std::pair{L"Time",winrt::Windows::Foundation::Point{36,578}},
            {L"Date",{36,729}},{L"WidgetGroupPanel",{36,836}},{L"MediaTransportControls",{1560,870}}}) {
        auto node = root.FindName(expected.first).as<x::FrameworkElement>();
        auto actual = node.TransformToVisual(root).TransformPoint({0,0});
        if (std::abs(actual.X-expected.second.X)>0.02 || std::abs(actual.Y-expected.second.Y)>0.02)
            throw std::runtime_error("1080p outer layout differs from reference coordinates");
    }
}

void CheckFeedbackLayoutFixture() {
    // Win11's native two-row template, with the measured Win10 preset applied.
    auto root=x::Markup::XamlReader::Load(LR"(
      <Grid xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
            xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" Width="276">
        <Grid.ColumnDefinitions><ColumnDefinition Width="Auto"/><ColumnDefinition Width="*"/></Grid.ColumnDefinitions>
        <Grid.RowDefinitions><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/></Grid.RowDefinitions>
        <Border Grid.RowSpan="2" Grid.ColumnSpan="2"/>
        <Grid x:Name="Icon" Grid.RowSpan="2"><TextBlock Text="&#xE006;" FontFamily="Segoe MDL2 Assets" FontSize="16" LineHeight="18"/></Grid>
        <TextBlock x:Name="Heading" Grid.Column="1" Text="It's interesting!"/>
        <TextBlock x:Name="Description" Grid.Column="1" Grid.Row="1" Text="Tell me more about the image."/>
      </Grid>)").as<x::Controls::Grid>();
    ApplyNamed(root.FindName(L"Icon").as<x::FrameworkElement>(),L"LockApp.LikeDislikeButton > Grid#ButtonRoot > Grid");
    ApplyNamed(root.FindName(L"Heading").as<x::FrameworkElement>(),L"LockApp.LikeDislikeButton > Grid#ButtonRoot > TextBlock[3]");
    ApplyNamed(root.FindName(L"Description").as<x::FrameworkElement>(),L"LockApp.LikeDislikeButton > Grid#ButtonRoot > TextBlock[4]");
    root.Measure({276,500}); root.Arrange({0,0,276,root.DesiredSize().Height}); root.UpdateLayout();
    for (auto expected:{std::pair{L"Icon",winrt::Windows::Foundation::Point{0,9}},
                        {L"Heading",{24,7}},{L"Description",{24,29}}}) {
        auto actual=root.FindName(expected.first).as<x::FrameworkElement>().TransformToVisual(root).TransformPoint({0,0});
        if (std::abs(actual.X-expected.second.X)>0.02 || std::abs(actual.Y-expected.second.Y)>0.02)
            throw std::runtime_error("Feedback content differs from Windows 10 dump coordinates");
    }
    if (std::abs(root.ActualHeight()-53)>0.02) throw std::runtime_error("Feedback row height differs from reference");
    fwprintf(stdout,L"PASS feedback reference: row 276x53, icon (0,9), heading (24,7), description (24,29)\n");
}

int main() {
    int failures = 0;
    try {
        namespace x = winrt::Windows::UI::Xaml;
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        auto manager = x::Hosting::WindowsXamlManager::InitializeForCurrentThread();
        for (auto const& item : cases) {
            try {
                auto element = x::Markup::XamlReader::Load(item.element).as<x::FrameworkElement>();
                Apply(element, item);
                // Exercise custom templates as well as parsing their markup.
                if (auto control = element.try_as<x::Controls::Control>()) {
                    control.ApplyTemplate();
                    if (wcsstr(item.target,L"Button#PreviousButton") ||
                        wcsstr(item.target,L"Button#PlayPauseButton") ||
                        wcsstr(item.target,L"Button#NextButton")) CheckButtonStates(control);
                }
                fwprintf(stdout, L"PASS %ls\n", item.target);
            } catch (winrt::hresult_error const& e) {
                fwprintf(stdout, L"FAIL %ls : %08X %ls\n", item.target, static_cast<unsigned>(e.code().value), e.message().c_str());
                ++failures;
            } catch (std::exception const& e) {
                fwprintf(stdout,L"FAIL %ls\n",item.target);
                fprintf(stdout,"%s\n",e.what());
                ++failures;
            }
        }
        for (auto optical : {x::OpticalMarginAlignment::None, x::OpticalMarginAlignment::TrimSideBearings}) {
            x::Controls::TextBlock text;
            text.Text(L"3:27");
            text.FontFamily(x::Media::FontFamily(L"Segoe UI"));
            text.FontSize(128);
            text.FontWeight(winrt::Windows::UI::Text::FontWeights::Light());
            text.OpticalMarginAlignment(optical);
            text.Measure({1000,300}); text.Arrange({0,0,text.DesiredSize().Width,text.DesiredSize().Height});
            fwprintf(stdout,L"Clock optical=%d desiredWidth=%g actualWidth=%g\n",static_cast<int>(optical),
                     text.DesiredSize().Width,text.ActualWidth());
        }
        CheckLayoutFixture();
        CheckFeedbackLayoutFixture();
        fwprintf(stdout,L"XAML validation: %u cases, %d failures\n",static_cast<unsigned>(std::size(cases)),failures);
        fflush(stdout);
        ExitProcess(failures ? 1 : 0);
    } catch (winrt::hresult_error const& e) {
        fwprintf(stderr,L"HOST ERROR %08X %ls\n",static_cast<unsigned>(e.code().value),e.message().c_str());
        return 2;
    } catch (std::exception const& e) {
        fprintf(stderr,"VALIDATION ERROR %s\n",e.what());
        return 2;
    }
}
