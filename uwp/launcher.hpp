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
    std::string play_blocked; // when set, Play shows this instead of starting (launcher-only builds)
};

// Decrypted executable: LocalState\game\EBOOT.BIN, else the packaged one.
std::string locate_eboot(const LauncherConfig &config);

// Runs the start screen.  Returns true when the player chose Play; `game`
// then holds the disc image (unset when the build needs no disc).  SDL's
// window is closed again before returning so the game can open its own.
bool run_launcher(const LauncherConfig &config, GameImage &game);

} // namespace pspweb::uwp
