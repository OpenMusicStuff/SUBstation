// VST2 hosting on Windows (see Vst2Platform.h).

#include "plugins/Vst2Platform.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>

#include "platform/Paths.h"

namespace sub::vst2 {

namespace {

std::string knownFolder(REFKNOWNFOLDERID id) {
    PWSTR folder = nullptr;
    std::string result;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &folder))) result = platform::fromNative(folder);
    CoTaskMemFree(folder);
    return result;
}

// HKLM\SOFTWARE\VST\VSTPluginsPath, where installers say the user's VST2 folder is.
std::string registryFolder() {
    wchar_t value[MAX_PATH * 2] = {};
    DWORD size = sizeof(value) - sizeof(wchar_t);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\VST", L"VSTPluginsPath", RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ,
                     nullptr, value, &size) != ERROR_SUCCESS) {
        return {};
    }
    return platform::fromNative(value);
}

}  // namespace

std::vector<std::string> systemPluginFolders() {
    std::vector<std::string> paths;
    const auto add = [&paths](const std::string& folder) {
        if (folder.empty()) return;
        const std::string key = platform::fileKey(folder);
        if (std::none_of(paths.begin(), paths.end(), [&](const std::string& p) { return platform::fileKey(p) == key; })) {
            paths.push_back(platform::normalPath(folder));
        }
    };
    add(registryFolder());
    const std::string programFiles = knownFolder(FOLDERID_ProgramFiles);
    const std::string common = knownFolder(FOLDERID_ProgramFilesCommon);
    if (!programFiles.empty()) {
        add(platform::fromPath(platform::toPath(programFiles) / "VSTPlugins"));
        add(platform::fromPath(platform::toPath(programFiles) / "Steinberg" / "VSTPlugins"));
    }
    if (!common.empty()) {
        add(platform::fromPath(platform::toPath(common) / "VST2"));
        add(platform::fromPath(platform::toPath(common) / "Steinberg" / "VST2"));
    }
    return paths;
}

bool isPluginFileName(const std::string& name) {
    if (name.size() <= 4) return false;
    std::string extension = name.substr(name.size() - 4);
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
    return extension == ".dll";
}

void* loadLibrary(const std::string& path, std::string& error) {
    // Its own folder first for the libraries it needs, as for an application.
    HMODULE library = LoadLibraryExW(platform::toNative(path).c_str(), nullptr,
                                     LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!library) library = LoadLibraryW(platform::toNative(path).c_str());
    if (!library) {
        // As the VST 3 SDK's loader words it, so the scanner's friendly reasons read both.
        error = "LoadLibraryW failed with error number: " + std::to_string(GetLastError());
    }
    return library;
}

void* librarySymbol(void* library, const char* name) {
    return library ? reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(library), name)) : nullptr;
}

void unloadLibrary(void* library) {
    if (library) FreeLibrary(static_cast<HMODULE>(library));
}

}  // namespace sub::vst2
