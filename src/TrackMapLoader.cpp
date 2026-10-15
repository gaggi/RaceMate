#include "TrackMapLoader.h"
#include <windows.h>
#include <winhttp.h>
#include <fstream>
#include <memory>

namespace rm::trackmap {
namespace {
constexpr size_t MaxBytes=1024*1024;
struct Close { void operator()(void* h) const { if(h) WinHttpCloseHandle(h); } };
using Handle=std::unique_ptr<void,Close>;
std::string Read(const std::filesystem::path& file) {
    std::error_code error; const auto size=std::filesystem::file_size(file,error);
    if(error || size==0 || size>MaxBytes) return {};
    std::ifstream stream(file,std::ios::binary); std::string data(static_cast<size_t>(size),'\0');
    return stream.read(data.data(),static_cast<std::streamsize>(data.size()))?data:std::string{};
}
bool Write(const std::filesystem::path& file,const std::string& bytes) {
    const auto temp=file.wstring()+L"."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(GetCurrentThreadId())+L".tmp";
    std::ofstream stream(temp,std::ios::binary|std::ios::trunc); stream.write(bytes.data(),static_cast<std::streamsize>(bytes.size())); stream.close();
    const bool ok=stream.good() && MoveFileExW(temp.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
    if(!ok) DeleteFileW(temp.c_str());
    return ok;
}
std::string Download(HINTERNET connection,const std::wstring& path,const std::atomic_bool& cancelled,ULONGLONG deadline) {
    if(cancelled || GetTickCount64()>deadline) return {};
    Handle request(WinHttpOpenRequest(connection,L"GET",path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE));
    if(!request) return {};
    DWORD redirect=WINHTTP_OPTION_REDIRECT_POLICY_NEVER; WinHttpSetOption(request.get(),WINHTTP_OPTION_REDIRECT_POLICY,&redirect,sizeof(redirect));
    if(!WinHttpSendRequest(request.get(),WINHTTP_NO_ADDITIONAL_HEADERS,0,WINHTTP_NO_REQUEST_DATA,0,0,0) || !WinHttpReceiveResponse(request.get(),nullptr)) return {};
    DWORD status=0,size=sizeof(status);
    if(!WinHttpQueryHeaders(request.get(),WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&size,WINHTTP_NO_HEADER_INDEX) || status!=200) return {};
    std::string result; char buffer[16384]; DWORD read=0;
    while(!cancelled && GetTickCount64()<=deadline) {
        if(!WinHttpReadData(request.get(),buffer,sizeof(buffer),&read)) return {};
        if(!read) return result;
        if(result.size()+read>MaxBytes) return {};
        result.append(buffer,read);
    }
    return {};
}
}
Loaded Load(const Source& source,const std::filesystem::path& cache,const std::atomic_bool& cancelled) {
    if(cancelled) { Loaded result; result.map.error="Cancelled"; return result; }
    const auto directory=cache/std::to_wstring(source.id);
    const std::string identity=std::string(source.folder)+"\n";
    Loaded loaded;
    if(Read(directory/L"source.txt")==identity) {
        loaded.map=Prepare(Read(directory/L"active.svg"),Read(directory/L"start-finish.svg"),Read(directory/L"pitroad.svg"));
        if(loaded.map) { loaded.fromCache=loaded.cached=true; return loaded; }
    }
    Handle session(WinHttpOpen(L"RaceMate/TrackMapPrototype",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0));
    if(session) WinHttpSetTimeouts(session.get(),4000,4000,5000,5000);
    Handle connection(session?WinHttpConnect(session.get(),L"members-assets.iracing.com",INTERNET_DEFAULT_HTTPS_PORT,0):nullptr);
    if(!connection || cancelled) { loaded.map.error="Could not connect to the official SVG server"; return loaded; }
    const std::string folder=source.folder; const std::wstring prefix(folder.begin(),folder.end());
    const auto deadline=GetTickCount64()+20000;
    auto active=Download(connection.get(),prefix+L"/active.svg",cancelled,deadline);
    auto finish=Download(connection.get(),prefix+L"/start-finish.svg",cancelled,deadline);
    if(active.empty() || finish.empty()) { loaded.map.error="Official SVGs unavailable; check connection and retry"; return loaded; }
    auto pit=Download(connection.get(),prefix+L"/pitroad.svg",cancelled,deadline);
    if(cancelled) { loaded.map.error="Cancelled"; return loaded; }
    loaded.map=Prepare(active,finish,pit);
    if(loaded.map) {
        std::error_code error; std::filesystem::create_directories(directory,error);
        if(!error) loaded.cached=Write(directory/L"active.svg",active) && Write(directory/L"start-finish.svg",finish) && Write(directory/L"pitroad.svg",pit) && Write(directory/L"source.txt",identity);
    }
    return loaded;
}
}
