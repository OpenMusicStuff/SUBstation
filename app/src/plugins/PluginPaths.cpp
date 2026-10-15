#include "plugins/PluginPaths.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "Text.h"
#include "browser/PathKeys.h"
#include "platform/Paths.h"
#include "plugins/vst2/Vst2Format.h"
#include "plugins/vst3/Vst3Format.h"

namespace sub::app {

namespace {

std::filesystem::path fsPath(const QString& path) { return platform::toPath(toBackendPath(path)); }

// Whether a file or folder of this name is a VST3 plug-in ("Name.vst3"): not Linux's own folder for them,
// "~/.vst3", which is named like one.
bool isPluginName(const QString& name) {
    return name.size() > 5 && name.endsWith(QStringLiteral(".vst3"), Qt::CaseInsensitive);
}

// Whether a file of this name may be a VST2 plug-in: a library (".dll" on Windows, ".so" elsewhere).
bool isVst2Name(const QString& name) { return vst2::Vst2Format::isPluginFile(name.toStdString()); }

QStringList foldersIn(const char* variable) {
    QStringList folders;
    for (const QString& folder : qEnvironmentVariable(variable).split(QDir::listSeparator()))
        if (!folder.isEmpty()) folders << normalPath(folder);
    return folders;
}

// Python's str.lower(), for sorting as it sorted.
std::string lowerForSort(const QString& s) { return browser::pyLower(s.toStdString()); }

}  // namespace

QStringList standardPluginFolders() {
    const bool vst3Set = qEnvironmentVariableIsSet("SUBSTATION_VST3_PATH");
    QStringList folders;
    if (vst3Set) {
        folders = foldersIn("SUBSTATION_VST3_PATH");
    } else {
        for (const std::string& folder : vst3::Vst3Format::instance().defaultSearchPaths())
            folders << normalPath(fromBackendPath(folder));
    }
    if (qEnvironmentVariableIsSet("SUBSTATION_VST2_PATH")) {
        folders << foldersIn("SUBSTATION_VST2_PATH");
    } else if (!vst3Set) {  // (SUBSTATION_VST3_PATH alone stands for every standard folder: the tests')
        for (const std::string& folder : vst2::Vst2Format::instance().defaultSearchPaths())
            folders << normalPath(fromBackendPath(folder));
    }
    QStringList unique;
    QSet<QString> seen;
    for (const QString& folder : folders) {
        if (seen.contains(pathKey(folder))) continue;
        seen.insert(pathKey(folder));
        unique << folder;
    }
    return unique;
}

QStringList pluginSearchFolders(const QStringList& custom) {
    // In order (Python kept them in a dict, which keeps insertion order).
    QStringList folders;
    QSet<QString> seen;
    QStringList all = standardPluginFolders();
    for (const QString& folder : custom)
        if (!folder.isEmpty()) all << folder;
    for (const QString& folder : all) {
        const QString key = pathKey(folder);
        if (seen.contains(key)) continue;
        seen.insert(key);
        folders << normalPath(folder);
    }
    return folders;
}

QStringList findPluginFiles(const QStringList& roots) {
    std::vector<QString> found;  // in the order found, one per key (the first)
    QSet<QString> foundKeys;
    QSet<QString> seen;  // real paths of the folders already listed
    auto add = [&](const QString& path) {
        if (!foundKeys.contains(caseKey(path))) {
            foundKeys.insert(caseKey(path));
            found.push_back(path);
        }
    };
    for (const QString& root : roots) {
        const QFileInfo rootInfo(root);
        if ((isPluginName(rootInfo.fileName()) && rootInfo.exists()) ||
            (isVst2Name(rootInfo.fileName()) && rootInfo.isFile())) {
            add(root);
            continue;
        }
        if (!rootInfo.isDir()) continue;
        std::vector<QString> stack{root};
        while (!stack.empty()) {
            const QString folder = stack.back();
            stack.pop_back();
            const QFileInfo info(folder);
            const QString canonical = info.canonicalFilePath();
            const QString real = caseKey(canonical.isEmpty() ? info.absoluteFilePath() : canonical);
            if (seen.contains(real)) continue;
            seen.insert(real);
            const QStringList names = QDir(folder).entryList(
                QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDir::NoSort);
            for (const QString& name : names) {
                const QString path = folder.endsWith(QLatin1Char('/')) ? folder + name : folder + QLatin1Char('/') + name;
                if (isPluginName(name))
                    add(path);
                else if (const QFileInfo file(path); file.isDir())  // following links
                    stack.push_back(path);
                else if (isVst2Name(name) && file.isFile())
                    add(path);
            }
        }
    }
    std::vector<std::pair<std::string, QString>> sorted;
    sorted.reserve(found.size());
    for (const QString& path : found) sorted.emplace_back(lowerForSort(path), path);
    std::stable_sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    QStringList files;
    for (auto& [_, path] : sorted) files << path;
    return files;
}

QStringList findPluginFiles() { return findPluginFiles(pluginSearchFolders()); }

QString pluginCachePath() { return localDataFile("SUBSTATION_PLUGIN_CACHE", QStringLiteral("vst3-cache.json")); }

QString pluginBinary(const QString& path) {
    const QFileInfo info(path);
    const QString inner =
        path + QLatin1Char('/') + QString::fromStdString(vst3::Vst3Format::binaryInBundle(info.fileName().toStdString()));
    return info.isDir() && QFileInfo::exists(inner) ? inner : path;
}

std::optional<std::array<int64_t, 2>> pluginSignature(const QString& path) {
    std::error_code error;
    const std::filesystem::path file = fsPath(pluginBinary(path));
    const auto status = std::filesystem::status(file, error);
    if (error || !std::filesystem::exists(status)) return std::nullopt;
    const auto written = std::filesystem::last_write_time(file, error);
    if (error) return std::nullopt;
    // A folder has no size of its own (as Windows reports it).
    const uintmax_t size = std::filesystem::is_directory(status) ? 0 : std::filesystem::file_size(file, error);
    if (error) return std::nullopt;
    // Nanoseconds since 1970, as Python's st_mtime_ns (caches it wrote stay valid).
#ifdef _MSC_VER
    const auto sinceEpoch = std::chrono::clock_cast<std::chrono::system_clock>(written).time_since_epoch();
#else
    const auto sinceEpoch = std::chrono::file_clock::to_sys(written).time_since_epoch();
#endif
    const int64_t ns = std::chrono::duration_cast<std::chrono::nanoseconds>(sinceEpoch).count();
    return std::array<int64_t, 2>{ns, static_cast<int64_t>(size)};
}

QString friendlyScanReason(const QString& input) {
    QStringList words;
    for (const std::string& word : browser::pySplit(input.toStdString())) words << QString::fromStdString(word);
    const QString reason = words.join(QLatin1Char(' '));
    static const QRegularExpression number(QStringLiteral("LoadLibraryW failed with error number: (\\d+)"));
    const QRegularExpressionMatch match = number.match(reason);
    if (match.hasMatch()) {
        const int code = match.captured(1).toInt();
        QString what;
        switch (code) {
            case 126: what = QStringLiteral("a file it needs is missing"); break;
            case 193: what = QStringLiteral("it is not a 64-bit Windows plug-in"); break;
            case 1114: what = QStringLiteral("it failed to start"); break;
            default: what = QStringLiteral("error %1").arg(code);
        }
        return QStringLiteral("Windows could not load it: %1.").arg(what);
    }
    const qsizetype cut = reason.lastIndexOf(QStringLiteral(": "));
    if (reason.startsWith(QStringLiteral("LoadLibraryW failed for path")) && cut >= 0)
        return QStringLiteral("Windows could not load it: ") + reason.mid(cut + 2);
    // Linux (the VST 3 SDK's module_linux.cpp): no code for Linux in it (a Windows plug-in, or a bundle
    // without its <arch>-linux folder), or dlopen's error.
    static const QRegularExpression noModule(QStringLiteral("^(.*) is not a module directory\\.$"));
    if (const QRegularExpressionMatch match = noModule.match(reason); match.hasMatch()) {
        const QString name = QFileInfo(match.captured(1)).fileName();
        return QStringLiteral("It is not a Linux plug-in: it has no %1.")
            .arg(QString::fromStdString(vst3::Vst3Format::binaryInBundle(name.toStdString())));
    }
    static const QRegularExpression dlopen(QStringLiteral("^dlopen failed\\. (.*)$"));
    if (const QRegularExpressionMatch match = dlopen.match(reason); match.hasMatch()) {
        const QString error = match.captured(1);
        static const QRegularExpression missing(QStringLiteral("^(\\S+): cannot open shared object file"));
        if (const QRegularExpressionMatch lib = missing.match(error); lib.hasMatch())
            return QStringLiteral("Linux could not load it: a library it needs is missing (%1).").arg(lib.captured(1));
        if (error.contains(QStringLiteral("wrong ELF class")))
            return QStringLiteral("Linux could not load it: it is not a 64-bit plug-in.");
        const qsizetype colon = error.indexOf(QStringLiteral(": "));  // (dlerror(): "<file>: <why>")
        return QStringLiteral("Linux could not load it: ") + (colon >= 0 ? error.mid(colon + 2) : error) +
               (error.endsWith(QLatin1Char('.')) ? QString() : QStringLiteral("."));
    }
    return reason;
}

}  // namespace sub::app
