// Buttons of wheels, button boxes and gamepads as shortcuts. Windows' shortcuts know only the keyboard; these devices'
// reports arrive as raw input, also while iRacing has the focus. Unlike a key shortcut, iRacing still gets the button.
struct JoyDevice { unsigned short vendor=0, product=0; std::wstring name; std::vector<BYTE> preparsed; std::vector<USAGE> down; bool known=false; };
std::map<HANDLE,JoyDevice> joyDevices;
// Listens only while a button is assigned or awaited: a wheel reports up to a thousand times a second.
void ListenToButtons() {
    if(!app.main) return;
    const bool wanted=app.joyCapture>=0 || std::any_of(app.joyButtons.begin(),app.joyButtons.end(),[](const JoyButton& b){ return b.button!=0; });
    if(wanted==app.joyListening) return;
    // Generic desktop joysticks, gamepads and multi-axis controllers: wheels, pedals and button boxes among them.
    RAWINPUTDEVICE devices[3]{{1,4,0,nullptr},{1,5,0,nullptr},{1,8,0,nullptr}};
    for(auto& d:devices) { d.dwFlags=wanted?RIDEV_INPUTSINK|RIDEV_DEVNOTIFY:RIDEV_REMOVE; d.hwndTarget=wanted?app.main:nullptr; }
    if(RegisterRawInputDevices(devices,static_cast<UINT>(std::size(devices)),sizeof(RAWINPUTDEVICE))) app.joyListening=wanted;
    if(!app.joyListening) joyDevices.clear();
}
// A device's vendor, product and name, and what its reports need to be read; once per device.
void LearnJoyDevice(HANDLE handle,JoyDevice& device) {
    device.known=true;
    RID_DEVICE_INFO info{}; info.cbSize=sizeof(info); UINT size=sizeof(info);
    if(GetRawInputDeviceInfoW(handle,RIDI_DEVICEINFO,&info,&size)!=static_cast<UINT>(-1) && info.dwType==RIM_TYPEHID) {
        device.vendor=static_cast<unsigned short>(info.hid.dwVendorId); device.product=static_cast<unsigned short>(info.hid.dwProductId);
    }
    size=0; GetRawInputDeviceInfoW(handle,RIDI_PREPARSEDDATA,nullptr,&size); device.preparsed.resize(size);
    if(!size || GetRawInputDeviceInfoW(handle,RIDI_PREPARSEDDATA,device.preparsed.data(),&size)==static_cast<UINT>(-1)) device.preparsed.clear();
    size=0; GetRawInputDeviceInfoW(handle,RIDI_DEVICENAME,nullptr,&size);
    std::wstring path(size,L'\0');
    if(size && GetRawInputDeviceInfoW(handle,RIDI_DEVICENAME,path.data(),&size)!=static_cast<UINT>(-1)) {
        const HANDLE file=CreateFileW(path.c_str(),0,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
        wchar_t name[127]{};
        if(file!=INVALID_HANDLE_VALUE) { if(HidD_GetProductString(file,name,sizeof(name))) device.name=name; CloseHandle(file); }
    }
}
// A button newly pressed: assigned while one is awaited, else the shortcuts it is assigned to.
void JoyPressed(const JoyDevice& device,USAGE button) {
    const JoyButton pressed{device.vendor,device.product,button,device.name};
    if(app.joyCapture>=0) {
        app.joyButtons[app.joyCapture]=pressed; app.joyCapture=-1; Save();
        ListenToButtons(); PostMessageW(app.main,RefreshUiMessage,0,0);
        return;
    }
    for(int i=0;i<HotkeyCount;++i) if(app.joyButtons[i]==pressed) RunShortcut(i+1);
}
void OnJoystickInput(HRAWINPUT input) {
    static std::vector<BYTE> buffer; static std::vector<USAGE> usages, down;
    UINT size=0; GetRawInputData(input,RID_INPUT,nullptr,&size,sizeof(RAWINPUTHEADER));
    if(!size) return;
    if(buffer.size()<size) buffer.resize(size);
    if(GetRawInputData(input,RID_INPUT,buffer.data(),&size,sizeof(RAWINPUTHEADER))!=size) return;
    const auto* raw=reinterpret_cast<const RAWINPUT*>(buffer.data());
    if(raw->header.dwType!=RIM_TYPEHID) return;
    auto& device=joyDevices[raw->header.hDevice];
    if(!device.known) LearnJoyDevice(raw->header.hDevice,device);
    if(device.preparsed.empty()) return;
    const auto data=reinterpret_cast<PHIDP_PREPARSED_DATA>(device.preparsed.data());
    // A device with several report IDs sends some without its buttons: those leave the buttons as they were.
    bool read=false; down.clear();
    for(DWORD r=0;r<raw->data.hid.dwCount;++r) {
        ULONG count=HidP_MaxUsageListLength(HidP_Input,HID_USAGE_PAGE_BUTTON,data); usages.resize(count);
        auto* report=const_cast<CHAR*>(reinterpret_cast<const CHAR*>(raw->data.hid.bRawData+r*raw->data.hid.dwSizeHid));
        if(count && HidP_GetUsages(HidP_Input,HID_USAGE_PAGE_BUTTON,0,usages.data(),&count,data,report,raw->data.hid.dwSizeHid)==HIDP_STATUS_SUCCESS) {
            read=true; down.insert(down.end(),usages.begin(),usages.begin()+count);
        }
    }
    if(!read) return;
    std::sort(down.begin(),down.end());
    const auto before=device.down; device.down=down;
    for(USAGE button:down) if(!std::binary_search(before.begin(),before.end(),button)) JoyPressed(device,button);
}
