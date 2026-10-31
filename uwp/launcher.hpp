// Start screen for the UWP / Xbox build: shows the cover, lets the player
// import their disc image (file picker or USB scan) and starts the game.
#pragma once

#include <string>

namespace pspweb::uwp {

struct GameFiles {
    std::string eboot; // decrypted executable (packaged, or LocalState\game)
    std::string disc;  // extracted disc tree
    std::string iso;   // raw image for sector reads (PSPWEB_ISO), may be empty
    bool ready{false};
    std::string problem; // why the game cannot start yet
};

struct LauncherConfig {
    std::string installed; // package folder
    std::string local;      // LocalState folder
    std::string title;
    std::string disc_id;    // required DISC_ID, empty when the title needs no disc
};

// Where the game files are right now (imported files win over packaged ones).
GameFiles locate_game(const LauncherConfig &config);

// Runs the start screen.  Returns true when the player chose Play with the
// game ready; `files` then holds the paths to use.  SDL's window is closed
// again before returning so the game can open its own.
bool run_launcher(const LauncherConfig &config, GameFiles &files);

} // namespace pspweb::uwp
