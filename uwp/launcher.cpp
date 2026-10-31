#include "launcher.hpp"

#include "iso9660.hpp"

#include <SDL.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.FileProperties.h>
#include <winrt/Windows.Storage.Pickers.h>
#include <winrt/Windows.Storage.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

namespace pspweb::uwp {
namespace {

namespace fs = std::filesystem;
namespace wf = winrt::Windows::Foundation;
namespace ws = winrt::Windows::Storage;

constexpr int kWidth = 1280, kHeight = 720;
constexpr float kPanelX = 760.0f, kTextX = 790.0f, kTextWidth = 460.0f;
constexpr SDL_Color kWhite{255, 255, 255, 255}, kGrey{170, 170, 170, 255}, kDim{110, 110, 110, 255},
    kGold{255, 196, 37, 255}, kBlack{0, 0, 0, 255}, kRed{255, 110, 100, 255};

fs::path utf8_path(const std::string &text) { return fs::path(std::u8string(text.begin(), text.end())); }
std::string utf8(const fs::path &path) {
    const auto text = path.u8string();
    return std::string(text.begin(), text.end());
}

bool ends_with_iso(std::string name) {
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return name.size() > 4u && name.compare(name.size() - 4u, 4u, ".iso") == 0;
}

struct Input {
    bool up{}, down{}, accept{}, back{}, quit{};
};

// SDL drawing for the start screen: background image, a dark side panel and
// text from a monospace glyph atlas (Assets\font.bmp, see make_launcher.py).
class Ui {
public:
    bool open(const std::string &installed, const std::string &title) {
        title_ = title;
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) return false;
        window_ = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, kWidth, kHeight, 0);
        if (!window_) return false;
        renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!renderer_) renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
        if (!renderer_) return false;
        SDL_RenderSetLogicalSize(renderer_, kWidth, kHeight);
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        const fs::path assets = utf8_path(installed) / "Assets";
        if (SDL_Surface *surface = SDL_LoadBMP(utf8(assets / "launcher.bmp").c_str())) {
            background_ = SDL_CreateTextureFromSurface(renderer_, surface);
            SDL_FreeSurface(surface);
        }
        load_font(assets);
        for (int i = 0; i < SDL_NumJoysticks(); ++i) add_pad(i);
        return true;
    }

    void close() {
        for (auto *pad : pads_) SDL_GameControllerClose(pad);
        pads_.clear();
        if (font_) SDL_DestroyTexture(font_);
        if (background_) SDL_DestroyTexture(background_);
        if (renderer_) SDL_DestroyRenderer(renderer_);
        if (window_) SDL_DestroyWindow(window_);
        font_ = background_ = nullptr;
        renderer_ = nullptr;
        window_ = nullptr;
    }

    Input poll() {
        Input input;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_QUIT: input.quit = true; break;
            case SDL_CONTROLLERDEVICEADDED: add_pad(event.cdevice.which); break;
            case SDL_CONTROLLERBUTTONDOWN:
                switch (event.cbutton.button) {
                case SDL_CONTROLLER_BUTTON_DPAD_UP: input.up = true; break;
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN: input.down = true; break;
                case SDL_CONTROLLER_BUTTON_A:
                case SDL_CONTROLLER_BUTTON_START: input.accept = true; break;
                case SDL_CONTROLLER_BUTTON_B: input.back = true; break;
                default: break;
                }
                break;
            case SDL_CONTROLLERAXISMOTION:
                if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
                    const int zone = event.caxis.value < -20000 ? -1 : (event.caxis.value > 20000 ? 1 : 0);
                    if (zone != stick_zone_ && zone < 0) input.up = true;
                    if (zone != stick_zone_ && zone > 0) input.down = true;
                    stick_zone_ = zone;
                }
                break;
            case SDL_KEYDOWN:
                switch (event.key.keysym.scancode) {
                case SDL_SCANCODE_UP: input.up = true; break;
                case SDL_SCANCODE_DOWN: input.down = true; break;
                case SDL_SCANCODE_RETURN:
                case SDL_SCANCODE_SPACE: input.accept = true; break;
                case SDL_SCANCODE_ESCAPE:
                case SDL_SCANCODE_BACKSPACE:
                case SDL_SCANCODE_AC_BACK: input.back = true; break;
                default: break;
                }
                break;
            default: break;
            }
        }
        return input;
    }

    // Background, side panel and title; the caller adds content and presents.
    void begin() {
        SDL_SetRenderDrawColor(renderer_, 10, 14, 12, 255);
        SDL_RenderClear(renderer_);
        if (background_) SDL_RenderCopy(renderer_, background_, nullptr, nullptr);
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 175);
        const SDL_FRect panel{kPanelX, 0.0f, kWidth - kPanelX, static_cast<float>(kHeight)};
        SDL_RenderFillRectF(renderer_, &panel);
        SDL_SetRenderDrawColor(renderer_, kGold.r, kGold.g, kGold.b, 255);
        const SDL_FRect rule{kTextX, 138.0f, 120.0f, 4.0f};
        SDL_RenderFillRectF(renderer_, &rule);
        text_fit(kTextX, 60.0f, title_, 0.8f, kWhite);
    }

    void present() {
        SDL_RenderPresent(renderer_);
        SDL_Delay(1); // vsync paces the loop; keep a floor if it is unavailable
    }

    float text(float x, float y, const std::string &line, float scale, SDL_Color colour) {
        if (!font_) return y;
        SDL_SetTextureColorMod(font_, colour.r, colour.g, colour.b);
        float pen = x;
        for (const unsigned char c : line) {
            const int index = (c < 32u || c > 126u ? '?' : c) - 32;
            const SDL_Rect src{(index % 16) * cell_w_, (index / 16) * cell_h_, cell_w_, cell_h_};
            const SDL_FRect dst{pen, y, cell_w_ * scale, cell_h_ * scale};
            SDL_RenderCopyF(renderer_, font_, &src, &dst);
            pen += advance_ * scale;
        }
        return y + cell_h_ * scale;
    }

    void text_fit(float x, float y, const std::string &line, float scale, SDL_Color colour) {
        const float fit = line.empty() ? scale : kTextWidth / (advance_ * static_cast<float>(line.size()));
        text(x, y, line, std::min(scale, fit), colour);
    }

    // Word-wrapped paragraph inside the panel; returns the y below it.
    float paragraph(float y, const std::string &body, float scale, SDL_Color colour) {
        const auto per_line = std::max<std::size_t>(8u, static_cast<std::size_t>(kTextWidth / (advance_ * scale)));
        std::string line, word;
        auto flush = [&] {
            if (!line.empty()) y = text(kTextX, y, line, scale, colour);
            line.clear();
        };
        for (std::size_t i = 0; i <= body.size(); ++i) {
            const char c = i < body.size() ? body[i] : ' ';
            if (c != ' ' && c != '\n') {
                word += c;
                continue;
            }
            if (!line.empty() && line.size() + 1u + word.size() > per_line) flush();
            while (word.size() > per_line) { // overlong words (paths) break anywhere
                line = word.substr(0, per_line);
                word.erase(0, per_line);
                flush();
            }
            if (!word.empty()) line += (line.empty() ? "" : " ") + word;
            word.clear();
            if (c == '\n') flush();
        }
        flush();
        return y;
    }

    void item(float y, const std::string &label, bool selected, bool enabled) {
        if (selected) {
            SDL_SetRenderDrawColor(renderer_, kGold.r, kGold.g, kGold.b, 235);
            const SDL_FRect bar{kTextX - 14.0f, y - 6.0f, kTextWidth + 28.0f, 52.0f};
            SDL_RenderFillRectF(renderer_, &bar);
        }
        text_fit(kTextX, y, label, 0.58f, selected ? kBlack : (enabled ? kWhite : kDim));
    }

    void footer(const std::string &hint) { text_fit(kTextX, 664.0f, hint, 0.36f, kGrey); }

    // One frame of a long operation: fraction in [0, 1], or < 0 when unknown.
    void busy(const std::string &line, const std::string &detail, double fraction) {
        poll();
        begin();
        float y = paragraph(200.0f, line, 0.46f, kWhite);
        paragraph(y + 12.0f, detail, 0.38f, kGrey);
        SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 60);
        const SDL_FRect track{kTextX, 470.0f, kTextWidth, 14.0f};
        SDL_RenderFillRectF(renderer_, &track);
        SDL_SetRenderDrawColor(renderer_, kGold.r, kGold.g, kGold.b, 255);
        if (fraction >= 0.0) {
            const SDL_FRect fill{kTextX, 470.0f, static_cast<float>(kTextWidth * std::min(1.0, fraction)), 14.0f};
            SDL_RenderFillRectF(renderer_, &fill);
        } else {
            const float t = static_cast<float>(SDL_GetTicks() % 1600u) / 1600.0f;
            const SDL_FRect block{kTextX + (kTextWidth - 90.0f) * t, 470.0f, 90.0f, 14.0f};
            SDL_RenderFillRectF(renderer_, &block);
        }
        present();
    }

private:
    void add_pad(int index) {
        if (!SDL_IsGameController(index)) return;
        if (SDL_GameController *pad = SDL_GameControllerOpen(index)) pads_.push_back(pad);
    }

    void load_font(const fs::path &assets) {
        std::ifstream metrics(assets / "font.txt");
        metrics >> cell_w_ >> cell_h_ >> advance_;
        SDL_Surface *loaded = SDL_LoadBMP(utf8(assets / "font.bmp").c_str());
        if (!loaded || cell_w_ <= 0 || cell_h_ <= 0) return;
        SDL_Surface *surface = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_ARGB8888, 0);
        SDL_FreeSurface(loaded);
        if (!surface) return;
        // White glyphs on black: brightness becomes alpha, colour comes from ColorMod.
        SDL_LockSurface(surface);
        for (int y = 0; y < surface->h; ++y) {
            auto *row = reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(surface->pixels) + y * surface->pitch);
            for (int x = 0; x < surface->w; ++x) row[x] = ((row[x] >> 16u) & 0xFFu) << 24u | 0x00FFFFFFu;
        }
        SDL_UnlockSurface(surface);
        font_ = SDL_CreateTextureFromSurface(renderer_, surface);
        SDL_FreeSurface(surface);
        if (font_) SDL_SetTextureBlendMode(font_, SDL_BLENDMODE_BLEND);
    }

    std::string title_;
    SDL_Window *window_{};
    SDL_Renderer *renderer_{};
    SDL_Texture *background_{}, *font_{};
    std::vector<SDL_GameController *> pads_;
    int cell_w_{}, cell_h_{}, advance_{};
    int stick_zone_{};
};

// Waits for a WinRT async operation on the UI thread without blocking it:
// SDL keeps pumping the CoreWindow dispatcher while the screen animates.
template <typename Operation>
auto wait(Operation const &operation, Ui &ui, const std::string &line) {
    const Uint32 start = SDL_GetTicks();
    while (operation.Status() == wf::AsyncStatus::Started) {
        const unsigned seconds = (SDL_GetTicks() - start) / 1000u;
        ui.busy(line, seconds > 1u ? std::to_string(seconds) + " s" : "", -1.0);
    }
    return operation.GetResults();
}

std::string describe(const winrt::hresult_error &error) {
    const std::string message = winrt::to_string(error.message());
    return message.empty() ? "Windows error " + std::to_string(static_cast<std::int32_t>(error.code())) : message;
}

// Copies the chosen image into LocalState\game, extracts it next to it and
// checks the disc ID.  Returns an empty string on success.
std::string import_iso(ws::StorageFile const &file, const LauncherConfig &config, Ui &ui) {
    const fs::path game = utf8_path(config.local) / "game";
    const fs::path staging = game / "disc.new", image = game / "import.iso";
    std::error_code ignored;
    fs::create_directories(game, ignored);
    const auto folder = wait(ws::ApplicationData::Current().LocalFolder().CreateFolderAsync(
                                 L"game", ws::CreationCollisionOption::OpenIfExists), ui, "Preparing...");
    const std::uint64_t size = wait(file.GetBasicPropertiesAsync(), ui, "Preparing...").Size();
    char gigabytes[32];
    std::snprintf(gigabytes, sizeof gigabytes, "%.2f GB", static_cast<double>(size) / 1e9);
    wait(file.CopyAsync(folder, L"import.iso", ws::NameCollisionOption::ReplaceExisting), ui,
         "Copying " + winrt::to_string(file.Name()) + " (" + gigabytes + "). This takes a few minutes.");

    fs::remove_all(staging, ignored);
    ExtractProgress progress;
    std::atomic<bool> finished{false};
    std::string error;
    std::thread worker([&] {
        error = extract_iso(image, staging, progress);
        finished = true;
    });
    while (!finished) {
        const double total = static_cast<double>(progress.total.load());
        ui.busy("Extracting the disc...", "", total > 0.0 ? progress.done.load() / total : -1.0);
    }
    worker.join();
    auto discard = [&] {
        fs::remove_all(staging, ignored);
        fs::remove(image, ignored);
    };
    if (!error.empty()) {
        discard();
        return "Import failed: " + error + ".";
    }
    const std::string id = read_disc_id(staging);
    if (!config.disc_id.empty() && id != config.disc_id) {
        discard();
        return (id.empty() ? std::string("That is not a PSP game disc") : "That disc is " + id) +
               "; this build needs " + config.disc_id + ".";
    }
    fs::remove_all(game / "disc", ignored);
    fs::rename(staging, game / "disc");
    fs::remove(game / "disc.iso", ignored);
    fs::rename(image, game / "disc.iso");
    return {};
}

ws::StorageFile pick_iso(Ui &ui) {
    ws::Pickers::FileOpenPicker picker;
    picker.ViewMode(ws::Pickers::PickerViewMode::List);
    picker.FileTypeFilter().Append(L".iso");
    return wait(picker.PickSingleFileAsync(), ui, "Choose your disc image (.iso) in the file picker.");
}

std::vector<ws::StorageFile> scan_usb(Ui &ui) {
    std::vector<ws::StorageFile> found;
    auto collect = [&](ws::StorageFolder const &folder) {
        for (auto const &file : wait(folder.GetFilesAsync(), ui, "Searching USB drives..."))
            if (ends_with_iso(winrt::to_string(file.Name()))) found.push_back(file);
    };
    for (auto const &drive : wait(ws::KnownFolders::RemovableDevices().GetFoldersAsync(), ui, "Searching USB drives...")) {
        collect(drive);
        for (auto const &folder : wait(drive.GetFoldersAsync(), ui, "Searching USB drives...")) collect(folder);
    }
    return found;
}

// Lets the player choose one of several images; returns -1 when cancelled.
int choose(Ui &ui, const std::vector<std::string> &names) {
    int selected = 0;
    for (;;) {
        const Input input = ui.poll();
        if (input.quit || input.back) return -1;
        if (input.accept) return selected;
        if (input.up) selected = (selected + static_cast<int>(names.size()) - 1) % static_cast<int>(names.size());
        if (input.down) selected = (selected + 1) % static_cast<int>(names.size());
        ui.begin();
        ui.text(kTextX, 190.0f, "Choose a disc image", 0.46f, kWhite);
        const int first = std::max(0, std::min(selected - 3, static_cast<int>(names.size()) - 7));
        for (int i = first; i < std::min<int>(first + 7, static_cast<int>(names.size())); ++i)
            ui.item(270.0f + (i - first) * 56.0f, names[static_cast<std::size_t>(i)], i == selected, true);
        ui.footer("A choose   B back");
        ui.present();
    }
}

} // namespace

GameFiles locate_game(const LauncherConfig &config) {
    std::error_code ignored;
    const fs::path local = utf8_path(config.local) / "game", installed = utf8_path(config.installed) / "game";
    GameFiles files;
    const fs::path eboot = fs::exists(local / "EBOOT.BIN", ignored) ? local / "EBOOT.BIN" : installed / "EBOOT.BIN";
    const fs::path disc = fs::exists(local / "disc" / "PSP_GAME", ignored) ? local / "disc" : installed / "disc";
    fs::path iso;
    if (fs::exists(local / "disc.iso", ignored) && disc == local / "disc") iso = local / "disc.iso";
    else if (fs::exists(disc / "__umd.iso", ignored)) iso = disc / "__umd.iso";
    files.eboot = utf8(eboot);
    files.disc = utf8(disc);
    files.iso = iso.empty() ? std::string() : utf8(iso);
    if (!fs::exists(eboot, ignored)) {
        files.problem = "The game executable is missing from this package. Rebuild it with your EBOOT.BIN.";
    } else if (!config.disc_id.empty()) {
        const std::string id = read_disc_id(disc);
        if (id.empty() || iso.empty())
            files.problem = "Import your " + config.title + " disc image (" + config.disc_id +
                            ", .iso) from a USB drive to play.";
        else if (id != config.disc_id)
            files.problem = "The imported disc is " + id + ", but this build needs " + config.disc_id + ".";
    }
    files.ready = files.problem.empty();
    return files;
}

bool run_launcher(const LauncherConfig &config, GameFiles &files) {
    Ui ui;
    if (!ui.open(config.installed, config.title)) {
        std::fprintf(stderr, "[launcher] SDL unavailable: %s\n", SDL_GetError());
        ui.close();
        files = locate_game(config);
        return files.ready;
    }
    files = locate_game(config);
    std::string message = files.ready ? "Ready to play." : files.problem;
    bool message_is_error = !files.ready;
    int selected = files.ready ? 0 : 1;
    const std::vector<std::string> items{"Play", "Choose ISO file", "Find ISO on USB drive"};
    for (;;) {
        const Input input = ui.poll();
        if (input.quit) break;
        if (input.up) selected = (selected + 2) % 3;
        if (input.down) selected = (selected + 1) % 3;
        if (input.accept) {
            if (selected == 0 && files.ready) {
                ui.close();
                return true;
            }
            if (selected == 0) {
                message = files.problem;
                message_is_error = true;
            } else {
                try {
                    ws::StorageFile chosen{nullptr};
                    if (selected == 1) {
                        chosen = pick_iso(ui);
                        if (!chosen) message = "No file chosen.";
                    } else {
                        const auto found = scan_usb(ui);
                        std::vector<std::string> names;
                        for (auto const &file : found) names.push_back(winrt::to_string(file.Path()));
                        if (found.empty()) {
                            message = "No .iso files found in the top two folder levels of any USB drive.";
                        } else {
                            const int index = found.size() == 1u ? 0 : choose(ui, names);
                            if (index >= 0) chosen = found[static_cast<std::size_t>(index)];
                            else message = "Cancelled.";
                        }
                    }
                    if (chosen) {
                        const std::string error = import_iso(chosen, config, ui);
                        message = error.empty() ? "Disc imported." : error;
                    }
                } catch (const winrt::hresult_error &error) {
                    message = "Could not read the drive: " + describe(error);
                } catch (const std::exception &error) {
                    message = std::string("Import failed: ") + error.what();
                }
                files = locate_game(config);
                if (files.ready && message == "Disc imported.") message += " Ready to play.";
                message_is_error = !files.ready;
                if (files.ready) selected = 0;
            }
        }
        ui.begin();
        ui.paragraph(190.0f, message, 0.42f, message_is_error ? kRed : kGrey);
        for (int i = 0; i < 3; ++i)
            ui.item(380.0f + i * 70.0f, items[static_cast<std::size_t>(i)], i == selected, i != 0 || files.ready);
        ui.footer("A select   Up/Down move");
        ui.present();
    }
    ui.close();
    return false;
}

} // namespace pspweb::uwp
