"""Assemble the standalone Windhawk source and cleaned preset from the supplied fork.

Usage: python tools/prepare_styler.py (reads the project's sources folder)
The resulting .wh.cpp has no dependency on this script or the .inc file.
"""
import argparse
import json
from pathlib import Path
from settings_formats import export_settings

ROOT = Path(__file__).resolve().parent.parent


def read_original_preset(path):
    # The supplied file uses only this flat target/styles subset of YAML.
    # This is deliberately not advertised as a general YAML parser.
    rules = []
    for line in path.read_text(encoding="utf-8-sig").splitlines():
        if line.startswith("  - target: "):
            rules.append({"target": line.split("target: ", 1)[1], "styles": []})
        elif line.startswith("      - "):
            value = line[8:]
            if value.startswith("'") and value.endswith("'"):
                value = value[1:-1].replace("''", "'")
            rules[-1]["styles"].append(value)
    if len(rules) < 40:
        raise ValueError("Unexpected original preferences format")
    return rules


def media_template(kind):
    # Replaces the Win11 button template, including its pointer/press animations,
    # while retaining the native Button/RepeatButton and live Content binding.
    return (
        f'<ControlTemplate TargetType="{kind}">'
        '<Grid x:Name="Win10MediaButtonRoot" Background="Transparent">'
        '<VisualStateManager.VisualStateGroups><VisualStateGroup x:Name="CommonStates">'
        '<VisualState x:Name="Normal"/>'
        '<VisualState x:Name="PointerOver"><VisualState.Setters>'
        '<Setter Target="Backdrop.Background" Value="#1AFFFFFF"/>'
        '</VisualState.Setters></VisualState>'
        '<VisualState x:Name="Pressed"><VisualState.Setters>'
        '<Setter Target="Backdrop.Background" Value="#33FFFFFF"/>'
        '</VisualState.Setters></VisualState>'
        '<VisualState x:Name="Disabled"><VisualState.Setters>'
        '<Setter Target="ContentPresenter.Opacity" Value="0.4"/>'
        '</VisualState.Setters></VisualState>'
        '</VisualStateGroup></VisualStateManager.VisualStateGroups>'
        '<Border x:Name="Backdrop" Background="Transparent" CornerRadius="0"/>'
        '<ContentPresenter x:Name="ContentPresenter" '
        'Content="{TemplateBinding Content}" ContentTemplate="{TemplateBinding ContentTemplate}" '
        'FontFamily="{TemplateBinding FontFamily}" FontSize="{TemplateBinding FontSize}" '
        'Foreground="{TemplateBinding Foreground}" '
        'HorizontalAlignment="Center" VerticalAlignment="Center"/>'
        '</Grid></ControlTemplate>'
    )


def make_preset(original):
    rules = []
    for rule in original:
        target = rule["target"]
        styles = list(rule["styles"])
        if target == "StackPanel#WidgetGroupPanel > * > TextBlock":
            continue
        if target.startswith(("LockApp.Hotspot", "LockApp.InfoHotspot")):
            styles = [s for s in styles if not s.startswith(("Opacity=", "Visibility=Visible"))]
        # Helper owns the replacement circle/glyph. Native child layout is kept,
        # but redundant opacity/visibility writes can fight native storyboards.
        if "SpotRectangle" in target or "PulseRect" in target:
            continue
        if "Spot" in target and target.endswith(" > TextBlock"):
            continue
        if target.endswith("Button#Spot > ContentPresenter"):
            continue
        if "ContentPresenter > Grid" in target and "Spot" in target:
            styles = [s for s in styles if not s.startswith(("Opacity=", "Visibility=Visible"))]
        if target.endswith(" > Grid > Grid#Spot"):
            styles = [s for s in styles if not s.startswith(("Opacity=", "Visibility=Visible"))]
        if target.endswith("StackPanel > ContentPresenter#ContentPresenter") and "InfoHotspot" in target:
            styles = [s for s in styles if not s.startswith(("Opacity=", "Visibility="))]
        if target == "TextBlock#Time" or target == "TextBlock#Date":
            styles = [s for s in styles if not s.startswith("TextAlignment=")]
            styles += ["TextAlignment=Left", "OpticalMarginAlignment=TrimSideBearings"]
        if target == "StackPanel#TimePanel":
            styles = ["HorizontalAlignment=Left"]
        if target in ["TextBlock#NetworkStatusBaseText", "TextBlock#NetworkStatusOverlayText", "TextBlock#NetworkStatusUnderlayText"]:
            styles += ["FontFamily=Segoe MDL2 Assets"]
        if target == "StackPanel#WidgetsContainer":
            # Preserve LockCanvas's live width/card-count decision.
            styles = [s for s in styles if not s.startswith("Width=")]
        if target == "StackPanel#WidgetGroupPanel":
            styles = [s for s in styles if not s.startswith("Height=")]
        if target == "Grid#MediaTransportControls":
            styles = [s for s in styles if not s.startswith("Background")]
            styles += [
                'Background:=<SolidColorBrush Color="#000000" Opacity="$mediaBackdropOpacity"/>',
                'ColumnDefinitions:=<ColumnDefinitionCollection><ColumnDefinition Width="Auto"/><ColumnDefinition Width="*"/></ColumnDefinitionCollection>',
            ]
        if target.endswith(" > ListView[3]"):
            styles += ["SelectionMode=None", "IsItemClickEnabled=False", "IsSwipeEnabled=False"]
        if target.endswith(" > Button#Spot"):
            styles = [s for s in styles if not s.startswith(("Background", "Opacity=", "Visibility="))]
            styles += ["Background=Transparent"]
        if "InfoHotspot" in target and target.endswith(" > Button#Title"):
            styles += ["MinHeight=37"]
        # Preserve the native source TextBlock and its bindings in place. The
        # helper displays a bound copy inside the native expanding panel.
        if target.endswith("StackPanel > TextBlock#TitleText") and "InfoHotspot" in target:
            target = "TextBlock#TitleText"
            styles = ["FontFamily=Segoe UI", "FontSize=15", "FontWeight=Normal", "LineHeight=20",
                      "TextAlignment=Left", "HorizontalAlignment=Left", "Margin=0", "TextWrapping=Wrap", "Visibility=Collapsed"]
        elif target.endswith("StackPanel > TextBlock[3]") and "InfoHotspot" in target:
            target = target.removesuffix("[3]")
        if "InfoHotspot" in target and target.endswith(" > Grid#RootGrid > Border#ButtonBackground"):
            # This border is animated by the original button template. Hiding it
            # removes the separate Win11 rounded hover patch, leaving Expando.
            styles = ["Visibility=Collapsed"]
        # Glyph Text setters below would sever bindings created by our template.
        if "Button#" in target and "ContentPresenter#ContentPresenter > TextBlock" in target:
            continue
        if target in ["Windows.UI.Xaml.Controls.Primitives.RepeatButton#PreviousButton",
                      "Button#PlayPauseButton", "Windows.UI.Xaml.Controls.Primitives.RepeatButton#NextButton"]:
            kind = "Button" if "PlayPause" in target else "RepeatButton"
            styles += ["Background=Transparent", "BorderBrush=Transparent", "BorderThickness=0",
                       "CornerRadius=0", "Template:=" + media_template(kind)]
            if "Previous" in target:
                styles += ["Content=\ue892"]
            elif "Next" in target:
                styles += ["Content=\ue893"]
        if styles:
            rules.append({"target": target, "styles": styles})

    rules += [
        {"target": "LockApp.Hotspot > Grid > Button#Expando > ContentPresenter > Grid > Border#Title > StackPanel > Border#ExpandedContentParent > StackPanel > TextBlock",
         "styles": ["FontFamily=Segoe UI", "TextAlignment=Left"]},
        {"target": "LockApp.Hotspot > Grid > Button#Expando > ContentPresenter > Grid > Border#Title > StackPanel > Border#ExpandedContentParent",
         "styles": ["HorizontalAlignment=Stretch"]},
        {"target": "Grid#MediaTransportControls > ListView > ItemsPresenter > StackPanel > ListViewItem > Windows.UI.Xaml.Controls.Primitives.ListViewItemPresenter",
         "styles": ["PointerOverBackground=Transparent", "PressedBackground=Transparent",
                    "SelectedBackground=Transparent", "SelectedPointerOverBackground=Transparent",
                    "SelectedPressedBackground=Transparent", "SelectionCheckMarkVisualEnabled=False"]},
        {"target": "LockApp.LikeDislikeButton > Grid#ButtonRoot > TextBlock", "styles": ["FontFamily=Segoe UI", "TextAlignment=Left"]},
        {"target": "LockApp.InfoHotspot#infoHotspot > Grid > Grid#Expando > StackPanel#ExpandedContentParent > StackPanel > TextBlock",
         "styles": ["FontFamily=Segoe UI", "TextAlignment=Left"]},
        {"target": "LockApp.LikeDislikeButton", "styles": [
            "FontFamily=Segoe UI", "FontSize=15", "FontWeight=Normal", "Padding=8,4,8,4",
            "Background=#33FFFFFF", "BorderBrush=Transparent", "CornerRadius=0"]},
        {"target": "LockApp.LikeDislikeButton#LikeButton", "styles": ["Margin=0,10,0,4"]},
        {"target": "LockApp.LikeDislikeButton#DislikeButton", "styles": ["Margin=0"]},
        {"target": "LockApp.LikeDislikeButton > Grid#ButtonRoot > Grid", "styles": [
            "Width=16", "Height=18", "Margin=0,9,8,0", "HorizontalAlignment=Center", "VerticalAlignment=Top"]},
        {"target": "LockApp.LikeDislikeButton > Grid#ButtonRoot > TextBlock[3]", "styles": [
            "FontFamily=Segoe UI", "FontSize=15", "FontWeight=Normal", "LineHeight=0",
            "Margin=0,7,8,2", "TextWrapping=WrapWholeWords", "Foreground=White"]},
        {"target": "LockApp.LikeDislikeButton > Grid#ButtonRoot > TextBlock[4]", "styles": [
            "FontFamily=Segoe UI", "FontSize=12", "FontWeight=Normal", "LineHeight=0",
            "Margin=0,0,8,8", "TextWrapping=WrapWholeWords", "Foreground=#99FFFFFF"]},
        {"target": "LockApp.LikeDislikeButton > Grid#ButtonRoot@CommonStates > Border", "styles": [
            "CornerRadius=0", "Background@Normal=Transparent", "Background@PointerOver=#33FFFFFF",
            "Background@Pressed=#33FFFFFF"]},
    ]
    return {"win10Spotlight": True, "syncDismissalFade": True, "controlStyles": rules,
            "styleConstants": ["mediaBackdropOpacity=0.6"], "themeResourceVariables": [""]}


def prepare_source(reference):
    source = (reference / "windows-11-lockapp-styler.wh.cpp").read_text(encoding="utf-8-sig")
    source = source.replace("// @name            Windows 11 Lock Screen Styler",
                            "// @name            Windows 11 Lock Screen Styler - Windows 10 layout\n// @version         1.0.3")
    source = source.replace("- controlStyles:", "- win10Spotlight: false\n  $name: Windows 10 Spotlight badges and location\n  $description: Use with the included Windows 10 preset. Keeps native links and live photo location.\n- syncDismissalFade: false\n  $name: Synchronize widget and media dismissal opacity\n  $description: Follow the native clock panel opacity on the compositor.\n- controlStyles:", 1)
    source = source.replace("void CleanupCustomizations(InstanceHandle handle);", """void CleanupCustomizations(InstanceHandle handle);
void Win10Initialize();
void Win10Uninitialize();
void Win10TrackElement(InstanceHandle, winrt::Windows::UI::Xaml::FrameworkElement);
void Win10RemoveElement(InstanceHandle);""", 1)
    source = source.replace("ApplyCustomizations(element.Handle, frameworkElement, element.Type);",
                            "ApplyCustomizations(element.Handle, frameworkElement, element.Type);\n            Win10TrackElement(element.Handle, frameworkElement);", 1)
    source = source.replace("CleanupCustomizations(element.Handle);", "Win10RemoveElement(element.Handle);\n        CleanupCustomizations(element.Handle);", 1)
    source = source.replace("void UninitializeForCurrentThread() {", (ROOT / "tools/win10_behavior.inc").read_text(encoding="utf-8") + "\n\nvoid UninitializeForCurrentThread() {\n    Win10Uninitialize();", 1)
    source = source.replace("ProcessResourceVariablesFromSettings();\n\n    g_initializedForThread = true;",
                            "ProcessResourceVariablesFromSettings();\n    Win10Initialize();\n\n    g_initializedForThread = true;", 1)
    source = source.replace('    std::wstring xaml =\n        LR"(<ResourceDictionary', '''    // The supplied older crash dump identifies UIElement_Translation here.
    // It is a facade property, not a dependency property; XAML's Setter parser
    // dereferences a null CDependencyProperty for it instead of returning HRESULT.
    for (auto unsafe : {L"Property=\\\"Translation\\\"", L"Property=\\\"UIElement.Translation\\\""}) {
        if (xamlStyleSetters.find(unsafe) != std::wstring_view::npos)
            throw std::runtime_error("Translation cannot be used in a XAML Setter. Use RenderTransform with TranslateTransform instead.");
    }
    std::wstring xaml =
        LR"(<ResourceDictionary''', 1)
    # Acquire the worker reference before launching it; the original fork could
    # Release on the worker before the creator's AddRef ran.
    source = source.replace("    HANDLE thread = CreateThread(", "    AddRef();\n    HANDLE thread = CreateThread(", 1)
    source = source.replace("    if (thread) {\n        AddRef();\n        CloseHandle(thread);\n    }",
                            "    if (thread) {\n        CloseHandle(thread);\n    } else {\n        Release();\n    }", 1)
    # The inherited notification-styler statistics job is unrelated to this fork.
    begin = source.index("PTP_TIMER g_statsTimer;")
    end = source.index("BOOL Wh_ModInit()", begin)
    source = source[:begin] + source[end:]
    source = source.replace("    if (g_target == Target::ShellExperienceHost) {\n        StartStatsTimer();\n    }\n", "")
    source = source.replace("    if (g_target == Target::ShellExperienceHost) {\n        StopStatsTimer();\n    }\n", "")
    source = source.replace("// ==WindhawkModReadme==\n/*\n\n*/", """// ==WindhawkModReadme==
/*
# Windows 10 lock screen layout

Use windows-10-lockscreen.advanced-settings.json in Advanced > Mod settings.
Or use windows-10-lockscreen.wh.preferences.yaml in the Settings tab's YAML editor.
Disable other XAML stylers targeting LockApp.exe while testing.
The helpers preserve native controls, their commands, and the live location text.
The source is based on the user's Windows 11 Lock Screen Styler fork and the
upstream Notification Center Styler (GPLv3). See README.md for evidence and
the remaining runtime validation steps. This is a test candidate, not a claim
of a verified pixel-perfect match on every Windows build or display scale.
*/""", 1)
    (ROOT / "builds").mkdir(exist_ok=True)
    (ROOT / "builds/windows-11-lockapp-styler.wh.cpp").write_text(source, encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference-dir", type=Path, default=ROOT / 'sources')
    args = parser.parse_args()
    prepare_source(args.reference_dir)
    original = read_original_preset(args.reference_dir / "windows-11-lockapp-styler.wh.preferences.mine.txt")
    preset = make_preset(original)
    export_settings(preset, ROOT / 'preferences', 'windows-10-lockscreen', 'windows-10-lockscreen.wh.preferences.yaml')
    print(f"Prepared standalone mod and {len(preset['controlStyles'])} style rules")


if __name__ == "__main__":
    main()
