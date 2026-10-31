// Finds the player's disc image (.iso / .cso / .chd) and serves the game's
// disc reads straight out of it through the webfs layer.
#pragma once

#include "disc_image.hpp"

#include <memory>
#include <string>
#include <vector>

namespace pspweb::uwp {

struct SearchPlace {
    std::string label;  // shown to the player, e.g. "Development Files"
    std::wstring folder;
};

struct GameImage {
    std::string path;     // UTF-8
    std::string name;     // file name only
    std::string location; // SearchPlace::label it was found in
    std::string disc_id;
    std::shared_ptr<DiscImage> image;
    std::vector<IsoEntry> entries;
};

// Development Files first (the default place), then LocalState, USB drives
// and finally a disc packaged with -PackDisc.
std::vector<SearchPlace> search_places(const std::string &installed, const std::string &local);

// First image in `places` (each folder and its direct subfolders) whose disc
// ID matches `disc_id` (any PSP disc when empty).  Problems with other images
// are appended to `notes`.  Returns false when none was found.
bool find_game(const std::vector<SearchPlace> &places, const std::string &disc_id, GameImage &game,
               std::vector<std::string> &notes);

// Writes the webfs manifest for `game` (returned path) and routes disc reads,
// including raw UMD sector reads, to the image.
std::string serve_game(const GameImage &game, const std::string &local);

} // namespace pspweb::uwp
