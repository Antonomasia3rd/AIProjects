#include <windows.h>
#undef GetCurrentTime
#include <winrt/base.h>
#include <winrt/Windows.UI.Xaml.Hosting.h>
#include <winrt/Windows.UI.Xaml.Markup.h>
#include <winrt/Windows.UI.Xaml.h>
#include <iostream>

int main() {
    try {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        auto manager = winrt::Windows::UI::Xaml::Hosting::WindowsXamlManager::InitializeForCurrentThread();
        auto item = winrt::Windows::UI::Xaml::Markup::XamlReader::Load(
            LR"(<Grid xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" />)");
        std::cout << "XAML parser initialized\n";
        // Process exit disposes this isolated probe; it never touches LockApp.
        ExitProcess(0);
    } catch (winrt::hresult_error const& e) {
        std::wcerr << L"XAML host error " << std::hex << e.code().value << L": " << e.message().c_str() << L'\n';
        return 1;
    }
}
