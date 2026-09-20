// Schema 4 supplements the diagnostic property chain with evaluated UI values.
namespace wuxa = winrt::Windows::UI::Xaml::Media::Animation;

std::wstring DumpColor(winrt::Windows::UI::Color c) {
    wchar_t color[16];
    swprintf_s(color, L"#%02X%02X%02X%02X", c.A, c.R, c.G, c.B);
    return color;
}

std::wstring DescribeDumpValue(wf::IInspectable const& obj) {
    if (!obj) return L"(null)";
    std::wostringstream out;
    out.imbue(std::locale::classic()); out << std::setprecision(9);
    if (auto v = obj.try_as<wf::IPropertyValue>()) {
        switch (v.Type()) {
        case wf::PropertyType::String: return std::wstring(v.GetString());
        case wf::PropertyType::Double: out << v.GetDouble(); return out.str();
        case wf::PropertyType::Single: out << v.GetSingle(); return out.str();
        case wf::PropertyType::Boolean: return v.GetBoolean() ? L"true" : L"false";
        case wf::PropertyType::Int32: out << v.GetInt32(); return out.str();
        case wf::PropertyType::UInt32: out << v.GetUInt32(); return out.str();
        default: break;
        }
    }
    if (auto value = obj.try_as<wf::IReference<wux::Thickness>>()) {
        auto v=value.Value(); out<<v.Left<<L','<<v.Top<<L','<<v.Right<<L','<<v.Bottom; return out.str();
    }
    if (auto value = obj.try_as<wf::IReference<wux::CornerRadius>>()) {
        auto v=value.Value(); out<<v.TopLeft<<L','<<v.TopRight<<L','<<v.BottomRight<<L','<<v.BottomLeft; return out.str();
    }
    if (auto value = obj.try_as<wf::IReference<winrt::Windows::UI::Text::FontWeight>>()) {
        out<<value.Value().Weight; return out.str();
    }
    if (auto family=obj.try_as<wuxm::FontFamily>()) return std::wstring(family.Source());
    out << winrt::get_class_name(obj).c_str();
    if (auto brush = obj.try_as<wuxm::Brush>()) {
        out << L" {Opacity=" << brush.Opacity();
        if (auto solid = obj.try_as<wuxm::SolidColorBrush>()) out << L", Color=" << DumpColor(solid.Color());
        if (auto acrylic = obj.try_as<wuxm::AcrylicBrush>()) {
            out << L", TintColor=" << DumpColor(acrylic.TintColor())
                << L", TintOpacity=" << acrylic.TintOpacity()
                << L", FallbackColor=" << DumpColor(acrylic.FallbackColor())
                << L", BackgroundSource=" << static_cast<int>(acrylic.BackgroundSource())
                << L", AlwaysUseFallback=" << (acrylic.AlwaysUseFallback() ? L"true" : L"false");
            if (auto lum = acrylic.TintLuminosityOpacity()) out << L", TintLuminosityOpacity=" << lum.Value();
            else out << L", TintLuminosityOpacity=Auto";
        }
        out << L"}";
    } else if (auto transform = obj.try_as<wuxm::TranslateTransform>()) {
        out << L" {X=" << transform.X() << L", Y=" << transform.Y() << L"}";
    } else if (auto transform = obj.try_as<wuxm::ScaleTransform>()) {
        out << L" {ScaleX=" << transform.ScaleX() << L", ScaleY=" << transform.ScaleY()
            << L", CenterX=" << transform.CenterX() << L", CenterY=" << transform.CenterY() << L"}";
    } else if (auto transform = obj.try_as<wuxm::CompositeTransform>()) {
        out << L" {TranslateX=" << transform.TranslateX() << L", TranslateY=" << transform.TranslateY()
            << L", ScaleX=" << transform.ScaleX() << L", ScaleY=" << transform.ScaleY()
            << L", Rotation=" << transform.Rotation() << L", CenterX=" << transform.CenterX()
            << L", CenterY=" << transform.CenterY() << L"}";
    } else if (auto bitmap = obj.try_as<wuxm::Imaging::BitmapImage>()) {
        auto uri = bitmap.UriSource();
        out << L" {Uri=" << (uri ? uri.RawUri().c_str() : L"") << L", PixelWidth=" << bitmap.PixelWidth()
            << L", PixelHeight=" << bitmap.PixelHeight() << L"}";
    }
    return out.str();
}

void AppendDumpNumber(std::wostringstream& out, double value) {
    if (std::isfinite(value)) out << value;
    else out << L"null";
}

std::wstring RuntimeDumpDetails(wf::IInspectable const& obj) {
    auto node = obj.try_as<wux::FrameworkElement>();
    if (!node) return L"null";
    std::wostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(9) << L"{\"Opacity\":";
    AppendDumpNumber(out, node.Opacity());
    out << L",\"Visibility\":" << static_cast<int>(node.Visibility());
    out << L",\"IsHitTestVisible\":" << (node.IsHitTestVisible() ? L"true" : L"false");
    out << L",\"RenderTransform\":";
    AppendJsonString(out, DescribeDumpValue(node.RenderTransform()));
    if (auto root = node.XamlRoot()) {
        out << L",\"rasterizationScale\":";
        AppendDumpNumber(out, root.RasterizationScale());
        out << L",\"rootWidth\":";
        AppendDumpNumber(out, root.Size().Width);
        out << L",\"rootHeight\":";
        AppendDumpNumber(out, root.Size().Height);
    }
    if (auto text = obj.try_as<wuxc::TextBlock>()) {
        out << L",\"Text\":"; AppendJsonString(out, text.Text().c_str());
        out << L",\"OpticalMarginAlignment\":" << static_cast<int>(text.OpticalMarginAlignment())
            << L",\"TextLineBounds\":" << static_cast<int>(text.TextLineBounds())
            << L",\"LineStackingStrategy\":" << static_cast<int>(text.LineStackingStrategy())
            << L",\"IsTextScaleFactorEnabled\":" << (text.IsTextScaleFactorEnabled() ? L"true" : L"false")
            << L",\"BaselineOffset\":";
        AppendDumpNumber(out, text.BaselineOffset());
    }
    if (auto grid = obj.try_as<wuxc::Grid>()) {
        out << L",\"Background\":"; AppendJsonString(out, DescribeDumpValue(grid.Background()));
        out << L",\"BorderBrush\":"; AppendJsonString(out, DescribeDumpValue(grid.BorderBrush()));
        auto radius = grid.CornerRadius();
        out << L",\"CornerRadius\":\"" << radius.TopLeft << L',' << radius.TopRight << L','
            << radius.BottomRight << L',' << radius.BottomLeft << L'\"';
    } else if (auto border = obj.try_as<wuxc::Border>()) {
        out << L",\"Background\":"; AppendJsonString(out, DescribeDumpValue(border.Background()));
        out << L",\"BorderBrush\":"; AppendJsonString(out, DescribeDumpValue(border.BorderBrush()));
        auto radius = border.CornerRadius();
        out << L",\"CornerRadius\":\"" << radius.TopLeft << L',' << radius.TopRight << L','
            << radius.BottomRight << L',' << radius.BottomLeft << L'\"';
    } else if (auto control = obj.try_as<wuxc::Control>()) {
        out << L",\"IsEnabled\":" << (control.IsEnabled() ? L"true" : L"false");
        out << L",\"Background\":"; AppendJsonString(out, DescribeDumpValue(control.Background()));
        out << L",\"BorderBrush\":"; AppendJsonString(out, DescribeDumpValue(control.BorderBrush()));
        auto radius = control.CornerRadius();
        out << L",\"CornerRadius\":\"" << radius.TopLeft << L',' << radius.TopRight << L','
            << radius.BottomRight << L',' << radius.BottomLeft << L'\"';
    } else if (auto presenter = obj.try_as<wuxc::ContentPresenter>()) {
        out << L",\"Background\":"; AppendJsonString(out, DescribeDumpValue(presenter.Background()));
        out << L",\"BorderBrush\":"; AppendJsonString(out, DescribeDumpValue(presenter.BorderBrush()));
        auto radius = presenter.CornerRadius();
        out << L",\"CornerRadius\":\"" << radius.TopLeft << L',' << radius.TopRight << L','
            << radius.BottomRight << L',' << radius.BottomLeft << L'\"';
    } else if (auto panel = obj.try_as<wuxc::Panel>()) {
        out << L",\"Background\":"; AppendJsonString(out, DescribeDumpValue(panel.Background()));
    } else if (auto shape = obj.try_as<winrt::Windows::UI::Xaml::Shapes::Shape>()) {
        out << L",\"Fill\":"; AppendJsonString(out, DescribeDumpValue(shape.Fill()));
    } else if (auto image = obj.try_as<wuxc::Image>()) {
        out << L",\"Source\":"; AppendJsonString(out, DescribeDumpValue(image.Source()));
    }
    try {
        auto visual = wux::Hosting::ElementCompositionPreview::GetElementVisual(node);
        auto opacity = visual.Opacity();
        auto offset = visual.Offset();
        out << L",\"compositionOpacity\":"; AppendDumpNumber(out, opacity);
        out << L",\"compositionOffset\":[";
        AppendDumpNumber(out, offset.x); out << L',';
        AppendDumpNumber(out, offset.y); out << L',';
        AppendDumpNumber(out, offset.z); out << L']';
    } catch (...) {}
    out << L"}";
    return out.str();
}

void DumpTimeline(std::wostringstream& out, wuxa::Timeline const& timeline, unsigned depth = 0) {
    if (!timeline || depth > 12) { out << L"null"; return; }
    out << L"{\"type\":"; AppendJsonString(out, winrt::get_class_name(timeline).c_str());
    out << L",\"targetName\":"; AppendJsonString(out, wuxa::Storyboard::GetTargetName(timeline).c_str());
    out << L",\"targetProperty\":"; AppendJsonString(out, wuxa::Storyboard::GetTargetProperty(timeline).c_str());
    auto duration = timeline.Duration();
    out << L",\"durationType\":" << static_cast<int>(duration.Type);
    if (duration.Type == wux::DurationType::TimeSpan)
        out << L",\"durationMs\":" << duration.TimeSpan.count() / 10000.0;
    if (auto begin = timeline.BeginTime()) out << L",\"beginMs\":" << begin.Value().count() / 10000.0;
    if (auto animation = timeline.try_as<wuxa::DoubleAnimation>()) {
        if (auto value = animation.From()) { out << L",\"from\":"; AppendDumpNumber(out, value.Value()); }
        if (auto value = animation.To()) { out << L",\"to\":"; AppendDumpNumber(out, value.Value()); }
        if (auto value = animation.By()) { out << L",\"by\":"; AppendDumpNumber(out, value.Value()); }
    }
    if (auto animation = timeline.try_as<wuxa::ColorAnimation>()) {
        if (auto value = animation.To()) { out << L",\"to\":"; AppendJsonString(out, DumpColor(value.Value())); }
    }
    if (auto animation = timeline.try_as<wuxa::DoubleAnimationUsingKeyFrames>()) {
        out << L",\"keyFrames\":[";
        bool first = true;
        for (auto const& key : animation.KeyFrames()) {
            if (!first) out << L','; first = false;
            out << L"{\"ms\":" << key.KeyTime().TimeSpan.count() / 10000.0 << L",\"value\":";
            AppendDumpNumber(out, key.Value()); out << L'}';
        }
        out << L']';
    }
    if (auto animation = timeline.try_as<wuxa::ObjectAnimationUsingKeyFrames>()) {
        out << L",\"keyFrames\":[";
        bool first = true;
        for (auto const& key : animation.KeyFrames()) {
            if (!first) out << L','; first = false;
            out << L"{\"ms\":" << key.KeyTime().TimeSpan.count() / 10000.0 << L",\"value\":";
            AppendJsonString(out, DescribeDumpValue(key.Value())); out << L'}';
        }
        out << L']';
    }
    if (auto storyboard = timeline.try_as<wuxa::Storyboard>()) {
        out << L",\"children\":[";
        bool first = true;
        for (auto const& child : storyboard.Children()) {
            if (!first) out << L','; first = false;
            DumpTimeline(out, child, depth + 1);
        }
        out << L']';
    }
    out << L'}';
}

std::wstring VisualStateDumpDetails(wux::FrameworkElement const& node) {
    std::wostringstream out;
    out.imbue(std::locale::classic());
    out << L'[';
    bool firstGroup = true;
    for (auto const& group : wux::VisualStateManager::GetVisualStateGroups(node)) {
        if (!firstGroup) out << L','; firstGroup = false;
        out << L"{\"name\":"; AppendJsonString(out, group.Name().c_str());
        out << L",\"current\":";
        AppendJsonString(out, group.CurrentState() ? group.CurrentState().Name().c_str() : L"");
        out << L",\"states\":[";
        bool first = true;
        for (auto const& state : group.States()) {
            if (!first) out << L','; first = false;
            AppendJsonString(out, state.Name().c_str());
        }
        out << L"],\"stateDetails\":[";
        first = true;
        for (auto const& state : group.States()) {
            if (!first) out << L','; first = false;
            out << L"{\"name\":"; AppendJsonString(out, state.Name().c_str());
            out << L",\"storyboard\":"; DumpTimeline(out, state.Storyboard());
            out << L",\"setters\":[";
            bool firstSetter = true;
            for (auto const& base : state.Setters()) {
                auto setter = base.try_as<wux::Setter>();
                if (!setter) continue;
                if (!firstSetter) out << L','; firstSetter = false;
                auto target = setter.Target();
                out << L"{\"targetPath\":";
                AppendJsonString(out, target && target.Path() ? target.Path().Path().c_str() : L"");
                out << L",\"target\":";
                auto fe = target && target.Target() ? target.Target().try_as<wux::FrameworkElement>() : nullptr;
                AppendJsonString(out, fe ? fe.Name().c_str() : L"");
                out << L",\"value\":"; AppendJsonString(out, DescribeDumpValue(setter.Value()));
                out << L'}';
            }
            out << L"]}";
        }
        out << L"],\"transitions\":[";
        first = true;
        for (auto const& transition : group.Transitions()) {
            if (!first) out << L','; first = false;
            out << L"{\"from\":"; AppendJsonString(out, transition.From().c_str());
            out << L",\"to\":"; AppendJsonString(out, transition.To().c_str());
            auto d = transition.GeneratedDuration();
            if (d.Type == wux::DurationType::TimeSpan) out << L",\"generatedDurationMs\":" << d.TimeSpan.count() / 10000.0;
            out << L",\"storyboard\":"; DumpTimeline(out, transition.Storyboard());
            out << L'}';
        }
        out << L"]}";
    }
    out << L']';
    return out.str();
}
