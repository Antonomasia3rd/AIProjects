#pragma once
// Explicit, temporary overseas WuWa sign-in inputs. No stored passwords or
// browser/game credential discovery. The adapter owns HTTPS and parsing.
#include "kuro_notes_model.h"
#include <utility>
namespace aip { namespace kuro { namespace auth {
template<class Character> inline void Wipe(std::basic_string<Character>& value) noexcept {
    volatile Character* memory=value.empty()?nullptr:&value[0];
    for(size_t index=0;index<value.size();++index)memory[index]=0;
    value.clear();
}
struct Credentials {
    std::wstring email,password,deviceId;
    Credentials()=default;Credentials(const Credentials&)=delete;Credentials& operator=(const Credentials&)=delete;
    ~Credentials(){Wipe(password);Wipe(email);}
};
struct CaptchaProof {
    std::wstring lotNumber,passToken,genTime,captchaOutput;
    bool Available()const{return !lotNumber.empty()&&!passToken.empty()&&!genTime.empty()&&!captchaOutput.empty();}
    void Clear(){Wipe(lotNumber);Wipe(passToken);Wipe(genTime);Wipe(captchaOutput);}
    ~CaptchaProof(){Clear();}
};
enum class Status { Error,CaptchaRequired,Authenticated };
struct Result {
    Status status=Status::Error;
    std::wstring token,message;
    int expiresIn=0;
    ~Result(){Wipe(token);}
};
inline constexpr const wchar_t* GameCaptchaId=L"1f4565ff7acc97b1a2fc97b921743aa4";
inline bool ValidCredentials(const Credentials& input) {
    return !input.email.empty()&&input.email.size()<=320&&SafeText(input.email,320)&&input.email.find(L'@')!=std::wstring::npos&&
        !input.password.empty()&&input.password.size()<=1024&&SafeText(input.password,1024)&&
        !input.deviceId.empty()&&SafeText(input.deviceId,128);
}
}}}
