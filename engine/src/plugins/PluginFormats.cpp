// The plug-in formats there are (PluginFormat.h).

#include <algorithm>
#include <cctype>

#include "plugins/PluginFormat.h"
#include "plugins/vst2/Vst2Format.h"
#include "plugins/vst3/Vst3Format.h"

namespace sub {

PluginFormat* pluginFormat(const std::string& name) {
    if (name == "VST3") return &vst3::Vst3Format::instance();
    if (name == "VST2") return &vst2::Vst2Format::instance();
    return nullptr;
}

PluginFormat* pluginFormatOfFile(const std::string& path) {
    std::string tail = path;
    while (!tail.empty() && (tail.back() == '/' || tail.back() == '\\')) tail.pop_back();  // (a bundle's folder)
    std::transform(tail.begin(), tail.end(), tail.begin(),
                   [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
    if (tail.size() > 5 && tail.ends_with(".vst3")) return &vst3::Vst3Format::instance();
    if (vst2::Vst2Format::isPluginFile(path)) return &vst2::Vst2Format::instance();
    return nullptr;
}

}  // namespace sub
