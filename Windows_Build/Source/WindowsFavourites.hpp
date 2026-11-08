#pragma once
// Windows-only favourites. The list references the ORIGINAL bank slots; it
// never writes, copies or changes a .sy1 file. Keys are relative to the active
// library folder, so Synth2_Data can move between drives unchanged.
#include <algorithm>
#include <cctype>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif

namespace oxo::winfav {
namespace fs = std::filesystem;
struct Item {
    std::string bank;
    int slot = -1;
    std::string name;
};

// Natural ordering without changing original PatchManager bank indices.
inline bool NaturalLess(const std::wstring& a, const std::wstring& b) {
    size_t i=0,j=0;
    while(i<a.size() && j<b.size()) {
        if(a[i]>=L'0' && a[i]<=L'9' && b[j]>=L'0' && b[j]<=L'9') {
            const size_t ai=i,bj=j;
            while(i<a.size() && a[i]>=L'0' && a[i]<=L'9') ++i;
            while(j<b.size() && b[j]>=L'0' && b[j]<=L'9') ++j;
            size_t nzA=ai,nzB=bj;
            while(nzA<i && a[nzA]==L'0') ++nzA;
            while(nzB<j && b[nzB]==L'0') ++nzB;
            const size_t la=i-nzA,lb=j-nzB;
            if(la!=lb) return la<lb;
            for(size_t k=0;k<la;k++) if(a[nzA+k]!=b[nzB+k]) return a[nzA+k]<b[nzB+k];
            // Stable equal-value tie break: fewer leading zeroes first.
            if(i-ai!=j-bj) return i-ai<j-bj;
        } else {
            const wchar_t ac=std::towlower(a[i]),bc=std::towlower(b[j]);
            if(ac!=bc) return ac<bc;
            ++i;++j;
        }
    }
    return i==a.size() && j!=b.size();
}

inline std::string BankKey(const fs::path& root, const fs::path& source, bool factory) {
    if(factory) return "@factory";
    const fs::path rel=source.lexically_normal().lexically_relative(root.lexically_normal());
    // If the source is outside the library, preserve it as an absolute fallback.
    // Normal portable library banks use the relative branch.
    if(rel.empty() || (!rel.empty() && *rel.begin()==fs::path("..")))
        return "@absolute:"+source.generic_string();
    return "@library:"+rel.generic_string();
}

inline std::string Escape(const std::string& s) {
    static constexpr char hex[]="0123456789abcdef";
    std::string out="\"";
    for(unsigned char c:s) {
        if(c=='\"'||c=='\\') {out+='\\';out+=char(c);}
        else if(c<0x20) {out+="\\u00";out+=hex[c>>4];out+=hex[c&15];}
        else out+=char(c);
    }
    return out+'"';
}
struct Parser {
    const std::string& s;size_t p=0;
    void ws() {while(p<s.size() && (s[p]==' '||s[p]=='\t'||s[p]=='\r'||s[p]=='\n'))++p;}
    bool eat(char c) {ws();if(p<s.size() && s[p]==c){++p;return true;}return false;}
    bool string(std::string& out) {
        out.clear();if(!eat('"'))return false;
        while(p<s.size()) {
            unsigned char c=static_cast<unsigned char>(s[p++]);
            if(c=='"')return true;
            if(c<0x20)return false;
            if(c!='\\'){out+=char(c);continue;}
            if(p>=s.size())return false;
            c=static_cast<unsigned char>(s[p++]);
            if(c=='"'||c=='\\'||c=='/')out+=char(c);
            else if(c=='n')out+='\n';else if(c=='t')out+='\t';else if(c=='r')out+='\r';
            else if(c=='b')out+='\b';else if(c=='f')out+='\f';
            else if(c=='u') {
                if(p+4>s.size())return false;
                unsigned code=0;
                for(int i=0;i<4;i++){
                    char h=s[p++];code<<=4;
                    if(h>='0'&&h<='9')code+=h-'0';
                    else if(h>='a'&&h<='f')code+=h-'a'+10;
                    else if(h>='A'&&h<='F')code+=h-'A'+10;
                    else return false;
                }
                // The encoder only generates escapes for ASCII controls.
                if(code<0x80)out+=char(code);
                else if(code<0x800){out+=char(0xC0|(code>>6));out+=char(0x80|(code&63));}
                else {out+=char(0xE0|(code>>12));out+=char(0x80|((code>>6)&63));out+=char(0x80|(code&63));}
            } else return false;
        }
        return false;
    }
    bool number(int& value) {
        ws();const size_t start=p;
        if(p<s.size() && s[p]=='-')++p;
        if(p==s.size() || s[p]<'0'||s[p]>'9')return false;
        long long v=0;bool negative=s[start]=='-';
        while(p<s.size() && s[p]>='0' && s[p]<='9'){
            v=v*10+s[p++]-'0';if(v>1000000000LL)return false;
        }
        value=static_cast<int>(negative?-v:v);return true;
    }
    bool skip(int depth=0) {
        if(depth>16)return false;
        ws();if(p>=s.size())return false;
        if(s[p]=='"'){std::string tmp;return string(tmp);}
        if(eat('{')){
            if(eat('}'))return true;
            do {std::string k;if(!string(k)||!eat(':')||!skip(depth+1))return false;}while(eat(','));
            return eat('}');
        }
        if(eat('[')){
            if(eat(']'))return true;
            do {if(!skip(depth+1))return false;}while(eat(','));
            return eat(']');
        }
        const size_t start=p;
        while(p<s.size() && s[p]!=',' && s[p]!=']' && s[p]!='}' &&
              s[p]!=' ' && s[p]!='\r' && s[p]!='\n' && s[p]!='\t')++p;
        return p>start;
    }
};

inline bool Parse(const std::string& json,std::vector<Item>& items) {
    Parser r{json};std::vector<Item> result;
    bool hasEntries=false,hasFormat=false;
    if(!r.eat('{'))return false;
    if(!r.eat('}'))do {
        std::string key;if(!r.string(key)||!r.eat(':'))return false;
        if(key=="version") {int v=0;if(!r.number(v)||v!=1)return false;hasFormat=true;}
        else if(key=="favourites") {
            if(hasEntries||!r.eat('['))return false;
            hasEntries=true;
            if(!r.eat(']'))do {
                if(!r.eat('{'))return false;
                Item it{};bool bank=false,slot=false;
                if(!r.eat('}'))do {
                    std::string field;if(!r.string(field)||!r.eat(':'))return false;
                    if(field=="bank") {if(!r.string(it.bank))return false;bank=true;}
                    else if(field=="slot") {if(!r.number(it.slot))return false;slot=true;}
                    else if(field=="name") {if(!r.string(it.name))return false;}
                    else if(!r.skip())return false;
                }while(r.eat(',') && !r.eat('}'));
                // Handle a final closing } when the last key had no comma.
                if(r.p>0 && json[r.p-1]!='}' && !r.eat('}'))return false;
                if(!bank || !slot || it.bank.empty() || it.slot<0 || it.slot>=128)return false;
                if(std::none_of(result.begin(),result.end(),[&](const Item& x){return x.bank==it.bank&&x.slot==it.slot;}))
                    result.push_back(std::move(it));
                if(result.size()>100000)return false;
            }while(r.eat(',') && !r.eat(']'));
            if(r.p>0 && json[r.p-1]!=']' && !r.eat(']'))return false;
        } else if(!r.skip())return false;
    }while(r.eat(',') && !r.eat('}'));
    if(r.p>0 && json[r.p-1]!='}' && !r.eat('}'))return false;
    r.ws();if(r.p!=json.size() || !hasFormat || !hasEntries)return false;
    items=std::move(result);return true;
}

inline fs::path File(const fs::path& root) {return root/"favourites.json";}
inline bool Load(const fs::path& root,std::vector<Item>& items) {
    std::error_code ec;const auto path=File(root);
    if(!fs::exists(path,ec))return !ec;
    if(ec || !fs::is_regular_file(path,ec) || ec || fs::file_size(path,ec)>8*1024*1024 || ec)return false;
    std::ifstream in(path,std::ios::binary);if(!in)return false;
    std::string json(std::istreambuf_iterator<char>{in},{});
    if(in.bad())return false;
    return Parse(json,items);
}
inline bool Save(const fs::path& root,const std::vector<Item>& items) {
    std::string json="{\n  \"version\": 1,\n  \"favourites\": [\n";
    for(size_t i=0;i<items.size();i++) {
        const auto& f=items[i];
        if(f.bank.empty()||f.slot<0||f.slot>=128)return false;
        if(i)json+=",\n";
        json+="    {\"bank\": "+Escape(f.bank)+", \"slot\": "+std::to_string(f.slot)+
              ", \"name\": "+Escape(f.name)+"}";
    }
    json+="\n  ]\n}\n";
    std::error_code ec;fs::create_directories(root,ec);if(ec)return false;
    const fs::path path=File(root);
    fs::path tmp=path;
#ifdef _WIN32
    tmp += L".tmp."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(GetCurrentThreadId());
#else
    tmp += ".tmp";
#endif
    {
        std::ofstream out(tmp,std::ios::binary|std::ios::trunc);
        if(!out)return false;
        out.write(json.data(),static_cast<std::streamsize>(json.size()));
        out.flush();
        if(!out){fs::remove(tmp,ec);return false;}
    }
#ifdef _WIN32
    if(!MoveFileExW(tmp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){
        fs::remove(tmp,ec);return false;
    }
#else
    fs::rename(tmp,path,ec);
    if(ec){fs::remove(tmp,ec);return false;}
#endif
    return true;
}
}
