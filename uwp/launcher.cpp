#include "launcher.hpp"

#include <SDL.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

namespace pspweb::uwp {
namespace {

namespace fs = std::filesystem;

constexpr int kWidth = 1280, kHeight = 720;
constexpr float kPanelX = 760.0f, kTextX = 790.0f, kTextWidth = 460.0f;
constexpr SDL_Color kWhite{255, 255, 255, 255}, kGrey{170, 170, 170, 255}, kDim{110, 110, 110, 255},
    kGold{255, 196, 37, 255}, kBlack{0, 0, 0, 255}, kRed{255, 110, 100, 255};

fs::path utf8_path(const std::string &text) { return fs::path(std::u8string(text.begin(), text.end())); }
std::string utf8(const fs::path &path) {
    const auto text = path.u8string();
    return std::string(text.begin(), text.end());
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

} // namespace

std::string locate_eboot(const LauncherConfig &config) {
    std::error_code ignored;
    const fs::path local = utf8_path(config.local) / "game" / "EBOOT.BIN";
    return utf8(fs::exists(local, ignored) ? local : utf8_path(config.installed) / "game" / "EBOOT.BIN");
}

bool run_launcher(const LauncherConfig &config, GameImage &game) {
    Ui ui;
    const bool have_ui = ui.open(config.installed, config.title);
    if (!have_ui) std::fprintf(stderr, "[launcher] SDL unavailable: %s\n", SDL_GetError());
    const auto places = search_places(config.installed, config.local);
    const std::string instructions =
        "Put your " + config.title + " PSP game file (.iso, .cso or .chd) in Development Files "
        "(D:\\DevelopmentFiles, the default), on the D: drive, in LocalState, or on a USB drive, "
        "then press A.";
    std::error_code ignored;
    for (;;) {
        // Search (again).
        if (have_ui) ui.busy("Looking for " + config.title + "...", "", -1.0);
        std::vector<std::string> notes;
        std::string problem;
        game = GameImage{};
        if (!fs::exists(utf8_path(locate_eboot(config)), ignored))
            problem = "The game executable (EBOOT.BIN) is missing from this package. Rebuild it with yours.";
        else if (config.needs_disc && !find_game(places, config.disc_id, game, notes))
            problem = instructions;
        const bool ready = problem.empty();
        if (ready) std::fprintf(stderr, "[launcher] found %s in %s\n", game.path.c_str(), game.location.c_str());
        else std::fprintf(stderr, "[launcher] %s\n", problem.c_str());
        for (const auto &note : notes) std::fprintf(stderr, "[launcher] skipped %s\n", note.c_str());
        if (!have_ui) return ready;

        std::string detail;
        if (ready && config.needs_disc) detail = "Found " + game.name + " in " + game.location + ".";
        for (const auto &note : notes) detail += (detail.empty() ? "" : "\n") + note;
        for (;;) {
            const Input input = ui.poll();
            if (input.quit) {
                ui.close();
                return false;
            }
            if (input.accept && ready) {
                ui.close();
                return true;
            }
            if (input.accept) break; // search again
            ui.begin();
            float y = ui.paragraph(190.0f, ready ? "Ready to play." : problem, 0.42f, ready ? kWhite : kRed);
            ui.paragraph(y + 16.0f, detail, 0.34f, kGrey);
            ui.item(560.0f, ready ? "Play" : "Search again", true, true);
            ui.footer(ready ? "A play" : "A search again");
            ui.present();
        }
    }
}

} // namespace pspweb::uwp
