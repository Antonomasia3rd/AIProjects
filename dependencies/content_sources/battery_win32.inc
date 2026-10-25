#ifndef AIP_CONTENT_SOURCES_BATTERY_WIN32_INC
#define AIP_CONTENT_SOURCES_BATTERY_WIN32_INC
#include "battery_model.h"
#include <windows.h>
#include <setupapi.h>
#include <poclass.h>
#include <powrprof.h>
#include <objbase.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <limits>
#include <vector>
#pragma comment(lib,"setupapi.lib")
#pragma comment(lib,"powrprof.lib")
#pragma comment(lib,"ole32.lib")
#pragma comment(lib,"uuid.lib")

namespace aip { namespace battery { namespace native {
// GUID_DEVICE_BATTERY from the Windows SDK's poclass.h. Some SDK/library
// combinations declare it without providing its definition in uuid.lib.
inline constexpr GUID batteryDeviceInterface={0x72631e54,0x78a4,0x11d0,{0xbc,0xf7,0x00,0xaa,0x00,0xb7,0xb3,0x2a}};
struct CollectOptions {
    std::uint32_t timeoutMilliseconds=1500,ioTimeoutMilliseconds=250;
    bool includeDevices=true;
    std::function<bool()> canceled;
};
struct Collection {RawSnapshot raw;std::uint64_t sampledUtcMilliseconds=0;bool canceled=false;};
// Supplying hooks replaces every hardware/policy operation, with no native
// fallback for an omitted hook. Fixtures can therefore exercise Collect itself.
struct Hooks {
    std::function<bool(SYSTEM_POWER_STATUS&)> systemStatus;
    std::function<void(Policy&,const CollectOptions&)> policy;
    std::function<void(std::vector<Device>&,std::wstring&,bool&,const CollectOptions&)> devices;
    std::function<std::uint64_t()> utcMilliseconds;
};
namespace detail {
inline std::uint64_t UtcMilliseconds(){
    const auto value=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    return value<0?0:static_cast<std::uint64_t>(value);
}
inline bool Canceled(const CollectOptions& options)noexcept{try{return options.canceled&&options.canceled();}catch(...){return true;}}
inline std::uint64_t Deadline(std::uint64_t now,std::uint32_t duration){
    const auto maximum=(std::numeric_limits<std::uint64_t>::max)();return now>maximum-duration?maximum:now+duration;
}
inline void Diagnostic(std::wstring& target,const wchar_t* text){
    if(target.size()>=1024)return;if(!target.empty())target+=L" ";target+=text;
}
inline void ApplySystemStatus(const SYSTEM_POWER_STATUS& status,RawSnapshot& raw){
    raw.powerSource=status.ACLineStatus==1?PowerSource::AC:status.ACLineStatus==0?PowerSource::DC:PowerSource::Unknown;
    if(status.SystemStatusFlag<=1)raw.saverEnabled=status.SystemStatusFlag!=0;
    if(status.BatteryFlag!=255){
        raw.presence=(status.BatteryFlag&128)!=0?Presence::Absent:Presence::Present;
        if(raw.presence==Presence::Present)raw.chargeState=(status.BatteryFlag&8)!=0?ChargeState::Charging:
            raw.powerSource==PowerSource::DC?ChargeState::Discharging:raw.powerSource==PowerSource::AC?ChargeState::NotCharging:ChargeState::Unknown;
    }
    if(raw.presence!=Presence::Absent&&status.BatteryLifePercent<=100){raw.percent=status.BatteryLifePercent;raw.presence=Presence::Present;}
    if(raw.presence==Presence::Present&&raw.powerSource==PowerSource::DC&&status.BatteryLifeTime!=MAXDWORD)
        raw.remainingSeconds=static_cast<std::int64_t>(status.BatteryLifeTime);
}
inline CriticalAction Action(DWORD value){
    // winnt.h specifies POWER_ACTION for GUID_BATTERY_DISCHARGE_ACTION_x.
    // These are stored values, not the 0..3 display/provisioning indexes.
    switch(value){case PowerActionNone:return CriticalAction::None;case PowerActionSleep:return CriticalAction::Sleep;
    case PowerActionHibernate:return CriticalAction::Hibernate;case PowerActionShutdown:case PowerActionShutdownOff:return CriticalAction::Shutdown;
    default:return CriticalAction::Unknown;}
}
inline CriticalAction TypedAction(DWORD type,DWORD bytes,DWORD value){return type==REG_DWORD&&bytes==sizeof(DWORD)?Action(value):CriticalAction::Unknown;}
template<class PossibleReader> CriticalAction ResolveActionIndex(DWORD index,PossibleReader read){
    if(index>64)return CriticalAction::Unknown;
    DWORD type=0,bytes=sizeof(DWORD),value=0;
    return read(index,type,bytes,value)?TypedAction(type,bytes,value):CriticalAction::Unknown;
}
inline void ApplyDeviceInformation(const BATTERY_INFORMATION& information,const BATTERY_STATUS* status,Device& device){
    device.relativeCapacity=(information.Capabilities&BATTERY_CAPACITY_RELATIVE)!=0;
    if(!device.relativeCapacity){
        if(information.DesignedCapacity&&information.DesignedCapacity!=BATTERY_UNKNOWN_CAPACITY)device.designCapacityMWh=information.DesignedCapacity;
        if(information.FullChargedCapacity&&information.FullChargedCapacity!=BATTERY_UNKNOWN_CAPACITY)device.fullCapacityMWh=information.FullChargedCapacity;
        if(status&&status->Capacity!=BATTERY_UNKNOWN_CAPACITY)device.currentCapacityMWh=status->Capacity;
        if(status&&static_cast<ULONG>(status->Rate)!=BATTERY_UNKNOWN_RATE)device.rateMilliwatts=static_cast<std::int64_t>(status->Rate);
    }
    if(status&&status->Voltage!=BATTERY_UNKNOWN_VOLTAGE&&status->Voltage)device.voltageMillivolts=status->Voltage;
}

// All following calls are read-only. The synchronous setup/policy/open APIs are
// worker-only; no Win32 API can promise a deadline for a malfunctioning driver.
inline bool Stopped(const CollectOptions& options,std::uint64_t deadline){return Canceled(options)||GetTickCount64()>=deadline;}
inline void ReadPolicy(Policy& policy,const CollectOptions& options,std::uint64_t deadline){
    if(Stopped(options,deadline))return;
    GUID* active=nullptr;if(PowerGetActiveScheme(nullptr,&active)!=ERROR_SUCCESS||!active)return;
    const GUID scheme=*active;LocalFree(active);
    wchar_t identifier[40]{};if(StringFromGUID2(scheme,identifier,40)>0)policy.schemeId=identifier;
    if(Stopped(options,deadline))return;
    DWORD size=0;
    if(PowerReadFriendlyName(nullptr,&scheme,nullptr,nullptr,nullptr,&size)==ERROR_SUCCESS&&size>=sizeof(wchar_t)&&size<=8192&&size%sizeof(wchar_t)==0){
        std::vector<wchar_t> name(size/sizeof(wchar_t)+1,L'\0');
        if(!Stopped(options,deadline)&&PowerReadFriendlyName(nullptr,&scheme,nullptr,nullptr,reinterpret_cast<PUCHAR>(name.data()),&size)==ERROR_SUCCESS)
            policy.schemeName.assign(name.data(),wcsnlen_s(name.data(),name.size()));
    }
    // Reserve GUID is documented but absent from older Windows SDK symbols.
    static const GUID reserve={0xf3c5027d,0xcd16,0x4930,{0xaa,0x6b,0x90,0xdb,0x84,0x4a,0x8f,0x00}};
    for(int index=0;index<2;++index){
        auto& side=index==0?policy.ac:policy.dc;
        const auto read=[&](const GUID& setting,DWORD& value){return !Stopped(options,deadline)&&
            (index==0?PowerReadACValueIndex(nullptr,&scheme,&GUID_BATTERY_SUBGROUP,&setting,&value):
                PowerReadDCValueIndex(nullptr,&scheme,&GUID_BATTERY_SUBGROUP,&setting,&value))==ERROR_SUCCESS;};
        DWORD value=0;if(read(GUID_BATTERY_DISCHARGE_LEVEL_0,value)&&value<=100)side.criticalPercent=static_cast<int>(value);
        if(read(reserve,value)&&value<=100)side.reservePercent=static_cast<int>(value);
        // Resolve the chosen logical index through the installed possible-value
        // metadata before interpreting POWER_ACTION. The enumeration keys
        // 0/1/2/3 and their SettingValue data 0/2/3/6 are different contracts.
        if(read(GUID_BATTERY_DISCHARGE_ACTION_0,value))side.action=ResolveActionIndex(value,
            [&](DWORD settingIndex,DWORD& type,DWORD& bytes,DWORD& data){return !Stopped(options,deadline)&&
                PowerReadPossibleValue(nullptr,&GUID_BATTERY_SUBGROUP,&GUID_BATTERY_DISCHARGE_ACTION_0,&type,settingIndex,
                    reinterpret_cast<PUCHAR>(&data),&bytes)==ERROR_SUCCESS;});
    }
}
struct DeviceHandle {
    HANDLE value=INVALID_HANDLE_VALUE;
    explicit DeviceHandle(HANDLE handle):value(handle){}
    ~DeviceHandle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
};
struct PendingRequest {
    std::shared_ptr<DeviceHandle> device;std::wstring path;
    OVERLAPPED overlapped{};std::array<unsigned char,128> input{},output{};
    ~PendingRequest(){if(overlapped.hEvent)CloseHandle(overlapped.hEvent);}
};
struct PendingState {std::mutex mutex;std::array<std::unique_ptr<PendingRequest>,16> requests;};
inline PendingState& Pending(){
    // A broken driver may ignore cancellation. Keep its buffers/OVERLAPPED and
    // handle alive until completion (or process teardown), bounded to 16 slots.
    // Deliberately no static destructor that could free an outstanding IRP.
    static auto* state=new PendingState;return *state;
}
inline void Reap(PendingState& pending){
    for(auto& request:pending.requests)if(request){DWORD bytes=0;
        if(GetOverlappedResult(request->device->value,&request->overlapped,&bytes,FALSE)||GetLastError()!=ERROR_IO_INCOMPLETE)request.reset();
    }
}
inline bool Outstanding(const PendingState& pending,const std::wstring& path){
    for(const auto& request:pending.requests)if(request&&request->path==path)return true;return false;
}
template<class Input,class Output> bool Query(PendingState& pending,const std::shared_ptr<DeviceHandle>& device,const std::wstring& path,
    DWORD code,const Input& input,Output& output,const CollectOptions& options,std::uint64_t deadline){
    static_assert(sizeof(Input)<=128&&sizeof(Output)<=128,"Battery query buffers are bounded");
    if(Stopped(options,deadline)){SetLastError(Canceled(options)?ERROR_OPERATION_ABORTED:ERROR_TIMEOUT);return false;}
    auto slot=std::find_if(pending.requests.begin(),pending.requests.end(),[](const auto& request){return !request;});
    if(slot==pending.requests.end()||Outstanding(pending,path)){SetLastError(ERROR_BUSY);return false;}
    auto request=std::make_unique<PendingRequest>();request->device=device;request->path=path;
    request->overlapped.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!request->overlapped.hEvent)return false;
    std::memcpy(request->input.data(),&input,sizeof(input));DWORD bytes=0;
    bool complete=DeviceIoControl(device->value,code,request->input.data(),static_cast<DWORD>(sizeof(input)),request->output.data(),
        static_cast<DWORD>(sizeof(output)),&bytes,&request->overlapped)!=FALSE;
    if(!complete&&GetLastError()!=ERROR_IO_PENDING)return false;
    if(!complete){
        const auto ioDeadline=(std::min)(deadline,Deadline(GetTickCount64(),(std::max)(1u,options.ioTimeoutMilliseconds)));
        while(!Stopped(options,ioDeadline)){
            const auto now=GetTickCount64();if(now>=ioDeadline)break;
            const auto left=ioDeadline-now;const auto wait=WaitForSingleObject(request->overlapped.hEvent,static_cast<DWORD>((std::min)(left,std::uint64_t{25})));
            if(wait==WAIT_OBJECT_0){complete=GetOverlappedResult(device->value,&request->overlapped,&bytes,FALSE)!=FALSE;break;}
            if(wait!=WAIT_TIMEOUT)break;
        }
        if(!complete){
            const auto failure=Canceled(options)?ERROR_OPERATION_ABORTED:GetTickCount64()>=ioDeadline?ERROR_TIMEOUT:GetLastError();
            // Even ERROR_NOT_FOUND from cancellation is not a completion fence.
            CancelIoEx(device->value,&request->overlapped);
            if(!GetOverlappedResult(device->value,&request->overlapped,&bytes,FALSE)&&GetLastError()==ERROR_IO_INCOMPLETE)*slot=std::move(request);
            SetLastError(failure);return false;
        }
    }
    if(bytes!=sizeof(output)){SetLastError(ERROR_INVALID_DATA);return false;}std::memcpy(&output,request->output.data(),sizeof(output));return true;
}
struct DeviceList {HDEVINFO value=INVALID_HANDLE_VALUE;~DeviceList(){if(value!=INVALID_HANDLE_VALUE)SetupDiDestroyDeviceInfoList(value);}};
inline void ReadDevices(std::vector<Device>& devices,std::wstring& diagnostic,bool& complete,const CollectOptions& options,std::uint64_t deadline){
    complete=false;
    auto& pending=Pending();std::unique_lock<std::mutex> lock(pending.mutex,std::try_to_lock);
    if(!lock.owns_lock()){Diagnostic(diagnostic,L"Battery device details are already being refreshed.");return;}Reap(pending);
    if(Stopped(options,deadline))return;
    DeviceList list;list.value=SetupDiGetClassDevsW(&batteryDeviceInterface,nullptr,nullptr,DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);
    if(list.value==INVALID_HANDLE_VALUE){Diagnostic(diagnostic,L"Windows battery device details are unavailable.");return;}
    complete=true;DWORD index=0;
    for(;index<16&&!Stopped(options,deadline);++index){
        SP_DEVICE_INTERFACE_DATA interfaceData{};interfaceData.cbSize=sizeof(interfaceData);
        if(!SetupDiEnumDeviceInterfaces(list.value,nullptr,&batteryDeviceInterface,index,&interfaceData)){
            if(GetLastError()!=ERROR_NO_MORE_ITEMS){complete=false;Diagnostic(diagnostic,L"Some battery devices could not be enumerated.");}break;}
        DWORD size=0;SetupDiGetDeviceInterfaceDetailW(list.value,&interfaceData,nullptr,0,&size,nullptr);
        if(GetLastError()!=ERROR_INSUFFICIENT_BUFFER||size<sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)||size>65536){complete=false;Diagnostic(diagnostic,L"A battery device path could not be read.");continue;}
        std::vector<unsigned char> storage(size+sizeof(wchar_t),0);auto* data=reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(storage.data());data->cbSize=sizeof(*data);
        if(!SetupDiGetDeviceInterfaceDetailW(list.value,&interfaceData,data,size,nullptr,nullptr)){complete=false;Diagnostic(diagnostic,L"A battery device path changed during collection.");continue;}
        const std::wstring path=data->DevicePath;
        if(Outstanding(pending,path)){complete=false;Diagnostic(diagnostic,L"A battery driver is still completing an earlier request.");continue;}
        const auto handle=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OVERLAPPED,nullptr);
        if(handle==INVALID_HANDLE_VALUE){complete=false;Diagnostic(diagnostic,L"A battery device could not be opened for read-only access.");continue;}
        DeviceHandle opened(handle);auto device=std::make_shared<DeviceHandle>(opened.value);opened.value=INVALID_HANDLE_VALUE;ULONG tag=0;const ULONG noWait=0;
        if(!Query(pending,device,path,IOCTL_BATTERY_QUERY_TAG,noWait,tag,options,deadline)){
            const auto failure=GetLastError();if(failure!=ERROR_FILE_NOT_FOUND&&failure!=ERROR_NO_SUCH_DEVICE){complete=false;Diagnostic(diagnostic,L"A battery slot's presence could not be determined.");}continue;}
        if(tag==0)continue;
        BATTERY_QUERY_INFORMATION query{};query.BatteryTag=tag;query.InformationLevel=BatteryInformation;BATTERY_INFORMATION information{};
        if(!Query(pending,device,path,IOCTL_BATTERY_QUERY_INFORMATION,query,information,options,deadline)){complete=false;Diagnostic(diagnostic,L"Some battery capacity details are unavailable.");continue;}
        // Match GetSystemPowerStatus: do not mix peripheral/short-term UPS data
        // into a laptop's system-battery Wh/wear aggregates.
        if(!(information.Capabilities&BATTERY_SYSTEM_BATTERY)||(information.Capabilities&BATTERY_IS_SHORT_TERM))continue;
        Device item;item.id=path;BATTERY_WAIT_STATUS request{};request.BatteryTag=tag;request.Timeout=0;BATTERY_STATUS status{};
        const bool hasStatus=Query(pending,device,path,IOCTL_BATTERY_QUERY_STATUS,request,status,options,deadline);
        if(!hasStatus&&(GetLastError()==ERROR_FILE_NOT_FOUND||GetLastError()==ERROR_NO_SUCH_DEVICE)){complete=false;Diagnostic(diagnostic,L"A battery changed during collection.");continue;}
        ApplyDeviceInformation(information,hasStatus?&status:nullptr,item);devices.push_back(std::move(item));
        if(!hasStatus)Diagnostic(diagnostic,L"A battery's current capacity, rate or voltage is unavailable.");
    }
    if(index==16){SP_DEVICE_INTERFACE_DATA extra{};extra.cbSize=sizeof(extra);
        if(SetupDiEnumDeviceInterfaces(list.value,nullptr,&batteryDeviceInterface,index,&extra)||GetLastError()!=ERROR_NO_MORE_ITEMS){complete=false;Diagnostic(diagnostic,L"The battery device count exceeded the collection limit.");}}
    if(GetTickCount64()>=deadline){complete=false;if(!Canceled(options))Diagnostic(diagnostic,L"Battery device collection reached its time budget; available system status is still shown.");}
}
} // namespace detail

inline Collection Collect(const CollectOptions& requested={},const Hooks* injected=nullptr){
    Collection result;result.raw.deviceDetailsComplete=false;CollectOptions options=requested;
    options.timeoutMilliseconds=(std::clamp)(options.timeoutMilliseconds,100u,10000u);
    options.ioTimeoutMilliseconds=(std::clamp)(options.ioTimeoutMilliseconds,25u,2000u);
    try{
        result.sampledUtcMilliseconds=injected&&injected->utcMilliseconds?injected->utcMilliseconds():detail::UtcMilliseconds();
        if(detail::Canceled(options)){result.canceled=true;return result;}
        Hooks nativeHooks;
        if(!injected){
#ifdef DESKTOPSTUB_INERT_HARDWARE
            result.raw.diagnostic=L"Native battery collection is disabled in inert validation.";return result;
#else
            const auto deadline=detail::Deadline(GetTickCount64(),options.timeoutMilliseconds);
            nativeHooks.systemStatus=[](SYSTEM_POWER_STATUS& status){return GetSystemPowerStatus(&status)!=FALSE;};
            nativeHooks.policy=[deadline](Policy& policy,const CollectOptions& policyOptions){detail::ReadPolicy(policy,policyOptions,deadline);};
            nativeHooks.devices=[deadline](std::vector<Device>& devices,std::wstring& diagnostic,bool& complete,const CollectOptions& deviceOptions){detail::ReadDevices(devices,diagnostic,complete,deviceOptions,deadline);};
            injected=&nativeHooks;
#endif
        }
        SYSTEM_POWER_STATUS status{};
        if(injected->systemStatus&&injected->systemStatus(status))detail::ApplySystemStatus(status,result.raw);
        else detail::Diagnostic(result.raw.diagnostic,L"Windows system power status is unavailable.");
        if(!detail::Canceled(options)&&injected->policy)injected->policy(result.raw.policy,options);
        result.raw.deviceDetailsComplete=false;
        if(options.includeDevices&&!detail::Canceled(options)&&injected->devices)injected->devices(result.raw.devices,result.raw.diagnostic,result.raw.deviceDetailsComplete,options);
        if(result.raw.presence==Presence::Unknown&&!result.raw.devices.empty())result.raw.presence=Presence::Present;
        result.canceled=detail::Canceled(options);
        if(result.canceled){result.raw=RawSnapshot{};result.raw.deviceDetailsComplete=false;result.raw.diagnostic=L"Battery refresh was canceled.";}
    }catch(...){result.raw=RawSnapshot{};result.raw.deviceDetailsComplete=false;result.raw.diagnostic=L"Battery collection could not complete.";}
    return result;
}
}}} // namespace aip::battery::native
#endif
