// Windows startup registration, included inside the application's private namespace.
std::wstring StartupCommand() {
    wchar_t path[32768]{}; const DWORD length=GetModuleFileNameW(nullptr,path,32768);
    if(!length || length>=32768) return {};
    return L"\""+std::wstring(path,length)+L"\"";
}
int StartupShowCommand(bool trayAvailable,int requested) {
    return app.startMinimized && trayAvailable?SW_HIDE:requested;
}
bool RegisterStartup(bool enabled) {
    if(app.diagnostics) return true;
    HKEY key=nullptr;
    if(RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)!=ERROR_SUCCESS) return false;
    LSTATUS result;
    if(enabled) {
        const auto command=StartupCommand();
        result=command.empty()?ERROR_BAD_PATHNAME:RegSetValueExW(key,L"RaceMate",0,REG_SZ,reinterpret_cast<const BYTE*>(command.c_str()),static_cast<DWORD>((command.size()+1)*sizeof(wchar_t)));
    } else { result=RegDeleteValueW(key,L"RaceMate"); if(result==ERROR_FILE_NOT_FOUND) result=ERROR_SUCCESS; }
    RegCloseKey(key); return result==ERROR_SUCCESS;
}
