// Start screen for the UWP / Xbox build: shows the cover and either where to
// put the game file or which file was found, then starts the game.
#pragma once

#include "game_locator.hpp"

#include <string>

namespace pspweb::uwp {

struct LauncherConfig {
    std::string installed; // package folder
    std::string local;     // LocalState folder
    std::string title;
    std::string disc_id;   // required DISC_ID, empty for any PSP disc
    bool needs_disc{true}; // false for the game-free smoke package
    // Launcher-only package: Play starts the separately installed game app
    // through this protocol (e.g. "ncaa10psp-game:") instead of running here.
    std::string launch_uri;
    // Game package: start as soon as the game file is found (the launcher app
    // already showed the start screen).
    bool auto_play{false};
};

// Decrypted executable: LocalState\game\EBOOT.BIN, else the packaged one.
std::string locate_eboot(const LauncherConfig &config);

// Runs the start screen.  Returns true when the game should run in this
// process (`game` then holds the disc image, unset when the build needs no
// disc); false to exit, including after handing over to the game app.  SDL's
// window is closed again before returning so the game can open its own.
bool run_launcher(const LauncherConfig &config, GameImage &game);

} // namespace pspweb::uwp
