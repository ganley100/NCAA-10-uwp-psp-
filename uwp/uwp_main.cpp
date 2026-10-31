// UWP / Xbox One entry point.
//
// SDL2's WinRT backend owns the CoreWindow, input (Windows.Gaming.Input) and
// WASAPI audio; once it is running it calls uwp_main, which shows the start
// screen (uwp/launcher.cpp), then builds the command line the native SDL
// player expects and runs it (main.cpp's main() is compiled as pspweb_main
// for this target, see uwp/uwp.cmake).
//
// Game files: the decrypted EBOOT.BIN ships in the package; the disc is the
// player's .iso / .cso / .chd found in Development Files, LocalState or on a
// USB drive (uwp/game_locator.cpp) and is read in place through webfs.  Saves
// live in LocalState\ms0 and the log in LocalState\pspweb.log.

#include "launcher.hpp"

#include <SDL.h>
#include <SDL_main.h>

#include <io.h>
#include <roapi.h>
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

int pspweb_main(int argc, char **argv);

namespace {

#if defined(PSPWEB_NCAA10) || defined(PSPWEB_UWP_LAUNCHER_ONLY)
constexpr const char *kTitle = "NCAA 10 PSP";
constexpr const char *kDiscId = "ULUS10440";
constexpr bool kNeedsDisc = true;
#else
constexpr const char *kTitle = "NCAA 10 PSP (test)";
constexpr const char *kDiscId = "";
constexpr bool kNeedsDisc = false; // the smoke package's synthetic program reads no disc
#endif

std::string winrt_path(SDL_WinRT_Path which) {
    const char *path = SDL_WinRTGetFSPathUTF8(which);
    return path != nullptr ? path : ".";
}

int uwp_main(int, char **) {
    pspweb::uwp::LauncherConfig config;
    config.installed = winrt_path(SDL_WINRT_PATH_INSTALLED_LOCATION);
    config.local = winrt_path(SDL_WINRT_PATH_LOCAL_FOLDER);
    config.title = kTitle;
    config.disc_id = kDiscId;
    config.needs_disc = kNeedsDisc;
#ifdef PSPWEB_UWP_LAUNCHER_ONLY
    config.play_blocked = "This is the launcher only: the NCAA 10 game code is not in this build yet. "
                          "Install the full NCAA 10 PSP package (built from your EBOOT.BIN) to play.";
#endif

    // stdout and stderr both go to one unbuffered log readable from Device Portal.
    const std::string log = config.local + "\\pspweb.log";
    if (std::freopen(log.c_str(), "w", stdout) != nullptr) {
        _dup2(_fileno(stdout), _fileno(stderr));
        std::setvbuf(stdout, nullptr, _IONBF, 0);
        std::setvbuf(stderr, nullptr, _IONBF, 0);
    }

    // Keep the B button for the game instead of letting the shell navigate back.
    SDL_SetHint(SDL_HINT_WINRT_HANDLE_BACK_BUTTON, "1");

    pspweb::uwp::GameImage game;
    if (!pspweb::uwp::run_launcher(config, game)) {
        std::fprintf(stderr, "[uwp] launcher closed without starting the game\n");
        return 0;
    }

    std::error_code ignored;
    std::string eboot = pspweb::uwp::locate_eboot(config);
    std::string disc = config.local + "\\disc", memstick = config.local + "\\ms0";
    std::filesystem::create_directories(std::filesystem::path(memstick), ignored);
    std::filesystem::create_directories(std::filesystem::path(disc), ignored);
    std::string exe = "pspweb", disc_flag = "--disc", ms_flag = "--ms", realtime_flag = "--realtime", one = "1";
    std::vector<char *> argv{exe.data(), eboot.data(), disc_flag.data(), disc.data(), ms_flag.data(),
                             memstick.data(), realtime_flag.data(), one.data()};
    // The disc is read in place from the image through webfs (raw UMD reads included).
    std::string webfs_flag = "--webfs", manifest;
    if (game.image) {
        manifest = pspweb::uwp::serve_game(game, config.local);
        argv.push_back(webfs_flag.data());
        argv.push_back(manifest.data());
    }
    std::fprintf(stderr, "[uwp] eboot=%s image=%s saves=%s\n", eboot.c_str(), game.path.c_str(), memstick.c_str());
    argv.push_back(nullptr);
    const int result = pspweb_main(static_cast<int>(argv.size()) - 1, argv.data());
    std::fprintf(stderr, "[uwp] exited with %d\n", result);
    return result;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    if (FAILED(RoInitialize(RO_INIT_MULTITHREADED))) return 1;
    return SDL_WinRTRunApp(uwp_main, nullptr);
}
