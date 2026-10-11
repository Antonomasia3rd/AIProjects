// Activation factories only: no StartupTask lookup/mutation, package operation,
// media-session request, account/network access, or resident application launch.
#define NOMINMAX
#include <windows.h>
#include "../dependencies/winrt_activation.h"
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Data.Xml.Dom.h>
#include <cstdio>
#include <thread>
#include <atomic>
#include <stdexcept>

template<typename Class, typename Interface>
static void CheckFactory(std::atomic<int>& checks)
{
    auto factory = aip::AcquireUncachedActivationFactory<Class, Interface>();
    if (!factory) throw std::runtime_error("Missing activation factory");
    ++checks;
}

int main()
{
    std::atomic<int> checks{0}, failures{0};
    try {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        for (int iteration = 0; iteration < 100; ++iteration) {
            // Populate the normal projection cache as Live Tile XML does.
            {
                winrt::Windows::Data::Xml::Dom::XmlDocument document;
                document.LoadXml(L"<tile/>");
                ++checks;
            }
            std::thread worker([&] {
                bool initialized = false;
                try {
                    winrt::init_apartment(winrt::apartment_type::multi_threaded);
                    initialized = true;
                    using namespace winrt::Windows::ApplicationModel;
                    using namespace winrt::Windows::Media::Control;
                    using namespace winrt::Windows::Storage::Streams;
                    CheckFactory<StartupTask, IStartupTaskStatics>(checks);
                    CheckFactory<GlobalSystemMediaTransportControlsSessionManager,
                        IGlobalSystemMediaTransportControlsSessionManagerStatics>(checks);
                    CheckFactory<DataReader, IDataReaderFactory>(checks);
                    {
                        using namespace winrt::Windows::Foundation;
                        auto factory = aip::AcquireUncachedActivationFactory<Uri, IUriRuntimeClassFactory>();
                        auto uri = factory.CreateUri(L"https://example.invalid/fixture");
                        if (uri.Host() != L"example.invalid") throw std::runtime_error("URI mismatch");
                        ++checks;
                    }
                } catch (const winrt::hresult_error& ex) {
                    std::printf("FAIL: worker HRESULT %08x\n", static_cast<unsigned>(ex.code().value));
                    ++failures;
                } catch (...) { ++failures; }
                if (initialized) winrt::uninit_apartment();
            });
            worker.join();
            // This is the ordering in the reported crash: MTA exit, then an
            // unrelated STA operation flushes cached projection factories.
            winrt::clear_factory_cache();
            ++checks;
        }
        bool missingInterface = false;
        try {
            aip::AcquireUncachedActivationFactory<winrt::Windows::Foundation::Uri,
                winrt::Windows::ApplicationModel::IStartupTaskStatics>();
        } catch (const winrt::hresult_error& ex) {
            missingInterface = ex.code().value == E_NOINTERFACE;
        }
        if (!missingInterface) ++failures;
        ++checks;
        winrt::uninit_apartment();
    } catch (const winrt::hresult_error& ex) {
        std::printf("FAIL: outer HRESULT %08x\n", static_cast<unsigned>(ex.code().value));
        ++failures;
    } catch (...) { ++failures; }
    std::printf("WinRT factory lifetime tests: %d checks, %d failures\n", checks.load(), failures.load());
    return failures ? 1 : 0;
}
