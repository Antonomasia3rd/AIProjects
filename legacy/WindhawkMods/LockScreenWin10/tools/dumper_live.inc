// All subscriptions and callbacks belong to the element's XAML thread.
// Callback work is bounded to one value and a queue insertion, never a property
// chain query or disk operation. No polling/render callback is used here.
struct LiveElement : std::enable_shared_from_this<LiveElement> {
    struct Hook {
        winrt::weak_ref<wux::DependencyObject> source;
        wux::DependencyProperty property{nullptr};
        int64_t token=0;
        void Revoke() noexcept { try { if (auto node=source.get()) node.UnregisterPropertyChangedCallback(property,token); } catch (...) {} }
    };
    struct StateHook {
        winrt::weak_ref<wux::VisualStateGroup> source;
        winrt::event_token changing{},changed{};
    };
    winrt::weak_ref<wux::FrameworkElement> element;
    InstanceHandle handle=0;
    DWORD threadId=0;
    std::atomic<bool> active{true};
    std::vector<Hook> hooks;
    std::unordered_map<std::wstring,std::vector<Hook>> nested;
    std::vector<StateHook> states;
    std::vector<std::pair<std::wstring,wux::DependencyProperty>> properties;
    winrt::event_token sizeToken{},loadedToken{},viewToken{};
    uint64_t startTick=0;

    std::wostringstream Record(const wchar_t* event) const {
        std::wostringstream out; out.imbue(std::locale::classic()); out<<std::setprecision(9);
        out<<L"{\"event\":\""<<event<<L"\",\"elapsedMs\":"<<GetTickCount64()-startTick<<L",\"handle\":"<<handle;
        return out;
    }
    void Stop() noexcept {
        if (!active.exchange(false)) return;
        for (auto& hook:hooks) hook.Revoke(); hooks.clear();
        for (auto& [_,list]:nested) for (auto& hook:list) hook.Revoke(); nested.clear();
        for (auto& state:states) try { if (auto group=state.source.get()) {
            group.CurrentStateChanging(state.changing); group.CurrentStateChanged(state.changed);
        }} catch (...) {}
        states.clear();
        try { if (auto node=element.get()) {
            if (sizeToken.value) node.SizeChanged(sizeToken);
            if (loadedToken.value) node.Loaded(loadedToken);
            if (viewToken.value) node.as<wuxc::ScrollViewer>().ViewChanged(viewToken);
        }} catch (...) {}
    }
    ~LiveElement() { Stop(); }
    void GeometryChanged() noexcept {
        if (!active || !g_writer.HasOutput() || !g_settings.includeRectangle) return;
        try {
            auto node=element.get(); if (!node) return;
            auto bounds=node.TransformToVisual(nullptr).TransformBounds({0,0,static_cast<float>(node.ActualWidth()),static_cast<float>(node.ActualHeight())});
            auto out=Record(L"geometryChanged");
            out<<L",\"rectangle\":{\"x\":"; AppendDumpNumber(out,bounds.X);
            out<<L",\"y\":"; AppendDumpNumber(out,bounds.Y);
            out<<L",\"width\":"; AppendDumpNumber(out,bounds.Width);
            out<<L",\"height\":"; AppendDumpNumber(out,bounds.Height); out<<L"}}";
            g_writer.Write(out.str());
        } catch (...) {}
    }
    void Changed(std::wstring const& name,wux::DependencyProperty const& property,bool replaceNested) noexcept {
        if (!active || !g_writer.HasOutput()) return;
        try {
            auto node=element.get(); if (!node) return;
            auto value=node.GetValue(property);
            auto out=Record(L"propertyChanged"); out<<L",\"property\":"; AppendJsonString(out,name);
            out<<L",\"value\":"; AppendJsonString(out,DescribeDumpValue(value)); out<<L'}';
            g_writer.Write(out.str());
            if (replaceNested) WatchNested(name,property,value);
        } catch (...) {}
    }
    void WatchNested(std::wstring const& name,wux::DependencyProperty const& ownerProperty,wf::IInspectable const& value) {
        auto old=nested.extract(name); if (old) for (auto& hook:old.mapped()) hook.Revoke();
        auto object=value ? value.try_as<wux::DependencyObject>() : nullptr;
        if (!object) return;
        std::vector<wux::DependencyProperty> list;
        if (object.try_as<wuxm::Brush>()) list.push_back(wuxm::Brush::OpacityProperty());
        if (object.try_as<wuxm::SolidColorBrush>()) list.push_back(wuxm::SolidColorBrush::ColorProperty());
        if (object.try_as<wuxm::AcrylicBrush>()) {
            list.insert(list.end(),{wuxm::AcrylicBrush::TintColorProperty(),wuxm::AcrylicBrush::TintOpacityProperty(),
                wuxm::AcrylicBrush::TintLuminosityOpacityProperty(),wuxm::XamlCompositionBrushBase::FallbackColorProperty(),
                wuxm::AcrylicBrush::BackgroundSourceProperty(),wuxm::AcrylicBrush::AlwaysUseFallbackProperty()});
        }
        if (object.try_as<wuxm::TranslateTransform>()) list.insert(list.end(),{wuxm::TranslateTransform::XProperty(),wuxm::TranslateTransform::YProperty()});
        if (object.try_as<wuxm::CompositeTransform>()) list.insert(list.end(),{wuxm::CompositeTransform::TranslateXProperty(),
            wuxm::CompositeTransform::TranslateYProperty(),wuxm::CompositeTransform::ScaleXProperty(),
            wuxm::CompositeTransform::ScaleYProperty(),wuxm::CompositeTransform::RotationProperty()});
        std::weak_ptr<LiveElement> weak=shared_from_this();
        for (auto const& property:list) {
            auto token=object.RegisterPropertyChangedCallback(property,[weak,name,ownerProperty](auto const&,auto const&) noexcept {
                if (auto self=weak.lock()) self->Changed(name,ownerProperty,false);
            });
            nested[name].push_back({object,property,token});
        }
    }
    void Watch(std::wstring name,wux::DependencyProperty const& property) {
        auto node=element.get(); if (!node) return;
        std::weak_ptr<LiveElement> weak=shared_from_this();
        auto token=node.RegisterPropertyChangedCallback(property,[weak,name](auto const&,auto const& dp) noexcept {
            if (auto self=weak.lock()) self->Changed(name,dp,true);
        });
        hooks.push_back({node,property,token}); properties.emplace_back(name,property);
        WatchNested(name,property,node.GetValue(property));
    }
    void WatchStates() {
        if (!active || !g_settings.includeVisualStates) return;
        auto node=element.get(); if (!node) return;
        std::weak_ptr<LiveElement> weak=shared_from_this();
        for (auto group:wux::VisualStateManager::GetVisualStateGroups(node)) {
            if (std::any_of(states.begin(),states.end(),[&](auto const& old){return old.source.get()==group;})) continue;
            auto callback=[weak,name=std::wstring(group.Name())](bool changing,wux::VisualStateChangedEventArgs const& args) noexcept {
                auto self=weak.lock(); if (!self || !self->active || !g_writer.HasOutput()) return;
                try {
                    auto out=self->Record(changing ? L"stateChanging" : L"stateChanged");
                    out<<L",\"group\":"; AppendJsonString(out,name);
                    out<<L",\"oldState\":"; AppendJsonString(out,args.OldState() ? args.OldState().Name().c_str() : L"");
                    out<<L",\"newState\":"; AppendJsonString(out,args.NewState() ? args.NewState().Name().c_str() : L"");
                    out<<L'}'; g_writer.Write(out.str());
                } catch (...) {}
            };
            StateHook hook; hook.source=group;
            hook.changing=group.CurrentStateChanging([callback](auto const&,auto const& args){callback(true,args);});
            hook.changed=group.CurrentStateChanged([callback](auto const&,auto const& args){callback(false,args);});
            states.push_back(hook);
        }
    }
    static std::shared_ptr<LiveElement> Create(wux::FrameworkElement const& node,InstanceHandle handle,uint64_t startTick) {
        if (!node.Dispatcher().HasThreadAccess()) throw winrt::hresult_wrong_thread();
        auto self=std::make_shared<LiveElement>(); self->element=node; self->handle=handle;
        self->threadId=GetCurrentThreadId(); self->startTick=startTick;
        // Watch inherited public appearance/input properties; private CLR or
        // composition-only properties have no general DP notification contract.
        #define WATCH(type, property) self->Watch(L## #property,type::property##Property())
        WATCH(wux::UIElement,Opacity); WATCH(wux::UIElement,Visibility); WATCH(wux::UIElement,IsHitTestVisible);
        WATCH(wux::UIElement,RenderTransform); WATCH(wux::FrameworkElement,Width); WATCH(wux::FrameworkElement,Height);
        self->Watch(L"Grid.Row",wuxc::Grid::RowProperty()); self->Watch(L"Grid.Column",wuxc::Grid::ColumnProperty());
        self->Watch(L"Grid.RowSpan",wuxc::Grid::RowSpanProperty()); self->Watch(L"Grid.ColumnSpan",wuxc::Grid::ColumnSpanProperty());
        WATCH(wux::FrameworkElement,Margin); WATCH(wux::FrameworkElement,HorizontalAlignment); WATCH(wux::FrameworkElement,VerticalAlignment);
        if (node.try_as<wuxc::TextBlock>()) {
            WATCH(wuxc::TextBlock,Text); WATCH(wuxc::TextBlock,FontFamily); WATCH(wuxc::TextBlock,FontSize);
            WATCH(wuxc::TextBlock,FontWeight); WATCH(wuxc::TextBlock,Foreground); WATCH(wuxc::TextBlock,TextAlignment);
            WATCH(wuxc::TextBlock,TextWrapping); WATCH(wuxc::TextBlock,LineHeight);
        }
        if (node.try_as<wuxc::Control>()) {
            WATCH(wuxc::Control,IsEnabled); WATCH(wuxc::Control,Background); WATCH(wuxc::Control,Foreground);
            WATCH(wuxc::Control,FontFamily); WATCH(wuxc::Control,FontSize); WATCH(wuxc::Control,FontWeight);
            WATCH(wuxc::Control,Padding); WATCH(wuxc::Control,CornerRadius); WATCH(wuxc::Control,BorderBrush);
            WATCH(wuxc::Control,BorderThickness);
        } else if (node.try_as<wuxc::Grid>()) {
            WATCH(wuxc::Panel,Background); WATCH(wuxc::Grid,CornerRadius); WATCH(wuxc::Grid,BorderBrush);
            WATCH(wuxc::Grid,BorderThickness); WATCH(wuxc::Grid,Padding);
        } else if (node.try_as<wuxc::Border>()) {
            WATCH(wuxc::Border,Background); WATCH(wuxc::Border,CornerRadius); WATCH(wuxc::Border,BorderBrush);
            WATCH(wuxc::Border,BorderThickness); WATCH(wuxc::Border,Padding);
        } else if (node.try_as<wuxc::ContentPresenter>()) {
            WATCH(wuxc::ContentPresenter,Background); WATCH(wuxc::ContentPresenter,CornerRadius);
            WATCH(wuxc::ContentPresenter,BorderBrush); WATCH(wuxc::ContentPresenter,Padding);
        } else if (node.try_as<wuxc::Panel>()) WATCH(wuxc::Panel,Background);
        if (node.try_as<wuxc::Image>()) WATCH(wuxc::Image,Source);
        if (node.try_as<wuxc::TextBox>()) WATCH(wuxc::TextBox,Text);
        if (node.try_as<wuxc::ContentControl>()) WATCH(wuxc::ContentControl,Content);
        if (node.try_as<wuxc::PathIcon>()) WATCH(wuxc::PathIcon,Data);
        if (node.try_as<winrt::Windows::UI::Xaml::Shapes::Shape>()) {
            WATCH(winrt::Windows::UI::Xaml::Shapes::Shape,Fill); WATCH(winrt::Windows::UI::Xaml::Shapes::Shape,Stroke);
        }
        #undef WATCH
        self->WatchStates();
        std::weak_ptr<LiveElement> weak=self;
        self->sizeToken=node.SizeChanged([weak](auto const&,auto const&) noexcept { if (auto self=weak.lock()) self->GeometryChanged(); });
        self->loadedToken=node.Loaded([weak](auto const&,auto const&) noexcept {
            if (auto self=weak.lock()) try { self->WatchStates(); self->GeometryChanged(); } catch (...) {}
        });
        if (auto scroll=node.try_as<wuxc::ScrollViewer>())
            self->viewToken=scroll.ViewChanged([weak](auto const& sender,auto const& args) noexcept {
                auto self=weak.lock(); if (!self || !self->active || !g_writer.HasOutput()) return;
                try {
                    auto view=sender.template as<wuxc::ScrollViewer>();
                    auto out=self->Record(L"viewChanged");
                    out<<L",\"horizontalOffset\":"; AppendDumpNumber(out,view.HorizontalOffset());
                    out<<L",\"verticalOffset\":"; AppendDumpNumber(out,view.VerticalOffset());
                    out<<L",\"zoomFactor\":"; AppendDumpNumber(out,view.ZoomFactor());
                    out<<L",\"intermediate\":"<<(args.IsIntermediate() ? L"true" : L"false")<<L'}';
                    g_writer.Write(out.str());
                } catch (...) {}
            });
        return self;
    }
    std::wstring Baseline(InstanceHandle parent,unsigned childIndex,std::wstring const& path) {
        auto node=element.get(); auto out=Record(L"liveElement");
        out<<L",\"parent\":"<<parent<<L",\"childIndex\":"<<childIndex;
        out<<L",\"name\":"; AppendJsonString(out,node.Name().c_str());
        out<<L",\"runtimeType\":"; AppendJsonString(out,winrt::get_class_name(node).c_str());
        out<<L",\"path\":"; AppendJsonString(out,path);
        out<<L",\"properties\":[";
        bool first=true; for (auto const& [name,property]:properties) {
            if (!first) out<<L','; first=false;
            out<<L"{\"name\":"; AppendJsonString(out,name);
            out<<L",\"value\":"; AppendJsonString(out,DescribeDumpValue(node.GetValue(property))); out<<L'}';
        }
        out<<L"],\"effective\":"<<RuntimeDumpDetails(node);
        // Runtime setter targets may not be publicly resolvable. Live baseline
        // needs current group/state names, not another storyboard traversal.
        if (g_settings.includeVisualStates) {
            out<<L",\"visualStateGroups\":[";
            bool firstGroup=true;
            for (auto const& state:states) if (auto group=state.source.get()) {
                if (!firstGroup) out<<L','; firstGroup=false;
                out<<L"{\"name\":"; AppendJsonString(out,group.Name().c_str());
                out<<L",\"current\":"; AppendJsonString(out,group.CurrentState() ? group.CurrentState().Name().c_str() : L"");
                out<<L",\"states\":["; bool firstState=true;
                for (auto entry:group.States()) {
                    if (!firstState) out<<L','; firstState=false; AppendJsonString(out,entry.Name().c_str());
                }
                out<<L"]}";
            }
            out<<L']';
        }
        out<<L'}'; return out.str();
    }
};
