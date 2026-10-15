// VST2 hosting elsewhere than Windows (see Vst2Platform.h): Linux.

#include "plugins/Vst2Platform.h"

#include <dlfcn.h>

#include <cstdlib>
#include <string_view>

#include "platform/Paths.h"

namespace sub::vst2 {

std::vector<std::string> systemPluginFolders() {
    std::vector<std::string> paths;
    if (const char* list = std::getenv("VST_PATH"); list && *list) {  // (as other Linux hosts read it)
        std::string_view rest(list);
        while (!rest.empty()) {
            const size_t colon = rest.find(':');
            const std::string_view folder = rest.substr(0, colon);
            if (!folder.empty()) paths.emplace_back(folder);
            if (colon == std::string_view::npos) break;
            rest.remove_prefix(colon + 1);
        }
        return paths;
    }
    if (const char* home = std::getenv("HOME"); home && *home) {
        paths.push_back(platform::fromPath(platform::toPath(home) / ".vst"));
    }
    paths.push_back("/usr/lib/vst");
    paths.push_back("/usr/local/lib/vst");
    return paths;
}

bool isPluginFileName(const std::string& name) {
    return name.size() > 3 && name.compare(name.size() - 3, 3, ".so") == 0;
}

void* loadLibrary(const std::string& path, std::string& error) {
    void* library = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!library) {
        const char* reason = dlerror();
        error = "dlopen failed. " + std::string(reason ? reason : "");
    }
    return library;
}

void* librarySymbol(void* library, const char* name) { return library ? dlsym(library, name) : nullptr; }

void unloadLibrary(void* library) {
    if (library) dlclose(library);
}

}  // namespace sub::vst2
