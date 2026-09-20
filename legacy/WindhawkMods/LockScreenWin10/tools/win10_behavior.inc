// Candidate 4: reference feedback glyphs and hover states; native controls stay put.
#include <winrt/Windows.UI.Xaml.Hosting.h>
#include <winrt/Windows.UI.Xaml.Input.h>
#include <winrt/Windows.UI.Xaml.Controls.Primitives.h>
#include <winrt/Windows.UI.Xaml.Shapes.h>
#include <winrt/Windows.UI.Xaml.Data.h>
#include <winrt/Windows.UI.Xaml.Automation.Peers.h>
#include <winrt/Windows.UI.Xaml.Automation.Provider.h>
#include <winrt/Windows.UI.Text.h>
#include <winrt/Windows.UI.Composition.h>
#include <memory>

namespace win10lock {
namespace x = winrt::Windows::UI::Xaml;
namespace c = x::Controls;
namespace m = x::Media;
namespace f = winrt::Windows::Foundation;
namespace comp = winrt::Windows::UI::Composition;
using x::Hosting::ElementCompositionPreview;

static x::FrameworkElement Child(x::DependencyObject const& parent, int index) {
    if (!parent || index < 0 || index >= m::VisualTreeHelper::GetChildrenCount(parent)) return nullptr;
    return m::VisualTreeHelper::GetChild(parent,index).try_as<x::FrameworkElement>();
}
static x::FrameworkElement Find(x::DependencyObject const& root, std::wstring_view name) {
    if (!root) return nullptr;
    std::vector<x::DependencyObject> pending{root};
    for (unsigned n=0; !pending.empty() && n<4096; ++n) {
        auto node=pending.back(); pending.pop_back();
        if (auto fe=node.try_as<x::FrameworkElement>(); fe && fe.Name()==name) return fe;
        for (int i=m::VisualTreeHelper::GetChildrenCount(node); i-- > 0;)
            pending.push_back(m::VisualTreeHelper::GetChild(node,i));
    }
    return nullptr;
}
static x::FrameworkElement Ancestor(x::FrameworkElement node, std::wstring_view name) {
    for (unsigned i=0; node && i<100; ++i) {
        if (node.Name()==name) return node;
        auto parent=m::VisualTreeHelper::GetParent(node);
        node=parent ? parent.try_as<x::FrameworkElement>() : nullptr;
    }
    return nullptr;
}
static void RestoreValue(x::FrameworkElement const& node,x::DependencyProperty const& property,
                         f::IInspectable const& value) {
    if (value==x::DependencyProperty::UnsetValue()) node.ClearValue(property);
    else node.SetValue(property,value);
}
static x::Data::Binding SourceBinding(f::IInspectable const& source,const wchar_t* path) {
    x::Data::Binding binding;
    binding.Source(source); binding.Path(x::PropertyPath(path));
    binding.Mode(x::Data::BindingMode::OneWay);
    return binding;
}

struct Badge : std::enable_shared_from_this<Badge> {
    bool active=true;
    winrt::weak_ref<x::FrameworkElement> owner,paint,backdrop,feedback;
    winrt::weak_ref<c::Grid> host;
    winrt::weak_ref<c::StackPanel> expanded;
    winrt::weak_ref<c::TextBlock> sourceLocation;
    winrt::weak_ref<c::TextBlock> sourceCopyright,sourcePrompt;
    winrt::weak_ref<x::FrameworkElement> copyrightContainer;
    winrt::weak_ref<c::Button> title;
    c::Grid badge{nullptr};
    x::Shapes::Ellipse circle{nullptr};
    c::Canvas locationOverlay{nullptr};
    c::HyperlinkButton locationLink{nullptr};
    c::TextBlock locationText{nullptr};
    c::TextBlock copyrightText{nullptr};
    c::StackPanel headerBlock{nullptr},captionBlock{nullptr};
    x::Shapes::Line separator{nullptr};
    f::IInspectable oldCopyrightVisibility{nullptr};
    x::Data::Binding oldCopyrightVisibilityBinding{nullptr};
    f::IInspectable oldPaintOpacity{nullptr},oldFeedbackMargin{nullptr};
    x::Data::Binding oldMarginBinding{nullptr};
    x::Thickness baselineMargin{},appliedMargin{};
    bool changedMargin=false;
    comp::CompositionPropertySet gate{nullptr};
    comp::Visual circleVisual{nullptr},locationVisual{nullptr};
    bool circleAnimation=false,locationAnimation=false;
    int64_t visibilityToken=0,textToken=0;
    bool watchingVisibility=false,watchingText=false;
    int64_t copyrightToken=0;
    bool watchingCopyright=false,changedCopyrightVisibility=false;
    winrt::event_token layoutToken{},clickToken{};

    void UpdateGate() noexcept {
        if (!active) return;
        try {
            auto node=backdrop.get();
            gate.InsertScalar(L"Visible",node && node.Visibility()==x::Visibility::Visible ? 1.0f : 0.0f);
        } catch (...) {}
    }
    void PositionLocation() noexcept {
        if (!active || !locationOverlay || !headerBlock) return;
        try {
            auto panel=expanded.get(); if (!panel) return;
            // Windows 10 starts this block immediately after the heading text,
            // not after the separate Windows 11 title button's bottom inset.
            double top=-12;
            if (auto prompt=sourcePrompt.get(); prompt && prompt.ActualHeight()>0)
                top=prompt.TransformToVisual(panel).TransformPoint({0,0}).Y+prompt.ActualHeight();
            double desired=top-locationOverlay.TransformToVisual(panel).TransformPoint({0,0}).Y;
            double current=c::Canvas::GetTop(headerBlock);
            if (std::isfinite(desired) && (!std::isfinite(current) || std::abs(current-desired)>0.01))
                c::Canvas::SetTop(headerBlock,desired);
            if (auto presenter=feedback.get()) {
                auto margin=baselineMargin;
                if (headerBlock.Visibility()==x::Visibility::Visible)
                    margin.Top+=std::max(0.0,top+headerBlock.DesiredSize().Height-panel.Padding().Top);
                if (presenter.Margin()!=margin) presenter.Margin(margin);
                appliedMargin=margin; changedMargin=true;
            }
        } catch (...) {}
    }
    void UpdateLocation() noexcept {
        if (!active || !headerBlock) return;
        try {
            auto text=sourceLocation.get();
            auto credit=sourceCopyright.get();
            bool hasLocation=text && !text.Text().empty(),hasCopyright=credit && !credit.Text().empty();
            locationText.Visibility(hasLocation ? x::Visibility::Visible : x::Visibility::Collapsed);
            copyrightText.Visibility(hasCopyright ? x::Visibility::Visible : x::Visibility::Collapsed);
            headerBlock.Visibility(hasLocation || hasCopyright ? x::Visibility::Visible : x::Visibility::Collapsed);
            headerBlock.Measure({276,std::numeric_limits<float>::infinity()});
            PositionLocation();
        } catch (...) {}
    }
    void InvokeLocation() noexcept {
        if (!active) return;
        try {
            auto button=title.get();
            if (!button || !button.IsEnabled()) return;
            auto peer=x::Automation::Peers::FrameworkElementAutomationPeer::FromElement(button);
            if (!peer) peer=x::Automation::Peers::ButtonAutomationPeer(button);
            auto provider=peer.GetPattern(x::Automation::Peers::PatternInterface::Invoke)
                .try_as<x::Automation::Provider::IInvokeProvider>();
            if (provider) provider.Invoke();
        } catch (winrt::hresult_error const& e) {
            Wh_Log(L"Windows 10 location: native title action unavailable (%08X)",e.code());
        } catch (...) {}
    }
    void Dispose() noexcept {
        if (!active) return;
        active=false; // Already-copied event delegates become harmless.
        try { if (auto node=backdrop.get(); node && watchingVisibility)
            node.UnregisterPropertyChangedCallback(x::UIElement::VisibilityProperty(),visibilityToken);
        } catch (...) {}
        try { if (auto text=sourceLocation.get(); text && watchingText)
            text.UnregisterPropertyChangedCallback(c::TextBlock::TextProperty(),textToken);
        } catch (...) {}
        try { if (auto text=sourceCopyright.get(); text && watchingCopyright)
            text.UnregisterPropertyChangedCallback(c::TextBlock::TextProperty(),copyrightToken);
        } catch (...) {}
        try { if (locationOverlay && layoutToken.value) locationOverlay.LayoutUpdated(layoutToken); } catch (...) {}
        try { if (locationLink && clickToken.value) locationLink.Click(clickToken); } catch (...) {}
        try { if (circleVisual && circleAnimation) circleVisual.StopAnimation(L"Opacity"); } catch (...) {}
        try { if (locationVisual && locationAnimation) locationVisual.StopAnimation(L"Opacity"); } catch (...) {}
        try { if (locationText) locationText.ClearValue(c::TextBlock::TextProperty()); } catch (...) {}
        try { if (copyrightText) copyrightText.ClearValue(c::TextBlock::TextProperty()); } catch (...) {}
        try { if (locationLink) locationLink.ClearValue(c::Control::IsEnabledProperty()); } catch (...) {}
        try { if (auto node=paint.get(); node && oldPaintOpacity)
            RestoreValue(node,x::UIElement::OpacityProperty(),oldPaintOpacity);
        } catch (...) {}
        try { if (auto presenter=feedback.get(); presenter && changedMargin && presenter.Margin()==appliedMargin) {
            if (oldMarginBinding) presenter.SetBinding(x::FrameworkElement::MarginProperty(),oldMarginBinding);
            else RestoreValue(presenter,x::FrameworkElement::MarginProperty(),oldFeedbackMargin);
        }} catch (...) {}
        try { if (auto container=copyrightContainer.get(); container && changedCopyrightVisibility &&
                container.Visibility()==x::Visibility::Collapsed) {
            if (oldCopyrightVisibilityBinding) container.SetBinding(x::UIElement::VisibilityProperty(),oldCopyrightVisibilityBinding);
            else RestoreValue(container,x::UIElement::VisibilityProperty(),oldCopyrightVisibility);
        }} catch (...) {}
        try { if (auto panel=expanded.get(); panel && locationOverlay) {
            uint32_t i; if (panel.Children().IndexOf(locationOverlay,i)) panel.Children().RemoveAt(i);
        }} catch (...) {}
        try { if (auto grid=host.get(); grid && badge) {
            uint32_t i; if (grid.Children().IndexOf(badge,i)) grid.Children().RemoveAt(i);
        }} catch (...) {}
        locationVisual=nullptr; circleVisual=nullptr; gate=nullptr;
        locationText=nullptr; copyrightText=nullptr; locationLink=nullptr; locationOverlay=nullptr;
        captionBlock=nullptr; headerBlock=nullptr; separator=nullptr;
        circle=nullptr; badge=nullptr;
    }
    ~Badge() { Dispose(); }

    static std::shared_ptr<Badge> Create(x::FrameworkElement const& owner,c::Grid const& host,
        x::FrameworkElement const& paint,x::FrameworkElement const& background,
        c::StackPanel const& panel=nullptr,c::TextBlock const& location=nullptr,
        c::Button const& title=nullptr,x::FrameworkElement const& feedback=nullptr,
        c::TextBlock const& copyright=nullptr,c::TextBlock const& prompt=nullptr,
        x::FrameworkElement const& creditGroup=nullptr) {
        auto state=std::make_shared<Badge>();
        state->owner=owner; state->host=host; state->paint=paint; state->backdrop=background;
        state->oldPaintOpacity=paint.ReadLocalValue(x::UIElement::OpacityProperty());
        state->badge=c::Grid(); state->badge.Name(L"Win10SpotlightBadge");
        state->badge.Width(32); state->badge.Height(32);
        state->badge.HorizontalAlignment(x::HorizontalAlignment::Left);
        state->badge.VerticalAlignment(x::VerticalAlignment::Top);
        state->badge.IsHitTestVisible(false);
        c::Canvas::SetZIndex(state->badge,10);
        state->circle=x::Shapes::Ellipse(); state->circle.Name(L"Win10SpotlightCircle");
        state->circle.Width(32); state->circle.Height(32);
        state->circle.Fill(m::SolidColorBrush(winrt::Windows::UI::Color{255,23,23,23}));
        state->badge.Children().Append(state->circle);
        c::TextBlock glyph;
        glyph.Text(title ? L"\uE722" : L"\uE721");
        glyph.FontFamily(m::FontFamily(L"/Assets/LockMDL2.ttf#Lock MDL2 Assets"));
        glyph.FontSize(16); glyph.LineHeight(16); glyph.Width(16); glyph.Height(16);
        glyph.Foreground(m::SolidColorBrush(winrt::Windows::UI::Color{255,255,255,255}));
        glyph.HorizontalAlignment(x::HorizontalAlignment::Center);
        glyph.VerticalAlignment(x::VerticalAlignment::Center);
        glyph.TextAlignment(x::TextAlignment::Center);
        state->badge.Children().Append(glyph);

        auto backgroundVisual=ElementCompositionPreview::GetElementVisual(background);
        auto compositor=backgroundVisual.Compositor();
        state->gate=compositor.CreatePropertySet(); state->UpdateGate();
        state->circleVisual=ElementCompositionPreview::GetElementVisual(state->circle);
        auto fade=compositor.CreateExpressionAnimation(
            L"idle + (hover-idle) * Clamp(background.Opacity,0.0,1.0) * state.Visible");
        fade.SetScalarParameter(L"idle",77.0f/255.0f);
        fade.SetScalarParameter(L"hover",153.0f/255.0f);
        fade.SetReferenceParameter(L"background",backgroundVisual);
        fade.SetReferenceParameter(L"state",state->gate);
        state->circleVisual.StartAnimation(L"Opacity",fade);
        state->circleAnimation=true;
        std::weak_ptr<Badge> weak=state;
        state->visibilityToken=background.RegisterPropertyChangedCallback(x::UIElement::VisibilityProperty(),
            [weak](auto const&,auto const&) noexcept { if (auto self=weak.lock()) self->UpdateGate(); });
        state->watchingVisibility=true;

        if (panel && location && title && feedback) {
            state->expanded=panel; state->sourceLocation=location; state->title=title; state->feedback=feedback;
            state->sourceCopyright=copyright; state->sourcePrompt=prompt;
            state->baselineMargin=feedback.Margin();
            state->oldFeedbackMargin=feedback.ReadLocalValue(x::FrameworkElement::MarginProperty());
            if (auto binding=feedback.GetBindingExpression(x::FrameworkElement::MarginProperty()))
                state->oldMarginBinding=binding.ParentBinding();
            state->locationOverlay=c::Canvas(); state->locationOverlay.Name(L"Win10SpotlightLocationOverlay");
            state->locationOverlay.Width(276); state->locationOverlay.Height(0);
            state->locationOverlay.HorizontalAlignment(x::HorizontalAlignment::Left);
            state->locationOverlay.VerticalAlignment(x::VerticalAlignment::Top);
            c::Canvas::SetZIndex(state->locationOverlay,10);
            state->locationText=c::TextBlock(); state->locationText.Name(L"Win10SpotlightLocationText");
            state->locationText.FontFamily(m::FontFamily(L"Segoe UI"));
            state->locationText.FontSize(24); state->locationText.LineHeight(0);
            state->locationText.FontWeight(winrt::Windows::UI::Text::FontWeights::Light());
            state->locationText.Margin({0,2,0,0});
            state->locationText.TextWrapping(x::TextWrapping::WrapWholeWords);
            state->locationText.TextAlignment(x::TextAlignment::Left);
            state->locationText.TextDecorations(winrt::Windows::UI::Text::TextDecorations::None);
            state->locationText.SetBinding(c::TextBlock::TextProperty(),SourceBinding(location,L"Text"));
            state->copyrightText=c::TextBlock(); state->copyrightText.Name(L"Win10SpotlightCopyrightText");
            state->copyrightText.FontFamily(m::FontFamily(L"Segoe UI"));
            state->copyrightText.FontSize(12); state->copyrightText.FontWeight(winrt::Windows::UI::Text::FontWeights::Normal());
            state->copyrightText.Margin({0,5,0,0});
            state->copyrightText.TextWrapping(x::TextWrapping::WrapWholeWords);
            state->copyrightText.TextAlignment(x::TextAlignment::Left);
            state->copyrightText.Foreground(m::SolidColorBrush(winrt::Windows::UI::Color{153,255,255,255}));
            if (copyright) state->copyrightText.SetBinding(c::TextBlock::TextProperty(),SourceBinding(copyright,L"Text"));
            state->captionBlock=c::StackPanel();
            state->captionBlock.Children().Append(state->locationText);
            state->captionBlock.Children().Append(state->copyrightText);
            state->locationLink=c::HyperlinkButton(); state->locationLink.Name(L"Win10SpotlightLocationLink");
            state->locationLink.MaxWidth(276);
            state->locationLink.HorizontalAlignment(x::HorizontalAlignment::Left);
            state->locationLink.VerticalAlignment(x::VerticalAlignment::Top);
            state->locationLink.MinWidth(0); state->locationLink.MinHeight(0);
            state->locationLink.Padding({8,4,8,4});
            state->locationLink.Foreground(m::SolidColorBrush(winrt::Windows::UI::Color{255,255,255,255}));
            state->locationLink.Template(x::Markup::XamlReader::Load(LR"(<ControlTemplate
                xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
                xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" TargetType="HyperlinkButton">
                <Border x:Name="HeaderBackdrop" Background="Transparent" Padding="{TemplateBinding Padding}">
                  <VisualStateManager.VisualStateGroups>
                    <VisualStateGroup x:Name="CommonStates">
                      <VisualState x:Name="Normal"/>
                      <VisualState x:Name="PointerOver"><VisualState.Setters>
                        <Setter Target="HeaderBackdrop.Background" Value="#33FFFFFF"/>
                      </VisualState.Setters></VisualState>
                      <VisualState x:Name="Pressed"><VisualState.Setters>
                        <Setter Target="HeaderBackdrop.Background" Value="#33FFFFFF"/>
                      </VisualState.Setters></VisualState>
                      <VisualState x:Name="Disabled"/>
                    </VisualStateGroup>
                  </VisualStateManager.VisualStateGroups>
                  <ContentPresenter Content="{TemplateBinding Content}" Foreground="{TemplateBinding Foreground}"
                      HorizontalAlignment="Left" VerticalAlignment="Top"/>
                </Border>
                </ControlTemplate>)").as<c::ControlTemplate>());
            state->locationLink.Content(state->captionBlock);
            state->locationLink.SetBinding(c::Control::IsEnabledProperty(),SourceBinding(title,L"IsEnabled"));
            state->separator=x::Shapes::Line(); state->separator.Name(L"Win10SpotlightSeparator");
            state->separator.X1(0); state->separator.X2(255);
            state->separator.Stroke(m::SolidColorBrush(winrt::Windows::UI::Color{255,255,255,255}));
            state->separator.StrokeThickness(1); state->separator.Opacity(0.7);
            state->separator.Margin({0,25,0,0});
            state->headerBlock=c::StackPanel(); state->headerBlock.Name(L"Win10SpotlightHeader");
            state->headerBlock.Width(276);
            state->headerBlock.Children().Append(state->locationLink);
            state->headerBlock.Children().Append(state->separator);
            state->locationOverlay.Children().Append(state->headerBlock);
            panel.Children().Append(state->locationOverlay); // native children keep their indices
            if (copyright) {
                auto container=creditGroup;
                uint32_t nativeIndex;
                if (container && panel.Children().IndexOf(container,nativeIndex)) {
                    state->copyrightContainer=container;
                    state->oldCopyrightVisibility=container.ReadLocalValue(x::UIElement::VisibilityProperty());
                    if (auto binding=container.GetBindingExpression(x::UIElement::VisibilityProperty()))
                        state->oldCopyrightVisibilityBinding=binding.ParentBinding();
                    state->changedCopyrightVisibility=true;
                    container.Visibility(x::Visibility::Collapsed);
                }
            }
            auto contentVisual=ElementCompositionPreview::GetElementVisual(panel);
            state->locationVisual=ElementCompositionPreview::GetElementVisual(state->headerBlock);
            // The parent already contributes E. Avoid a squared fade: output=min(B,E).
            auto locationFade=compositor.CreateExpressionAnimation(
                L"state.Visible * Clamp(background.Opacity / Max(content.Opacity,0.0001),0.0,1.0)");
            locationFade.SetReferenceParameter(L"state",state->gate);
            locationFade.SetReferenceParameter(L"background",backgroundVisual);
            locationFade.SetReferenceParameter(L"content",contentVisual);
            state->locationVisual.StartAnimation(L"Opacity",locationFade);
            state->locationAnimation=true;
            state->textToken=location.RegisterPropertyChangedCallback(c::TextBlock::TextProperty(),
                [weak](auto const&,auto const&) noexcept { if (auto self=weak.lock()) self->UpdateLocation(); });
            state->watchingText=true;
            if (copyright) {
                state->copyrightToken=copyright.RegisterPropertyChangedCallback(c::TextBlock::TextProperty(),
                    [weak](auto const&,auto const&) noexcept { if (auto self=weak.lock()) self->UpdateLocation(); });
                state->watchingCopyright=true;
            }
            state->layoutToken=state->locationOverlay.LayoutUpdated(
                [weak](auto const&,auto const&) noexcept { if (auto self=weak.lock()) self->PositionLocation(); });
            state->clickToken=state->locationLink.Click(
                [weak](auto const&,auto const&) noexcept { if (auto self=weak.lock()) self->InvokeLocation(); });
            state->UpdateLocation();
        }
        paint.Opacity(0);
        host.Children().Append(state->badge);
        return state;
    }
};

// A text glyph retains the installed MDL2 font's metrics and rendering. Append
// it after both native PathIcons, preserving their indices and native actions.
struct FeedbackIcon {
    winrt::weak_ref<c::Grid> host;
    c::TextBlock glyph{nullptr};
    struct Original {
        winrt::weak_ref<c::PathIcon> icon;
        f::IInspectable visibility{nullptr};
        x::Data::Binding binding{nullptr};
    };
    std::vector<Original> originals;
    void Dispose() noexcept {
        auto saved=std::move(originals);
        for (auto const& value:saved) try {
            if (auto icon=value.icon.get(); icon && icon.Visibility()==x::Visibility::Collapsed) {
                if (value.binding) icon.SetBinding(x::UIElement::VisibilityProperty(),value.binding);
                else RestoreValue(icon,x::UIElement::VisibilityProperty(),value.visibility);
            }
        } catch (...) {}
        try { if (auto grid=host.get(); grid && glyph) {
            uint32_t i; if (grid.Children().IndexOf(glyph,i)) grid.Children().RemoveAt(i);
        }} catch (...) {}
        glyph=nullptr;
    }
    ~FeedbackIcon() { Dispose(); }
    static std::unique_ptr<FeedbackIcon> Create(c::Grid const& grid,bool dislike) {
        auto state=std::make_unique<FeedbackIcon>(); state->host=grid;
        for (auto child:grid.Children()) if (auto icon=child.try_as<c::PathIcon>()) {
            Original original; original.icon=icon;
            original.visibility=icon.ReadLocalValue(x::UIElement::VisibilityProperty());
            if (auto expression=icon.GetBindingExpression(x::UIElement::VisibilityProperty()))
                original.binding=expression.ParentBinding();
            state->originals.push_back(std::move(original));
            icon.Visibility(x::Visibility::Collapsed);
        }
        if (state->originals.empty()) return nullptr;
        state->glyph=c::TextBlock(); state->glyph.Name(L"Win10FeedbackGlyph");
        state->glyph.Text(dislike ? L"\uEA92" : L"\uE006");
        state->glyph.FontFamily(m::FontFamily(L"Segoe MDL2 Assets"));
        state->glyph.FontSize(16); state->glyph.LineHeight(18);
        state->glyph.Width(16); state->glyph.Height(18);
        state->glyph.Foreground(m::SolidColorBrush(winrt::Windows::UI::Color{255,204,204,204}));
        state->glyph.HorizontalAlignment(x::HorizontalAlignment::Center);
        state->glyph.VerticalAlignment(x::VerticalAlignment::Center);
        state->glyph.IsHitTestVisible(false);
        grid.Children().Append(state->glyph);
        return state;
    }
};

struct Fade {
    winrt::weak_ref<x::FrameworkElement> target,source;
    comp::Visual visual{nullptr};
    bool started=false;
    void Dispose() noexcept {
        try { if (visual && started) {
            visual.StopAnimation(L"Opacity");
            if (auto node=target.get()) visual.Opacity(static_cast<float>(node.Opacity()));
        }} catch (...) {}
        started=false; visual=nullptr;
    }
    ~Fade() { Dispose(); }
};
struct Context : std::enable_shared_from_this<Context> {
    bool spotlight=false,fade=false,busy=false,stopping=false;
    unsigned attempts=0;
    x::DispatcherTimer timer{nullptr};
    winrt::event_token tick{};
    std::unordered_map<InstanceHandle,winrt::weak_ref<x::FrameworkElement>> nodes;
    std::unordered_map<InstanceHandle,std::shared_ptr<Badge>> badges;
    std::unordered_map<InstanceHandle,std::unique_ptr<Fade>> fades;
    std::unordered_map<InstanceHandle,std::unique_ptr<FeedbackIcon>> feedbackIcons;
    static std::shared_ptr<Context> Create(bool spotlight,bool fade) {
        auto self=std::make_shared<Context>(); self->spotlight=spotlight; self->fade=fade;
        self->timer=x::DispatcherTimer(); self->timer.Interval(std::chrono::milliseconds(250));
        std::weak_ptr<Context> weak=self;
        self->tick=self->timer.Tick([weak](auto const&,auto const&) noexcept {
            if (auto locked=weak.lock()) locked->Update();
        });
        return self;
    }
    void Stop() noexcept {
        if (stopping) return;
        stopping=true;
        try { timer.Stop(); timer.Tick(tick); } catch (...) {}
        // Detach containers before restoring XAML can re-enter diagnostics.
        auto oldBadges=std::move(badges); auto oldFades=std::move(fades); nodes.clear();
        auto oldIcons=std::move(feedbackIcons);
        for (auto& [_,state]:oldBadges) state->Dispose();
        for (auto& [_,state]:oldFades) state->Dispose();
        for (auto& [_,state]:oldIcons) state->Dispose();
    }
    ~Context() { Stop(); }
    void Schedule() { if (!busy && !stopping) { attempts=0; timer.Start(); } }
    void Remove(InstanceHandle handle) {
        auto badge=badges.extract(handle); auto fade=fades.extract(handle); nodes.erase(handle);
        auto icon=feedbackIcons.extract(handle);
        if (badge) badge.mapped()->Dispose();
        if (fade) fade.mapped()->Dispose();
        if (icon) icon.mapped()->Dispose();
    }
    bool InstallFeedbackIcon(InstanceHandle handle,x::FrameworkElement const& owner) {
        auto root=Child(owner,0).try_as<c::Grid>(); if (!root) return false;
        c::Grid host{nullptr};
        for (auto child:root.Children()) if (auto grid=child.try_as<c::Grid>()) {
            for (auto entry:grid.Children()) if (entry.try_as<c::PathIcon>()) { host=grid; break; }
            if (host) break;
        }
        if (!host) return false;
        if (auto it=feedbackIcons.find(handle); it!=feedbackIcons.end()) {
            if (it->second->host.get()==host) return true;
            auto old=feedbackIcons.extract(it); old.mapped()->Dispose();
        }
        auto state=FeedbackIcon::Create(host,owner.Name()==L"DislikeButton");
        if (!state) return false;
        if (stopping) { state->Dispose(); return true; }
        feedbackIcons.emplace(handle,std::move(state));
        return true;
    }
    bool InstallBadge(InstanceHandle handle,x::FrameworkElement const& owner) {
        auto host=Child(owner,0).try_as<c::Grid>(); if (!host) return false;
        if (auto it=badges.find(handle); it!=badges.end()) {
            if (it->second->host.get()==host && it->second->paint.get()) return true;
            auto old=badges.extract(it); old.mapped()->Dispose();
        }
        auto spot=Find(host,L"Spot"),background=Find(host,L"ExpandoBackground");
        if (!spot || !background) return false;
        bool info=winrt::get_class_name(owner)==L"LockApp.InfoHotspot";
        auto paint=info ? m::VisualTreeHelper::GetParent(spot).try_as<x::FrameworkElement>() : Child(Child(spot,0),0);
        if (!paint) return false;
        auto panel=info ? Find(host,L"ExpandedContentParent").try_as<c::StackPanel>() : nullptr;
        auto location=info ? Find(host,L"TitleText").try_as<c::TextBlock>() : nullptr;
        auto title=info ? Find(host,L"Title").try_as<c::Button>() : nullptr;
        auto feedback=info ? Find(host,L"FeedbackSpotArea") : nullptr;
        if (info && (!panel || !location || !title || !feedback)) return false;
        c::TextBlock copyright{nullptr},prompt{nullptr};
        x::FrameworkElement creditGroup{nullptr};
        if (info) {
            for (auto child:panel.Children()) if (auto stack=child.try_as<c::StackPanel>())
                for (auto entry:stack.Children()) if (auto text=entry.try_as<c::TextBlock>()) { copyright=text; creditGroup=stack; break; }
            auto header=m::VisualTreeHelper::GetParent(location).try_as<c::StackPanel>();
            if (header) for (auto child:header.Children())
                if (auto text=child.try_as<c::TextBlock>(); text && text!=location) { prompt=text; break; }
        }
        auto state=Badge::Create(owner,host,paint,background,panel,location,title,feedback,copyright,prompt,creditGroup);
        if (stopping) { state->Dispose(); return true; }
        badges.emplace(handle,std::move(state));
        Wh_Log(L"Windows 10 animated Spotlight attached: %s",owner.Name().c_str());
        return true;
    }
    bool InstallFade(InstanceHandle handle,x::FrameworkElement const& target) {
        auto clock=Find(Ancestor(target,L"LockScreenTextContent"),L"TimeAndDatePanel");
        if (!clock) return false;
        if (auto it=fades.find(handle); it!=fades.end()) {
            if (it->second->source.get()==clock && it->second->target.get()==target) return true;
            auto old=fades.extract(it); old.mapped()->Dispose();
        }
        auto state=std::make_unique<Fade>();
        auto source=ElementCompositionPreview::GetElementVisual(clock);
        state->visual=ElementCompositionPreview::GetElementVisual(target);
        state->source=clock; state->target=target;
        auto animation=state->visual.Compositor().CreateExpressionAnimation(L"clock.Opacity");
        animation.SetReferenceParameter(L"clock",source);
        state->visual.StartAnimation(L"Opacity",animation); state->started=true;
        fades.emplace(handle,std::move(state)); return true;
    }
    void Update() noexcept {
        if (busy || stopping) return;
        busy=true; bool pending=false;
        try {
            auto snapshot=nodes;
            for (auto const& [handle,weak]:snapshot) {
                if (stopping) break;
                auto node=weak.get(); if (!node) { Remove(handle); continue; }
                try {
                    auto type=winrt::get_class_name(node);
                    if (spotlight && (type==L"LockApp.Hotspot" || type==L"LockApp.InfoHotspot"))
                        pending|=!InstallBadge(handle,node);
                    if (spotlight && type==L"LockApp.LikeDislikeButton" &&
                        (node.Name()==L"LikeButton" || node.Name()==L"DislikeButton"))
                        pending|=!InstallFeedbackIcon(handle,node);
                    if (stopping) break;
                    auto name=node.Name();
                    if (fade && (name==L"WidgetCanvasPanel" || name==L"MediaControlsContainer"))
                        pending|=!InstallFade(handle,node);
                } catch (winrt::hresult_error const& e) {
                    pending=true;
                    if (!attempts) Wh_Log(L"Windows 10 helper waiting: %08X",e.code());
                }
            }
        } catch (...) { pending=true; }
        busy=false;
        if (!pending || stopping || ++attempts>=80) try { timer.Stop(); } catch (...) {}
    }
};
thread_local std::shared_ptr<Context> context;
} // namespace win10lock

void Win10Initialize() {
    bool spotlight=Wh_GetIntSetting(L"win10Spotlight")!=0;
    bool fade=Wh_GetIntSetting(L"syncDismissalFade")!=0;
    if (spotlight || fade) win10lock::context=win10lock::Context::Create(spotlight,fade);
}
void Win10Uninitialize() {
    auto old=std::move(win10lock::context); if (old) old->Stop();
}
void Win10TrackElement(InstanceHandle handle,winrt::Windows::UI::Xaml::FrameworkElement element) {
    auto ctx=win10lock::context; if (!ctx || ctx->stopping) return;
    auto type=winrt::get_class_name(element); auto name=element.Name();
    if (type==L"LockApp.Hotspot" || type==L"LockApp.InfoHotspot" || type==L"LockApp.LikeDislikeButton" ||
        name==L"WidgetCanvasPanel" || name==L"MediaControlsContainer") ctx->nodes[handle]=element;
    ctx->Schedule();
}
void Win10RemoveElement(InstanceHandle handle) {
    auto ctx=win10lock::context; if (!ctx || ctx->stopping) return;
    ctx->Remove(handle); ctx->Schedule();
}

