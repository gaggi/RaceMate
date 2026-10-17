#pragma once
#include "TrackMap.h"
#include <atomic>
#include <filesystem>

namespace rm::trackmap {
struct Loaded {
    Result map;
    bool fromCache=false;
    bool cached=false;
};
// Only the official HTTPS CDN is contacted, and only while an enabled map needs a layout.
Loaded Load(const Source&,const std::filesystem::path& cache,const std::atomic_bool& cancelled,bool labels=false);
}
