#ifndef AIP_DEPENDENCIES_WINRT_ACTIVATION_H
#define AIP_DEPENDENCIES_WINRT_ACTIVATION_H

#include <winrt/base.h>

namespace aip {
// Short-lived apartments must not leave activation-factory proxies in the
// process-wide projection cache. An agile proxy can outlive its originating
// apartment while its proxy/stub DLL has already been unloaded. Keep the
// returned factory local, and destroy it before uninitializing that apartment.
template <typename Class, typename Interface>
inline auto AcquireUncachedActivationFactory()
{
    winrt::hresult_error error;
    auto factory = winrt::try_get_activation_factory<Class, Interface>(error);
    if (!factory) throw error;
    return factory;
}
}

#endif
