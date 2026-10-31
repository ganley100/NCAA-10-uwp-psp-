// UWP / Xbox One entry point.
//
// SDL2's WinRT backend owns the CoreWindow, input (Windows.Gaming.Input) and
// WASAPI audio; once it is running it calls uwp_main, which shows the start
// screen (uwp/launcher.cpp), then builds the command line the native SDL
// player expects and runs it (main.cpp's main() is compiled as pspweb_main
// for this target, see uwp/uwp.cmake).
//
// Game files: the decrypted EBOOT.BIN ships in the package; the disc comes
// from an ISO imported on the start screen (LocalState\game\disc and
// disc.iso) or, for -PackDisc builds, from the package.  Saves live in
// LocalState\ms0 and the log in LocalState\pspweb.log.

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

#ifdef PSPWEB_NCAA10
constexpr const char *kTitle = "NCAA Football 10";
constexpr const char *kDiscId = "ULUS10440";
#else
constexpr const char *kTitle = "PSP Recomp UWP";
constexpr const char *kDiscId = "";
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

    // stdout and stderr both go to one unbuffered log readable from Device Portal.
    const std::string log = config.local + "\\pspweb.log";
    if (std::freopen(log.c_str(), "w", stdout) != nullptr) {
        _dup2(_fileno(stdout), _fileno(stderr));
        std::setvbuf(stdout, nullptr, _IONBF, 0);
        std::setvbuf(stderr, nullptr, _IONBF, 0);
    }

    // Keep the B button for the game instead of letting the shell navigate back.
    SDL_SetHint(SDL_HINT_WINRT_HANDLE_BACK_BUTTON, "1");

    pspweb::uwp::GameFiles files;
    if (!pspweb::uwp::run_launcher(config, files)) {
        std::fprintf(stderr, "[uwp] launcher closed without starting the game\n");
        return 0;
    }

    std::error_code ignored;
    std::string memstick = config.local + "\\ms0";
    std::filesystem::create_directories(std::filesystem::path(memstick), ignored);
    // Raw UMD sector reads (NCAA's disc streaming) go to this image.
    if (!files.iso.empty()) _putenv_s("PSPWEB_ISO", files.iso.c_str());
    std::fprintf(stderr, "[uwp] eboot=%s disc=%s iso=%s saves=%s\n", files.eboot.c_str(), files.disc.c_str(),
                 files.iso.c_str(), memstick.c_str());

    std::string exe = "pspweb", disc_flag = "--disc", ms_flag = "--ms", realtime_flag = "--realtime", one = "1";
    std::vector<char *> argv{exe.data(), files.eboot.data(), disc_flag.data(), files.disc.data(),
                             ms_flag.data(), memstick.data(), realtime_flag.data(), one.data(), nullptr};
    const int result = pspweb_main(static_cast<int>(argv.size()) - 1, argv.data());
    std::fprintf(stderr, "[uwp] exited with %d\n", result);
    return result;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    if (FAILED(RoInitialize(RO_INIT_MULTITHREADED))) return 1;
    return SDL_WinRTRunApp(uwp_main, nullptr);
}
