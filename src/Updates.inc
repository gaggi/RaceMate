// Update check against the latest GitHub release, as in LaunchMate: once at startup (optional)
// and from the Settings page. The startup check never opens a dialog while iRacing runs or
// RaceMate sits in the tray; the Settings page shows the update instead.
struct UpdateInstall { std::filesystem::path file; std::wstring error; };
struct Updates {
    BackgroundTask<UpdateCheckResult> check; BackgroundTask<UpdateInstall> install;
    bool interactive=false;
    std::optional<UpdateCheckResult> latest; // Last finished check.
    std::wstring status;                     // Shown on the Settings page.
} updates;
constexpr int UpdateInstallButton=6101, UpdateOpenButton=6102, UpdateLaterButton=6103;
bool UpdateAvailable() { return updates.latest && updates.latest->state==UpdateCheckState::UpdateAvailable; }
HRESULT CALLBACK UpdateDialogCallback(HWND,UINT notification,WPARAM,LPARAM lp,LONG_PTR) {
    if(notification==TDN_HYPERLINK_CLICKED && lp) UpdateChecker::OpenReleasePage(reinterpret_cast<const wchar_t*>(lp));
    return S_OK;
}
int ShowUpdateDialog(const UpdateCheckResult& result) {
    const bool available=result.state==UpdateCheckState::UpdateAvailable;
    const std::wstring instruction=available?L"A newer RaceMate version is available":L"RaceMate is up to date";
    std::wstring content=L"Current version:  "+UpdateChecker::CurrentVersion()+L"\nGitHub version:  "+(result.release.versionDisplay.empty()?std::wstring(L"Unavailable"):result.release.versionDisplay);
    if(!result.release.releasePageUrl.empty()) content+=L"\n\n<a href=\""+result.release.releasePageUrl+L"\">Open this release on GitHub</a>";
    TASKDIALOGCONFIG config{sizeof(config)};
    config.hwndParent=app.main; config.dwFlags=TDF_ENABLE_HYPERLINKS|TDF_ALLOW_DIALOG_CANCELLATION|TDF_POSITION_RELATIVE_TO_WINDOW;
    config.pszWindowTitle=L"RaceMate Update"; config.pszMainIcon=TD_INFORMATION_ICON;
    config.pszMainInstruction=instruction.c_str(); config.pszContent=content.c_str(); config.pfCallback=UpdateDialogCallback;
    TASKDIALOG_BUTTON buttons[2]{};
    if(available) {
        buttons[0]=result.release.assetDownloadUrl.empty()?TASKDIALOG_BUTTON{UpdateOpenButton,L"Open release on GitHub"}:TASKDIALOG_BUTTON{UpdateInstallButton,L"Download and install update"};
        buttons[1]={UpdateLaterButton,L"Later"};
        config.pButtons=buttons; config.cButtons=2; config.nDefaultButton=buttons[0].nButtonID;
    } else config.dwCommonButtons=TDCBF_OK_BUTTON;
    int selected=IDCANCEL;
    if(FAILED(TaskDialogIndirect(&config,&selected,nullptr,nullptr))) {
        const bool yes=MessageBoxW(app.main,content.c_str(),L"RaceMate Update",(available?MB_YESNO:MB_OK)|MB_ICONINFORMATION)==IDYES;
        selected=yes?(result.release.assetDownloadUrl.empty()?UpdateOpenButton:UpdateInstallButton):IDCANCEL;
    }
    return selected;
}
void StartUpdateCheck(bool interactive) {
    if(app.diagnostics || updates.check.Running() || updates.install.Running()) return;
    updates.interactive=interactive; updates.status=L"Checking for updates...";
    updates.check.Start([](const std::atomic_bool&) { return UpdateChecker::CheckForUpdate(); });
    RefreshPage();
}
void BeginUpdateInstall(UpdateReleaseInfo release) {
    if(updates.install.Running()) return;
    updates.status=L"Downloading RaceMate "+release.versionDisplay+L"...";
    updates.install.Start([release=std::move(release)](const std::atomic_bool&) {
        UpdateInstall result; if(!UpdateChecker::DownloadReleaseAsset(release,result.file,result.error) && result.error.empty()) result.error=L"The download failed.";
        return result;
    });
    RefreshPage();
}
void OfferUpdate() {
    if(!updates.latest) return;
    const int choice=ShowUpdateDialog(*updates.latest);
    if(choice==UpdateInstallButton) BeginUpdateInstall(updates.latest->release);
    else if(choice==UpdateOpenButton && !UpdateChecker::OpenReleasePage(updates.latest->release.releasePageUrl))
        MessageBoxW(app.main,L"Could not open the GitHub release page.",L"RaceMate Update",MB_OK|MB_ICONWARNING);
}
// Called from Tick; both tasks finish on a worker thread.
void PollUpdates() {
    std::optional<UpdateCheckResult> checked;
    if(updates.check.Poll(checked)) {
        if(!checked) { checked=UpdateCheckResult{}; checked->message=L"The update check stopped unexpectedly."; }
        updates.latest=std::move(checked); const auto& result=*updates.latest;
        if(result.state==UpdateCheckState::Failed) {
            updates.status=L"Update check failed: "+result.message;
            if(updates.interactive) MessageBoxW(app.main,(L"Current version:  "+UpdateChecker::CurrentVersion()+L"\nGitHub version:  Unavailable\n\n"+result.message).c_str(),L"RaceMate Update",MB_OK|MB_ICONWARNING);
        } else if(result.state==UpdateCheckState::UpToDate) {
            updates.status=L"Up to date";
            if(updates.interactive) ShowUpdateDialog(result);
        } else {
            updates.status=L"Version "+result.release.versionDisplay+L" is available";
            const bool quiet=!updates.interactive && (app.frame.connected || !IsWindowVisible(app.main));
            RefreshPage(); if(!quiet) OfferUpdate();
        }
        RefreshPage();
    }
    std::optional<UpdateInstall> installed;
    if(updates.install.Poll(installed)) {
        std::wstring error=installed?installed->error:L"The download stopped unexpectedly.";
        if(error.empty() && UpdateChecker::LaunchSelfUpdater(installed->file,GetCurrentProcessId(),error)) { DestroyWindow(app.main); return; }
        updates.status=L"Update failed";
        MessageBoxW(app.main,(L"Failed to install the RaceMate update.\n\n"+error).c_str(),L"RaceMate Update",MB_OK|MB_ICONWARNING);
        RefreshPage();
    }
}
