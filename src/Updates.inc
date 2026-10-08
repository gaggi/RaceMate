// Update check against the latest GitHub release, as in LaunchMate and TPMate: no dialogs.
// The version row on the Settings page shows the state; a check at startup only changes that
// row and the sidebar footer, and a failed startup check stays silent.
struct UpdateInstall { std::filesystem::path file; std::wstring error; };
struct Updates {
    enum class Phase { Idle, Checking, UpToDate, Available, Downloading, Failed };
    Phase phase=Phase::Idle;
    std::wstring message; UpdateReleaseInfo release;
    BackgroundTask<UpdateCheckResult> check; BackgroundTask<UpdateInstall> install;
    bool startup=false;
} updates;
void StartUpdateCheck(bool startup) {
    if(app.diagnostics || updates.phase==Updates::Phase::Checking || updates.phase==Updates::Phase::Downloading) return;
    updates.startup=startup; updates.phase=Updates::Phase::Checking;
    updates.check.Start([](const std::atomic_bool&) { return UpdateChecker::CheckForUpdate(); });
    RefreshPage();
}
void BeginUpdateInstall() {
    if(updates.phase!=Updates::Phase::Available || updates.release.assetDownloadUrl.empty()) return;
    updates.phase=Updates::Phase::Downloading;
    updates.install.Start([release=updates.release](const std::atomic_bool&) {
        UpdateInstall result;
        if(!UpdateChecker::DownloadReleaseAsset(release,result.file,result.error)) result.error=L"Could not download the update. "+result.error;
        return result;
    });
    RefreshPage();
}
// Called from Tick; both tasks finish on a worker thread.
void PollUpdates() {
    std::optional<UpdateCheckResult> checked;
    if(updates.check.Poll(checked)) {
        if(!checked) { checked=UpdateCheckResult{}; checked->message=L"The update check failed unexpectedly."; }
        updates.release=checked->release; updates.message=checked->message;
        switch(checked->state) {
        // No network yet at startup is not worth a warning.
        case UpdateCheckState::Failed: updates.phase=updates.startup?Updates::Phase::Idle:Updates::Phase::Failed; break;
        case UpdateCheckState::UpToDate:
            updates.phase=Updates::Phase::UpToDate;
            if(!checked->release.versionDisplay.empty()) updates.message=L"Latest release on GitHub: "+checked->release.versionDisplay+L".";
            break;
        case UpdateCheckState::UpdateAvailable:
            updates.phase=Updates::Phase::Available; ui.nav.SetFooter(L"Update available: "+checked->release.versionDisplay,true);
            break;
        }
        RefreshPage();
    }
    std::optional<UpdateInstall> installed;
    if(updates.install.Poll(installed)) {
        std::wstring error=installed?installed->error:L"The update could not be installed.";
        if(error.empty()) {
            if(UpdateChecker::LaunchSelfUpdater(installed->file,GetCurrentProcessId(),error)) { DestroyWindow(app.main); return; }
            error=L"Could not prepare the update. "+error;
        }
        updates.phase=Updates::Phase::Failed; updates.message=error; RefreshPage();
    }
}
// Same states as in LaunchMate and TPMate; no dialogs.
RowList::Row UpdateVersionRow() {
    using Phase=Updates::Phase;
    RowList::Row row; row.title=L"Version " RACEMATE_VERSION; row.button=L"Check now";
    switch(updates.phase) {
    case Phase::Idle: row.detail=L"Look for a newer release on GitHub."; break;
    case Phase::Checking: row.detail=L"Checking GitHub..."; row.buttonEnabled=false; break;
    case Phase::UpToDate: row.detail=updates.message; row.pill=L"Up to date"; row.pillTone=RowList::Tone::Active; break;
    case Phase::Available: {
        const bool installable=!updates.release.assetDownloadUrl.empty();
        row.detail=L"Version "+updates.release.versionDisplay+L" is available. "+(installable?L"RaceMate restarts after installing it.":L"It has no download for this Windows version.");
        row.pill=L"Update available"; row.pillTone=RowList::Tone::Accent;
        row.button=installable?L"Install update":L"Open release";
        if(installable && !updates.release.releasePageUrl.empty()) row.iconButtons={L'\uE8A7'}; // Open the release page.
        break;
    }
    case Phase::Downloading: row.detail=L"Downloading the update..."; row.button=L"Install update"; row.buttonEnabled=false; break;
    case Phase::Failed: row.detail=updates.message; row.pill=L"Not updated"; row.pillTone=RowList::Tone::Warning; break;
    }
    return row;
}
void UpdateVersionButton() {
    if(updates.phase!=Updates::Phase::Available) StartUpdateCheck(false);
    else if(!updates.release.assetDownloadUrl.empty()) BeginUpdateInstall();
    else UpdateChecker::OpenReleasePage(updates.release.releasePageUrl);
}
