#include "game_locator.hpp"

#include "webfs.hpp"

#include <fcntl.h>
#include <io.h>
#include <windows.h>
#include <fileapifromapp.h>

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <unordered_map>

namespace pspweb::uwp {
namespace {

std::string utf8(const std::wstring &text) {
    if (text.empty()) return {};
    const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), length, nullptr, nullptr);
    return out;
}

std::wstring wide(const std::string &text) {
    if (text.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), length);
    return out;
}

bool is_disc_image(std::wstring name) {
    std::transform(name.begin(), name.end(), name.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    for (const wchar_t *ext : {L".iso", L".cso", L".chd"})
        if (name.size() > 4u && name.compare(name.size() - 4u, 4u, ext) == 0) return true;
    return false;
}

// The *FromApp file APIs reach folders outside the app container when the
// package declares broadFileSystemAccess (Development Files, USB drives).
void list_folder(const std::wstring &folder, std::vector<std::wstring> &images, std::vector<std::wstring> *subfolders) {
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileExFromAppW((folder + L"\\*").c_str(), FindExInfoBasic, &data, FindExSearchNameMatch, nullptr, 0);
    if (find == INVALID_HANDLE_VALUE) return;
    do {
        const std::wstring name = data.cFileName;
        if (name == L"." || name == L"..") continue;
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (subfolders) subfolders->push_back(folder + L"\\" + name);
        } else if (is_disc_image(name)) {
            images.push_back(folder + L"\\" + name);
        }
    } while (FindNextFileW(find, &data));
    FindClose(find);
}

std::FILE *open_read(const std::wstring &path) {
    HANDLE handle = CreateFileFromAppW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                       FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return nullptr;
    const int fd = _open_osfhandle(reinterpret_cast<intptr_t>(handle), _O_RDONLY | _O_BINARY);
    if (fd < 0) {
        CloseHandle(handle);
        return nullptr;
    }
    std::FILE *file = _fdopen(fd, "rb");
    if (!file) _close(fd);
    return file;
}

std::wstring file_name(const std::wstring &path) {
    const auto slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1u);
}

} // namespace

std::vector<SearchPlace> search_places(const std::string &installed, const std::string &local) {
    std::vector<SearchPlace> places{{"Development Files (D:\\DevelopmentFiles)", L"D:\\DevelopmentFiles"},
                                    {"the D: drive", L"D:"},
                                    {"LocalState", wide(local)}};
    for (wchar_t drive = L'E'; drive <= L'Z'; ++drive)
        places.push_back({std::string("USB drive ") + static_cast<char>(drive) + ":", std::wstring(1, drive) + L":"});
    places.push_back({"the app package", wide(installed) + L"\\game"});
    return places;
}

bool find_game(const std::vector<SearchPlace> &places, const std::string &disc_id, GameImage &game,
               std::vector<std::string> &notes) {
    for (const auto &place : places) {
        std::vector<std::wstring> images, subfolders;
        list_folder(place.folder, images, &subfolders);
        for (const auto &folder : subfolders) list_folder(folder, images, nullptr);
        for (const auto &path : images) {
            const std::string name = utf8(file_name(path));
            std::string error;
            std::shared_ptr<DiscImage> image = DiscImage::open(open_read(path), name, error);
            std::vector<IsoEntry> entries;
            if (image) error = list_iso(*image, entries);
            if (!error.empty()) {
                notes.push_back(name + " (" + place.label + "): " + error);
                continue;
            }
            const std::string id = read_disc_id(*image, entries);
            if (!disc_id.empty() && id != disc_id) {
                notes.push_back(name + " (" + place.label + ") is " + (id.empty() ? "not a PSP game" : id) +
                                ", not " + disc_id);
                continue;
            }
            game = GameImage{utf8(path), name, place.label, id, std::move(image), std::move(entries)};
            return true;
        }
    }
    return false;
}

std::string serve_game(const GameImage &game, const std::string &local) {
    struct Span {
        std::uint64_t offset, size;
    };
    auto spans = std::make_shared<std::unordered_map<std::string, Span>>();
    const std::string manifest = local + "\\disc.manifest";
    std::ofstream out(wide(manifest), std::ios::binary | std::ios::trunc);
    for (const auto &entry : game.entries) {
        if (entry.directory) {
            out << "D " << entry.path << "\n";
        } else {
            out << "F " << entry.size << " " << entry.path << "\n";
            (*spans)[entry.path] = {entry.offset, entry.size};
        }
    }
    // Raw UMD sector reads (NCAA streams its data this way) see the whole image.
    out << "F " << game.image->size() << " __umd.iso\n";
    (*spans)["__umd.iso"] = {0u, game.image->size()};
    out.close();

    std::shared_ptr<DiscImage> image = game.image;
    set_native_disc_reader([image, spans](const std::string &path, std::uint64_t offset, std::uint8_t *dst,
                                          std::size_t length) {
        const auto found = spans->find(path);
        if (found == spans->end() || offset + length > found->second.size) return false;
        return image->read(found->second.offset + offset, dst, length);
    });
    return manifest;
}

} // namespace pspweb::uwp
