// UWP / Xbox One entry point.
//
// SDL2's WinRT backend owns the CoreWindow, input (Windows.Gaming.Input) and
// WASAPI audio; once it is running it calls uwp_main, which builds the command
// line the native SDL player expects and runs it (main.cpp's main() is
// compiled as pspweb_main for this target, see uwp/uwp.cmake).
//
// Game files are looked up in LocalState\game first (copied there with the
// Xbox Device Portal file explorer) and then in the package's own game\
// folder.  Saves live in LocalState\ms0 and the log in LocalState\pspweb.log.

#include <SDL.h>
#include <SDL_main.h>

#include <io.h>
#include <roapi.h>
#include <windows.h>

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

int pspweb_main(int argc, char **argv);

namespace {

std::string winrt_path(SDL_WinRT_Path which) {
    const char *path = SDL_WinRTGetFSPathUTF8(which);
    return path != nullptr ? path : ".";
}

int uwp_main(int, char **) {
    namespace fs = std::filesystem;
    const std::string installed = winrt_path(SDL_WINRT_PATH_INSTALLED_LOCATION);
    const std::string local = winrt_path(SDL_WINRT_PATH_LOCAL_FOLDER);

    // stdout and stderr both go to one unbuffered log readable from Device Portal.
    const std::string log = local + "\\pspweb.log";
    if (std::freopen(log.c_str(), "w", stdout) != nullptr) {
        _dup2(_fileno(stdout), _fileno(stderr));
        std::setvbuf(stdout, nullptr, _IONBF, 0);
        std::setvbuf(stderr, nullptr, _IONBF, 0);
    }

    // Keep the B button for the game instead of letting the shell navigate back.
    SDL_SetHint(SDL_HINT_WINRT_HANDLE_BACK_BUTTON, "1");

    std::error_code ignored;
    const std::string sideloaded = local + "\\game";
    const std::string root = fs::exists(sideloaded + "\\EBOOT.BIN", ignored) ? sideloaded : installed + "\\game";
    std::string eboot = root + "\\EBOOT.BIN";
    std::string disc = root + "\\disc";
    std::string memstick = local + "\\ms0";
    fs::create_directories(memstick, ignored);
    std::fprintf(stderr, "[uwp] game=%s saves=%s\n", root.c_str(), memstick.c_str());
    if (!fs::exists(eboot, ignored)) {
        std::fprintf(stderr, "[uwp] missing %s\n", eboot.c_str());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Game files missing",
                                 "EBOOT.BIN was not found in the package or in LocalState\\game.", nullptr);
        return 1;
    }

    std::string exe = "pspweb", disc_flag = "--disc", ms_flag = "--ms", realtime_flag = "--realtime", one = "1";
    std::vector<char *> argv{exe.data(), eboot.data(), disc_flag.data(), disc.data(),
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
