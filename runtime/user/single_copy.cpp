// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
#include <user/single_copy.h>

#include <platform/file_io.h>

#include <format>
#include <system_error>

namespace singlecopy
{
    Claim ClaimDataFolder(const std::filesystem::path& data)
    {
        std::filesystem::path folder = data / "logs";
        std::error_code ec;
        std::filesystem::create_directories(folder, ec);
        bool busy = false;
        intptr_t lock = platform::LockFolder(folder, busy);
        if (lock >= 0)
            return Claim::Ours;  // never unlocked: the process ending lets go
        return busy ? Claim::OtherCopy : Claim::Unguarded;
    }

    Message AlreadyRunning(const char* appName)
    {
        return {
            std::format("{} is already running", appName),
            {
                std::format("Another copy of {} is open. Two copies would share the GPU, so both would play slowly, "
                    "and they would have the same saves open.", appName),
                "Switch to the copy that's open, or close it, then start the game again. On a Steam Deck, press the "
                "Steam button: the game that's running is at the top.",
            },
        };
    }
}
