// Exercise the actual helper and guard in an isolated UWP process.
#include "../builds/windows-11-lockapp-styler.wh.cpp"
#include <cstdio>
#include <stdexcept>

void Require(bool condition,const char* reason) {
    if (!condition) throw std::runtime_error(reason);
}

int main() {
    try {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        auto manager=winrt::Windows::UI::Xaml::Hosting::WindowsXamlManager::InitializeForCurrentThread();
        using namespace win10lock;
        bool rejected=false;
        try {
            GetStyleFromXamlSetters(L"TextBlock",L"<Setter Property=\"Translation\" Value=\"0,20,0\"/>");
        } catch (std::runtime_error const&) { rejected=true; }
        Require(rejected,"The crash-producing Translation setter was not rejected");
        puts("PASS: unsafe Translation setter rejected before XamlReader");

        auto root=x::Markup::XamlReader::Load(LR"(
          <Grid xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
                xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" Width="336" Height="400">
            <Border x:Name="Background" Background="#99171717"/>
            <Button x:Name="Title" VerticalAlignment="Top" Height="37" Width="336">
              <Button.Template><ControlTemplate TargetType="Button"><Grid>
                <ContentPresenter Content="{TemplateBinding Content}" HorizontalAlignment="Left" VerticalAlignment="Top"/>
              </Grid></ControlTemplate></Button.Template>
              <StackPanel x:Name="Header" Orientation="Horizontal">
                <Grid x:Name="Paint" Width="32" Height="32" Margin="0,0,16,0"/>
                <TextBlock x:Name="TitleText" Text="Anacapri, Italy" Visibility="Collapsed"/>
                <TextBlock x:Name="Prompt" Text="Like what you see?" FontFamily="Segoe UI" FontSize="15" LineHeight="20" Margin="0,5,0,0"/>
              </StackPanel>
            </Button>
            <StackPanel x:Name="Expanded" Margin="36,37,0,0" Padding="12,5,12,12" VerticalAlignment="Top">
              <ContentPresenter x:Name="Feedback" Height="120"/>
              <StackPanel x:Name="CopyrightContainer">
                <Rectangle Height="1" Margin="0,8,0,0"/>
                <TextBlock x:Name="Copyright" FontSize="12" Margin="0,8,0,4" Text="Copyright"/>
              </StackPanel>
            </StackPanel>
          </Grid>)").as<c::Grid>();
        auto background=root.FindName(L"Background").as<c::Border>();
        auto title=root.FindName(L"Title").as<c::Button>();
        auto header=root.FindName(L"Header").as<c::StackPanel>();
        auto source=root.FindName(L"TitleText").as<c::TextBlock>();
        auto paint=root.FindName(L"Paint").as<c::Grid>();
        auto expanded=root.FindName(L"Expanded").as<c::StackPanel>();
        auto feedback=root.FindName(L"Feedback").as<c::ContentPresenter>();
        auto copyright=root.FindName(L"Copyright").as<c::TextBlock>();
        auto copyrightContainer=root.FindName(L"CopyrightContainer").as<c::StackPanel>();
        auto prompt=root.FindName(L"Prompt").as<c::TextBlock>();
        auto firstChild=expanded.Children().GetAt(0),secondChild=expanded.Children().GetAt(1);
        auto initialMargin=feedback.Margin();
        root.Measure({336,400}); root.Arrange({0,0,336,400}); root.UpdateLayout();
        unsigned clicks=0;
        auto clickToken=title.Click([&](auto const&,auto const&) { ++clicks; });
        for (unsigned cycle=0;cycle<200;++cycle) {
            source.Text(L"Anacapri, Italy");
            copyright.Text(L"Copyright");
            auto state=Badge::Create(root,root,paint,background,expanded,source,title,feedback,copyright,prompt,copyrightContainer);
            Require(static_cast<bool>(state->circle.try_as<x::Shapes::Ellipse>()),"Badge is not an ellipse");
            Require(state->circle.Width()==32 && state->circle.Height()==32,"Ellipse is not 32 by 32");
            Require(state->circleAnimation && state->locationAnimation,"Fade expressions not started");
            Require(header.Children().Size()==3 && header.Children().GetAt(1)==source,"Native title children changed");
            Require(expanded.Children().GetAt(0)==firstChild && expanded.Children().GetAt(1)==secondChild,"Native panel indices changed");
            Require(feedback.Margin().Top>initialMargin.Top,"Header space not reserved");
            Require(state->locationText.FontSize()==24 && state->locationText.FontWeight().Weight==300,"Reference location typography not applied");
            Require(state->locationText.TextDecorations()==winrt::Windows::UI::Text::TextDecorations::None,"Location remains underlined");
            Require(state->copyrightText.FontSize()==12 && state->copyrightText.Foreground().as<m::SolidColorBrush>().Color().A==153,"Reference copyright typography not applied");
            Require(copyrightContainer.Visibility()==x::Visibility::Collapsed,"Native copyright footer remains visible");
            source.Text(L"Updated location");
            Require(state->locationText.Text()==L"Updated location","Location binding did not update");
            copyright.Text(L"Updated credit");
            Require(state->copyrightText.Text()==L"Updated credit","Copyright binding did not update");
            source.Text(L""); copyright.Text(L"");
            Require(feedback.Margin()==initialMargin,"Empty location leaves unused space");
            source.Text(L"Matterhorn mountain, Switzerland"); copyright.Text(L"Copyright");
            Require(feedback.Margin().Top>initialMargin.Top,"Header callbacks stopped after first update");
            float gate=-1;
            background.Visibility(x::Visibility::Collapsed);
            state->gate.TryGetScalar(L"Visible",gate);
            Require(gate==0,"Visibility gate did not close");
            background.Visibility(x::Visibility::Visible);
            state->gate.TryGetScalar(L"Visible",gate);
            Require(gate==1,"Visibility gate did not reopen");
            root.Measure({336,400}); root.Arrange({0,0,336,400}); root.UpdateLayout();
            state->PositionLocation(); root.UpdateLayout();
            if (!cycle) {
                state->locationLink.ApplyTemplate();
                auto border=Child(state->locationLink,0).as<c::Border>();
                for (auto pair:{std::pair{L"PointerOver",0x33},{L"Normal",0x00},{L"Pressed",0x33},{L"Normal",0x00}}) {
                    Require(x::VisualStateManager::GoToState(state->locationLink,pair.first,false),"Location hover state missing");
                    Require(border.Background().as<m::SolidColorBrush>().Color().A==pair.second,"Location hover paint did not change or restore");
                }
            }
            auto pos=state->headerBlock.TransformToVisual(expanded).TransformPoint({0,0});
            Require(std::abs(pos.Y+12)<0.02,"Header does not follow the heading baseline");
            if (!cycle) {
                auto loc=state->locationText.TransformToVisual(root).TransformPoint({0,0});
                auto credit=state->copyrightText.TransformToVisual(root).TransformPoint({0,0});
                auto line=state->separator.TransformToVisual(root).TransformPoint({0,0});
                auto body=feedback.TransformToVisual(root).TransformPoint({0,0});
                printf("REFERENCE fixture: location=(%g,%g), credit=(%g,%g), separator=(%g,%g), feedback=(%g,%g)\n",loc.X,loc.Y,credit.X,credit.Y,line.X,line.Y,body.X,body.Y);
                Require(std::abs(loc.X-56)<0.02 && std::abs(loc.Y-31)<0.02,"Location origin differs from Windows 10 dump");
                Require(std::abs(credit.Y-100)<0.1 && std::abs(line.Y-145)<0.1 && std::abs(body.Y-146)<0.1,"Header/footer order or spacing differs from Windows 10 dump");
                double twoLineBody=body.Y;
                source.Text(L"Anacapri, Italy");
                root.Measure({336,400}); root.Arrange({0,0,336,400}); root.UpdateLayout();
                Require(feedback.TransformToVisual(root).TransformPoint({0,0}).Y<twoLineBody-31,"Single-line location did not reduce reserved height");
            }
            if (!cycle) {
                state->InvokeLocation();
                MSG msg;
                for (unsigned i=0;i<50 && PeekMessage(&msg,nullptr,0,0,PM_REMOVE);++i) {
                    TranslateMessage(&msg); DispatchMessage(&msg);
                }
                Require(clicks==1,"Location did not invoke the native title button");
                title.IsEnabled(false);
                Require(!state->locationLink.IsEnabled(),"Disabled native title did not disable location");
                title.IsEnabled(true);
            }
            std::weak_ptr<Badge> weak=state;
            state->Dispose(); state->Dispose();
            Require(root.Children().Size()==3 && expanded.Children().Size()==2,"Generated nodes survived cleanup");
            Require(feedback.Margin()==initialMargin && paint.Opacity()==1,"Native properties not restored");
            Require(copyrightContainer.Visibility()==x::Visibility::Visible,"Native copyright visibility not restored");
            state.reset(); Require(weak.expired(),"Badge state leaked through an event or binding");
            // These used to be unsafe with raw pointer event captures.
            background.Visibility(x::Visibility::Collapsed);
            background.Visibility(x::Visibility::Visible);
            source.Text(L"Changed after detach");
        }
        title.Click(clickToken);
        c::Grid iconHost; c::PathIcon selected,unselected;
        selected.Opacity(0); iconHost.Children().Append(selected); iconHost.Children().Append(unselected);
        for (unsigned cycle=0;cycle<200;++cycle) {
            auto icon=FeedbackIcon::Create(iconHost,cycle%2);
            Require(icon && iconHost.Children().Size()==3,"Feedback glyph not appended");
            Require(iconHost.Children().GetAt(0)==selected && iconHost.Children().GetAt(1)==unselected,"Native icon indices changed");
            Require(icon->glyph.Text()==(cycle%2 ? L"\uEA92" : L"\uE006"),"Wrong feedback glyph");
            selected.Opacity(1); unselected.Opacity(0);
            Require(selected.Visibility()==x::Visibility::Collapsed && unselected.Visibility()==x::Visibility::Collapsed,"Native selection animation reveals thumbs");
            icon->Dispose(); icon->Dispose();
            Require(iconHost.Children().Size()==2 && selected.Visibility()==x::Visibility::Visible && unselected.Visibility()==x::Visibility::Visible,"Feedback cleanup failed");
        }
        puts("PASS: location hover/press/leave states and 200 feedback icon restoration cycles");
        puts("PASS: 200 helper attach/update/detach cycles; native parents and indices preserved");
        puts("PASS: measured location/copyright layout, wrapping, bindings, native action, and cleanup");
        fflush(stdout); ExitProcess(0);
    } catch (winrt::hresult_error const& e) {
        fwprintf(stderr,L"FAIL %08X %ls\n",static_cast<unsigned>(e.code().value),e.message().c_str());
        return 1;
    } catch (std::exception const& e) {
        fprintf(stderr,"FAIL %s\n",e.what()); return 1;
    }
}
