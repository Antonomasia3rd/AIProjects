#ifndef AIP_DESKTOPSTUB_PROFILE_MIGRATION_INC
#define AIP_DESKTOPSTUB_PROFILE_MIGRATION_INC
#include "desktopstub_profiles_native.inc"
namespace aip { namespace profiles { namespace native {
enum class MigrationPhase {Prepared=0,AwaitingSetup=1,Complete=2,RolledBack=3};
struct Migration {
    ProfileEntry entry;IdentityRecord before,after;MigrationPhase phase=MigrationPhase::Prepared;
    std::wstring operation,backup,oldRuntime,newRuntime,capturedText;
};
inline bool SameRecord(const IdentityRecord& a,const IdentityRecord& b){return a.id==b.id&&a.ownerSid==b.ownerSid&&a.profileKey==b.profileKey&&a.mode==b.mode&&a.preservedLegacy==b.preservedLegacy&&Same(a.package,b.package);}
inline std::wstring MigrationPath(const ProfileEntry& entry){return entry.iniPath+L".profile-identity-migration";}
inline bool ApplyIdentity(std::wstring& text,const IdentityRecord& record,const std::wstring& managed,std::wstring& error){
    if(!Valid(record)){error=L"The migration identity is invalid.";return false;}
    for(const auto& field:IdentityValues(record))if(!aip::WriteIniValueToText(text,L"ProfileIdentity",field.first.c_str(),field.second))return false;
    const bool fixed=record.mode==IdentityMode::Static;
    for(const auto& setting:std::vector<std::pair<std::wstring,std::wstring>>{{L"IdentityMode",fixed?L"Static":L"Auto"},{L"StaticPackageName",fixed?record.package.name:L""},{L"StaticPublisher",fixed?record.package.publisher:L""},{L"ManagedRuntime",managed}})
        if(!aip::WriteIniValueToText(text,L"Profile",setting.first.c_str(),setting.second))return false;
    return aip::WriteIniValueToText(text,L"Settings",L"ManifestIdentityName",fixed?record.package.name:L"")&&
        aip::WriteIniValueToText(text,L"Settings",L"ManifestPublisher",fixed?record.package.publisher:L"CN={ProductRuntimeBaseName}");
}
inline std::wstring SerializeMigration(const Context& context,const Migration& migration){
    std::wostringstream out;out<<L"AIP_PROFILE_IDENTITY_MIGRATION_1\n"<<static_cast<int>(migration.phase)<<L' '<<std::quoted(context.ownerSid)<<L' '<<std::quoted(context.installScope)<<L'\n'
        <<std::quoted(migration.operation)<<L' '<<std::quoted(migration.entry.id)<<L' '<<std::quoted(migration.entry.name)<<L' '<<std::quoted(migration.entry.iniPath)<<L' '<<std::quoted(migration.entry.profileKey)<<L'\n'
        <<std::quoted(migration.before.package.name)<<L' '<<std::quoted(migration.before.package.publisher)<<L' '<<static_cast<int>(migration.before.mode)<<L' '<<migration.before.preservedLegacy<<L'\n'
        <<std::quoted(migration.after.package.name)<<L' '<<std::quoted(migration.after.package.publisher)<<L' '<<static_cast<int>(migration.after.mode)<<L'\n'
        <<std::quoted(migration.backup)<<L' '<<std::quoted(migration.oldRuntime)<<L' '<<std::quoted(migration.newRuntime)<<L'\n';return out.str();
}
inline bool ParseMigration(const Context& context,const ProfileEntry& entry,const std::wstring& text,Migration& result,std::wstring& error){
    if(text.empty()||text.size()>262144){error=L"The identity migration receipt is empty or oversized.";return false;}
    Migration value;std::wistringstream in(text);std::wstring version,owner,scope,extra;int phase=0,oldMode=0,newMode=0,legacy=0;
    if(!(in>>version>>phase>>std::quoted(owner)>>std::quoted(scope)>>std::quoted(value.operation)>>std::quoted(value.entry.id)>>std::quoted(value.entry.name)>>std::quoted(value.entry.iniPath)>>std::quoted(value.entry.profileKey)
        >>std::quoted(value.before.package.name)>>std::quoted(value.before.package.publisher)>>oldMode>>legacy
        >>std::quoted(value.after.package.name)>>std::quoted(value.after.package.publisher)>>newMode
        >>std::quoted(value.backup)>>std::quoted(value.oldRuntime)>>std::quoted(value.newRuntime))||in>>extra||version!=L"AIP_PROFILE_IDENTITY_MIGRATION_1"||phase<0||phase>3||oldMode<0||oldMode>1||newMode<0||newMode>1||legacy<0||legacy>1||
        owner!=context.ownerSid||scope!=context.installScope||!Hex(value.operation,32)||value.entry.id!=entry.id||value.entry.iniPath!=entry.iniPath||value.entry.profileKey!=entry.profileKey){error=L"The identity migration receipt belongs to another profile or is invalid.";return false;}
    value.phase=static_cast<MigrationPhase>(phase);value.before.id=value.after.id=entry.id;value.before.ownerSid=value.after.ownerSid=context.ownerSid;value.before.profileKey=value.after.profileKey=entry.profileKey;
    value.before.mode=static_cast<IdentityMode>(oldMode);value.after.mode=static_cast<IdentityMode>(newMode);value.before.preservedLegacy=legacy!=0;
    if(!Valid(value.before)||!Valid(value.after)||value.backup!=entry.iniPath+L".before-identity-migration."+value.operation+L".bak"||!AbsoluteIniPath(value.oldRuntime)||!AbsoluteIniPath(value.newRuntime)){
        error=L"The identity migration receipt contains invalid ownership or backup paths.";return false;}
    const auto root=context.baseRoot+L"\\"+LowerAscii(entry.id)+L"\\runtime\\";
    const auto expectedNew=root+IdentityGeneration(value.after)+L"\\DesktopStub.exe";
    const auto expectedOld=root+IdentityGeneration(value.before)+L"\\DesktopStub.exe";
    if(value.newRuntime!=expectedNew||(value.oldRuntime!=expectedOld&&value.oldRuntime!=context.sourceRoot+L"\\DesktopStub.exe")){error=L"The identity migration runtime path is outside its owned generation.";return false;}
    result=std::move(value);return true;
}
class MigrationEngine {
    Store& store_;
    bool WriteReceipt(const Migration& migration,bool fresh,std::wstring& error){return store_.io.write(MigrationPath(migration.entry),SerializeMigration(store_.context,migration),fresh,error);}
    bool NoConflict(const ProfileEntry& entry,const IdentityRecord& proposed,std::wstring& error){
        if(!store_.RegisteredFamilyAvailable(proposed,false,error))return false;
        std::wstring proposedFamily;if(!PackageFamily(proposed.package,proposedFamily)){error=L"Windows rejected the proposed package identity or Publisher.";return false;}
        Registry registry;if(!store_.Load(registry,error))return false;
        for(const auto& other:registry.profiles)if(other.id!=entry.id){IdentityRecord identity;if(!store_.CurrentIdentity(other,identity,error))return false;std::wstring family;
            if(!PackageFamily(identity.package,family)){error=L"Another profile has an invalid Windows package identity; repair it before migrating.";return false;}
            if(_wcsicmp(family.c_str(),proposedFamily.c_str())==0){error=L"Another profile already uses this Windows package identity. Choose a different static name or Publisher.";return false;}}
        return true;
    }
public:
    explicit MigrationEngine(Store& store):store_(store){}
    bool Load(const ProfileEntry& entry,Migration& result,bool& present,std::wstring& error){
        const auto path=MigrationPath(entry);present=false;if(!store_.io.exists||!store_.io.exists(path,present,error))return false;if(!present)return true;
        if(!store_.io.owner(path,store_.context.ownerSid,error))return false;std::wstring text;bool missing=false;
        return store_.io.read(path,text,missing,error)&&!missing&&ParseMigration(store_.context,entry,text,result,error);
    }
    bool Prepare(const ProfileEntry& entry,const IdentityPlan& plan,Migration& result,std::wstring& error){
        if(!Valid(plan.current)||!Valid(plan.proposed)||plan.current.id!=entry.id||plan.proposed.id!=entry.id||plan.current.ownerSid!=store_.context.ownerSid||plan.proposed.ownerSid!=store_.context.ownerSid||
            plan.current.profileKey!=entry.profileKey||plan.proposed.profileKey!=entry.profileKey){error=L"The approved identity plan no longer belongs to this profile.";return false;}
        Migration previous;bool present=false;if(!Load(entry,previous,present,error))return false;
        if(present&&previous.phase!=MigrationPhase::Complete&&previous.phase!=MigrationPhase::RolledBack){error=L"This profile already has an unfinished identity migration. Restart it to recover before preparing another.";return false;}
        if(present&&(!store_.io.erase||!store_.io.erase(MigrationPath(entry),error)))return false;
        Migration value;value.entry=entry;value.before=plan.current;value.after=plan.proposed;
        if(!store_.io.random(value.operation)){error=L"A secure migration operation ID could not be created.";return false;}
        value.backup=entry.iniPath+L".before-identity-migration."+value.operation+L".bak";
        if(!store_.io.owner(entry.iniPath,store_.context.ownerSid,error))return false;bool missing=false;
        if(!store_.io.read(entry.iniPath,value.capturedText,missing,error)||missing)return false;
        std::optional<IdentityRecord> current;if(!IdentityFromText(value.capturedText,current,error)||!current||!SameRecord(*current,value.before)){error=L"The profile identity changed after approval. Reopen the identity settings.";return false;}
        if(!NoConflict(entry,value.after,error))return false;
        value.oldRuntime=store_.UsesOriginalMainRuntime(entry)?store_.context.sourceRoot+L"\\DesktopStub.exe":store_.RuntimeRootFor(entry,value.before)+L"\\DesktopStub.exe";
        value.newRuntime=store_.RuntimeRootFor(entry,value.after)+L"\\DesktopStub.exe";
        bool oldRuntimeExists=false;if(!store_.io.exists||!store_.io.exists(value.oldRuntime,oldRuntimeExists,error))return false;
        if(!oldRuntimeExists&&!store_.UsesOriginalMainRuntime(entry)&&!store_.StageForIdentity(entry,value.before,error))return false;
        if(!store_.StageForIdentity(entry,value.after,error)||!store_.io.backup||!store_.io.backup(entry.iniPath,value.backup,error)||!WriteReceipt(value,true,error))return false;
        result=std::move(value);return true;
    }
    bool Commit(Migration& migration,std::wstring& error){
        const bool saved=store_.io.locked(store_.RegistryPath(),[&]{if(!NoConflict(migration.entry,migration.after,error))return false;return store_.io.locked(migration.entry.iniPath,[&]{
            if(!store_.io.owner(migration.entry.iniPath,store_.context.ownerSid,error))return false;std::wstring text;bool missing=false;
            if(!store_.io.read(migration.entry.iniPath,text,missing,error)||missing||text!=migration.capturedText){error=L"The profile settings changed after migration approval. No identity was replaced.";return false;}
            if(!ApplyIdentity(text,migration.after,L"1",error)||!store_.io.write(migration.entry.iniPath,text,false,error))return false;return true;
        },error);},error);
        if(!saved)return false;migration.phase=MigrationPhase::AwaitingSetup;
        if(WriteReceipt(migration,false,error))return true;
        std::wstring ignored;Rollback(migration,ignored);return false;
    }
    bool Rollback(Migration& migration,std::wstring& error){
        const bool restored=store_.io.locked(migration.entry.iniPath,[&]{
            if(!store_.io.owner(migration.entry.iniPath,store_.context.ownerSid,error)||!store_.io.owner(migration.backup,store_.context.ownerSid,error))return false;
            std::wstring text,backup;bool missing=false;if(!store_.io.read(migration.entry.iniPath,text,missing,error)||missing||!store_.io.read(migration.backup,backup,missing,error)||missing)return false;
            std::optional<IdentityRecord> current;if(!IdentityFromText(text,current,error)||!current||(!SameRecord(*current,migration.after)&&!SameRecord(*current,migration.before))){error=L"The profile identity changed independently; the migration backup was kept for manual recovery.";return false;}
            std::wstring managed;const auto old=aip::ParseIniDocument(backup);if(!aip::ReadIniValueFromDoc(old,L"Profile",L"ManagedRuntime",managed))managed=L"0";
            struct Edited{std::wstring section,key,value;};std::vector<Edited> later;
            std::wstring expectedText;if(!ApplyIdentity(expectedText,migration.after,L"1",error))return false;
            const auto expected=aip::ParseIniDocument(expectedText),latest=aip::ParseIniDocument(text);
            for(const auto& field:std::vector<std::pair<std::wstring,std::wstring>>{{L"Profile",L"IdentityMode"},{L"Profile",L"StaticPackageName"},{L"Profile",L"StaticPublisher"},{L"Settings",L"ManifestIdentityName"},{L"Settings",L"ManifestPublisher"}}){
                std::wstring value,written;if(aip::ReadIniValueFromDoc(latest,field.first.c_str(),field.second.c_str(),value)&&aip::ReadIniValueFromDoc(expected,field.first.c_str(),field.second.c_str(),written)&&value!=written)later.push_back({field.first,field.second,value});
            }
            // Restore identity fields only. Preserve any later account/source
            // edits and the setup state managed by the host's own transaction.
            if(!ApplyIdentity(text,migration.before,managed,error))return false;
            for(const auto& field:later)if(!aip::WriteIniValueToText(text,field.section.c_str(),field.key.c_str(),field.value))return false;
            if(!store_.io.write(migration.entry.iniPath,text,false,error))return false;return true;
        },error);
        if(!restored)return false;migration.phase=MigrationPhase::RolledBack;
        if(!WriteReceipt(migration,false,error))return false;
        return true; // Keep the rollback receipt until the old runtime starts.
    }
    bool Complete(Migration& migration,bool success,std::wstring& error){
        if(!success)return Rollback(migration,error);
        IdentityRecord current;if(!store_.CurrentIdentity(migration.entry,current,error)||!SameRecord(current,migration.after)){error=L"The installed identity no longer matches the pending migration.";return false;}
        migration.phase=MigrationPhase::Complete;if(!WriteReceipt(migration,false,error))return false;
        if(store_.io.erase){std::wstring ignored;store_.io.erase(MigrationPath(migration.entry),ignored);}return true;
    }
};
}}}
#endif
