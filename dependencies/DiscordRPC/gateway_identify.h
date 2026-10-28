#pragma once
// Pure serialization shared by the native Gateway and its inert contract test.
// The returned body contains the supplied secret: never log or persist it.
#include "../desktop_app_baseline.h"
namespace aip { namespace discord {
inline std::string BuildGatewayIdentifyBody(const std::wstring& token,const std::string& properties,
    const std::string& clientState,int capabilities,const std::string& presence,bool readerOnly){
    std::string data="{\"token\":"+aip::JsonEscape(token)+",\"capabilities\":"+std::to_string(capabilities)+
        ",\"properties\":"+properties+",\"compress\":false,\"client_state\":"+clientState;
    if(!readerOnly)data+=",\"presence\":"+presence;
    data+='}';return data;
}
} }
