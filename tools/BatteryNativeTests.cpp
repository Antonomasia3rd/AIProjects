#define NOMINMAX
#define DESKTOPSTUB_INERT_HARDWARE
#include "../dependencies/content_sources/battery_win32.inc"
#include <iostream>
#include <stdexcept>
namespace {
using namespace aip::battery;using namespace aip::battery::native;
int checks=0,failures=0;
void Check(bool value,const char* label){++checks;if(!value){++failures;std::cout<<"FAIL: "<<label<<'\n';}}
SYSTEM_POWER_STATUS Status(){SYSTEM_POWER_STATUS status{};status.ACLineStatus=0;status.BatteryFlag=1;status.BatteryLifePercent=64;status.BatteryLifeTime=7200;status.BatteryFullLifeTime=MAXDWORD;return status;}
void SystemMapping(){
    auto status=Status();RawSnapshot raw;detail::ApplySystemStatus(status,raw);
    Check(raw.presence==Presence::Present&&raw.powerSource==PowerSource::DC&&raw.chargeState==ChargeState::Discharging,"DC system battery maps to present/discharging");
    Check(raw.percent==64&&raw.remainingSeconds==7200&&raw.saverEnabled==false,"documented charge, time and saver values are retained");
    status.ACLineStatus=1;status.BatteryFlag=8;status.SystemStatusFlag=1;raw={};detail::ApplySystemStatus(status,raw);
    Check(raw.powerSource==PowerSource::AC&&raw.chargeState==ChargeState::Charging&&raw.saverEnabled==true,"charging on AC and battery saver are independent flags");
    Check(!raw.remainingSeconds,"AC does not manufacture a remaining-time reading from a stale field");
    status.BatteryFlag=0;raw={};detail::ApplySystemStatus(status,raw);Check(raw.chargeState==ChargeState::NotCharging,"plugged-in battery can be present without charging");
    status.BatteryFlag=128;status.BatteryLifePercent=0;raw={};detail::ApplySystemStatus(status,raw);
    Check(raw.presence==Presence::Absent&&!raw.percent&&!raw.remainingSeconds&&raw.chargeState==ChargeState::Unknown,"no-system-battery flag dominates a zero percentage");
    status.ACLineStatus=255;status.BatteryFlag=255;status.BatteryLifePercent=255;status.SystemStatusFlag=255;raw={};detail::ApplySystemStatus(status,raw);
    Check(raw.presence==Presence::Unknown&&raw.powerSource==PowerSource::Unknown&&raw.chargeState==ChargeState::Unknown,"unknown flags do not pretend a battery is absent or charging");
    Check(!raw.percent&&!raw.remainingSeconds&&!raw.saverEnabled,"all unknown sentinels stay optional");
    status=Status();status.BatteryLifePercent=0;status.BatteryLifeTime=0;raw={};detail::ApplySystemStatus(status,raw);
    Check(raw.percent==0&&raw.remainingSeconds==0,"real zero charge and zero remaining seconds remain valid values");
    status.BatteryLifeTime=MAXDWORD;raw={};detail::ApplySystemStatus(status,raw);Check(!raw.remainingSeconds,"remaining-time MAXDWORD is unknown");
    status.BatteryLifePercent=101;raw={};detail::ApplySystemStatus(status,raw);Check(!raw.percent,"out-of-range charge is not clamped into plausible data");
    status.BatteryFlag=255;status.BatteryLifePercent=25;raw={};detail::ApplySystemStatus(status,raw);Check(raw.presence==Presence::Present&&raw.chargeState==ChargeState::Unknown,"valid percent proves presence without inventing charging state");
}
void DeviceMapping(){
    BATTERY_INFORMATION information{};information.Capabilities=BATTERY_SYSTEM_BATTERY;information.DesignedCapacity=60000;information.FullChargedCapacity=48000;
    BATTERY_STATUS status{};status.Capacity=12000;status.Rate=-9000;status.Voltage=11100;
    Device device;detail::ApplyDeviceInformation(information,&status,device);
    Check(device.designCapacityMWh==60000&&device.fullCapacityMWh==48000&&device.currentCapacityMWh==12000,"absolute capacities retain documented mWh units");
    Check(device.rateMilliwatts==-9000&&device.voltageMillivolts==11100&&!device.relativeCapacity,"signed discharge rate and millivolts are independent");
    information.Capabilities|=BATTERY_CAPACITY_RELATIVE;device={};detail::ApplyDeviceInformation(information,&status,device);
    Check(device.relativeCapacity&&!device.designCapacityMWh&&!device.fullCapacityMWh&&!device.currentCapacityMWh&&!device.rateMilliwatts,"relative units cannot become Wh/wear/milliwatts");
    Check(device.voltageMillivolts==11100,"relative capacity does not invalidate real voltage units");
    information.Capabilities=BATTERY_SYSTEM_BATTERY;information.DesignedCapacity=BATTERY_UNKNOWN_CAPACITY;information.FullChargedCapacity=0;
    status.Capacity=BATTERY_UNKNOWN_CAPACITY;status.Voltage=BATTERY_UNKNOWN_VOLTAGE;status.Rate=static_cast<LONG>(BATTERY_UNKNOWN_RATE);device={};detail::ApplyDeviceInformation(information,&status,device);
    Check(!device.designCapacityMWh&&!device.fullCapacityMWh&&!device.currentCapacityMWh&&!device.rateMilliwatts&&!device.voltageMillivolts,"device capacity/voltage/rate sentinels are absent values");
    information.DesignedCapacity=0;information.FullChargedCapacity=50000;status.Capacity=0;status.Rate=0;status.Voltage=0;device={};detail::ApplyDeviceInformation(information,&status,device);
    Check(!device.designCapacityMWh&&device.fullCapacityMWh==50000&&device.currentCapacityMWh==0&&device.rateMilliwatts==0&&!device.voltageMillivolts,"zero current capacity/rate are real but zero design/voltage are unusable");
    device={};detail::ApplyDeviceInformation(information,nullptr,device);Check(device.fullCapacityMWh==50000&&!device.currentCapacityMWh&&!device.rateMilliwatts,"a failed live-status query can preserve readable static capacity");
    Check(detail::Action(0)==CriticalAction::None&&detail::Action(2)==CriticalAction::Sleep&&detail::Action(3)==CriticalAction::Hibernate&&detail::Action(6)==CriticalAction::Shutdown,"critical stored action values use POWER_ACTION rather than display indexes");
    Check(detail::Action(PowerActionShutdown)==CriticalAction::Shutdown&&detail::Action(PowerActionReserved)==CriticalAction::Unknown&&detail::Action(PowerActionShutdownReset)==CriticalAction::Unknown&&detail::Action(PowerActionDisplayOff)==CriticalAction::Unknown,"reserved reboot and display-off values cannot be mislabeled as sleep or hibernate");
    Check(detail::TypedAction(REG_DWORD,sizeof(DWORD),2)==CriticalAction::Sleep&&detail::TypedAction(REG_DWORD,sizeof(DWORD),3)==CriticalAction::Hibernate&&detail::TypedAction(REG_DWORD,sizeof(DWORD),6)==CriticalAction::Shutdown,"typed action data decodes actual SettingValue rather than logical enumeration indexes");
    Check(detail::TypedAction(REG_SZ,sizeof(DWORD),3)==CriticalAction::Unknown&&detail::TypedAction(REG_DWORD,2,3)==CriticalAction::Unknown,"malformed or non-DWORD action data cannot turn into a plausible policy");
    DWORD requestedIndex=MAXDWORD;
    const auto possible=[&](DWORD index,DWORD& type,DWORD& bytes,DWORD& value){requestedIndex=index;type=REG_DWORD;bytes=sizeof(DWORD);const DWORD values[]={0,2,3,6};if(index>3)return false;value=values[index];return true;};
    Check(detail::ResolveActionIndex(1,possible)==CriticalAction::Sleep&&requestedIndex==1&&detail::ResolveActionIndex(2,possible)==CriticalAction::Hibernate&&requestedIndex==2&&detail::ResolveActionIndex(3,possible)==CriticalAction::Shutdown&&requestedIndex==3,"logical selection indexes resolve through actual possible-setting metadata before POWER_ACTION decoding");
    Check(detail::ResolveActionIndex(4,possible)==CriticalAction::Unknown&&detail::ResolveActionIndex(65,possible)==CriticalAction::Unknown,"missing or unbounded possible-setting indexes cannot be mistaken for raw action values");
    Check(detail::Deadline((std::numeric_limits<std::uint64_t>::max)()-2,10)==(std::numeric_limits<std::uint64_t>::max)(),"deadline arithmetic saturates rather than wrapping");
}
void InjectedCollector(){
    int systemCalls=0,policyCalls=0,deviceCalls=0;Hooks hooks;
    hooks.utcMilliseconds=[](){return std::uint64_t{123456};};
    hooks.systemStatus=[&](auto& status){++systemCalls;status=Status();return true;};
    hooks.policy=[&](auto& policy,const auto&){++policyCalls;policy.schemeName=L"Fixture balanced";policy.ac.criticalPercent=7;policy.dc.reservePercent=9;policy.dc.action=CriticalAction::Hibernate;};
    hooks.devices=[&](auto& devices,auto&,bool& complete,const auto&){++deviceCalls;complete=true;Device first;first.id=L"fixture-A";first.designCapacityMWh=60000;first.fullCapacityMWh=48000;first.currentCapacityMWh=12000;devices.push_back(first);Device second=first;second.id=L"fixture-B";devices.push_back(second);};
    const auto result=Collect({},&hooks);
    Check(systemCalls==1&&policyCalls==1&&deviceCalls==1&&result.sampledUtcMilliseconds==123456&&!result.canceled,"injected collector samples each source once without native fallback");
    Check(result.raw.devices.size()==2&&result.raw.devices[0].id!=result.raw.devices[1].id&&result.raw.deviceDetailsComplete,"separate battery identities and complete enumeration survive collection");
    Check(result.raw.policy.schemeName==L"Fixture balanced"&&result.raw.policy.ac.criticalPercent==7&&result.raw.policy.dc.reservePercent==9&&result.raw.policy.dc.action==CriticalAction::Hibernate,"active scheme and independent AC/DC policy survive collection");
    CollectOptions options;options.includeDevices=false;const auto detailsOff=Collect(options,&hooks);Check(deviceCalls==1&&systemCalls==2&&policyCalls==2&&!detailsOff.raw.deviceDetailsComplete,"details-off does not query battery devices or claim complete device totals");
    options.canceled=[](){return true;};const auto canceled=Collect(options,&hooks);Check(canceled.canceled&&systemCalls==2&&policyCalls==2&&deviceCalls==1,"pre-cancel skips every system/policy/device operation");
    options.canceled=[]()->bool{throw std::runtime_error("fixture");};Check(Collect(options,&hooks).canceled,"throwing cancellation predicate fails closed");
    options={};bool stop=false;hooks.systemStatus=[&](auto& status){status=Status();stop=true;return true;};options.canceled=[&](){return stop;};const auto midway=Collect(options,&hooks);
    Check(midway.canceled&&!midway.raw.percent&&midway.raw.devices.empty()&&policyCalls==2,"mid-collection cancellation discards partial readings and stops later stages");
    hooks.systemStatus=[](auto&)->bool{throw std::runtime_error("secret fixture driver detail");};options={};const auto failed=Collect(options,&hooks);
    Check(!failed.raw.diagnostic.empty()&&failed.raw.diagnostic.find(L"secret")==std::wstring::npos&&!failed.raw.percent,"collector exception yields a generic diagnostic with no arbitrary exception text");
    Hooks empty;empty.utcMilliseconds=[](){return std::uint64_t{10};};const auto absent=Collect({},&empty);
    Check(absent.raw.presence==Presence::Unknown&&absent.raw.devices.empty()&&!absent.raw.diagnostic.empty(),"missing injected hooks cannot fall through to hardware");
    const auto inert=Collect();Check(inert.raw.presence==Presence::Unknown&&inert.raw.devices.empty()&&inert.raw.diagnostic.find(L"inert")!=std::wstring::npos,"default validation path refuses native collection");
    hooks.systemStatus=[](auto& status){status=Status();return true;};hooks.devices=[](auto& devices,auto&,bool& complete,const auto&){complete=false;Device device;device.id=L"only-readable-battery";device.designCapacityMWh=60000;device.fullCapacityMWh=48000;device.currentCapacityMWh=12000;devices.push_back(device);};
    const auto partial=Collect({},&hooks);const auto aggregate=Aggregate(partial.raw);
    Check(!partial.raw.deviceDetailsComplete&&partial.raw.devices.size()==1&&partial.raw.percent==64,"partial device enumeration keeps readable individual data and independent system charge");
    Check(!aggregate.designCapacityMWh&&!aggregate.fullCapacityMWh&&!aggregate.currentCapacityMWh&&!aggregate.wearPercent,"partial device enumeration cannot publish a subset as total Wh/wear");
}
}
int main(){SystemMapping();DeviceMapping();InjectedCollector();std::cout<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;}
