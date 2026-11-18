#pragma once

// Read-only Windows audio checks for the window program (nothing is changed here).

#include <cstdint>
#include <string>
#include <vector>

namespace lsf {

struct AudioProgram {
    uint32_t pid = 0;
    std::string exe;     // executable name as on disk, e.g. "KOOK.exe"
    bool active = false; // currently playing (session state active)
};

// Programs with an audio session on any active playback device, one entry per executable
// (active if any of its sessions is). Excludes this program and system sounds.
std::vector<AudioProgram> AudioPrograms();

// Programs other than this one with an active session on the given playback endpoint.
std::vector<std::string> ProgramsPlayingTo(const std::wstring &endpoint_id);

} // namespace lsf
