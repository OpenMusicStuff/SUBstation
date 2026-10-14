#include "session/PluginHostingPreferences.h"

#include <QCoreApplication>
#include <QSettings>
#include <QVariantMap>

#include "audio/EngineBridge.h"

namespace sub::app {

namespace {

struct Mode {
    PluginHostingMode mode;
    const char* key;
    const char* name;
    const char* description;
};

constexpr Mode kModes[] = {
    {PluginHostingMode::Within, "within", "Within SUBstation",
     "Plug-ins run in SUBstation itself: the least memory and CPU, but a plug-in that crashes takes SUBstation "
     "down, and one that takes its time to load holds up the window meanwhile."},
    {PluginHostingMode::Together, "together", "Together",
     "Every plug-in runs in one sandbox, a process of its own: if a plug-in crashes, the plug-ins stop and "
     "SUBstation goes on."},
    {PluginHostingMode::ByManufacturer, "manufacturer", "By Manufacturer",
     "A sandbox for each plug-in maker: if a plug-in crashes, the other makers' plug-ins go on."},
    {PluginHostingMode::ByPlugin, "plugin", "By Plug-in",
     "A sandbox for each plug-in: its instances share it, and if it crashes, every other plug-in goes on."},
    {PluginHostingMode::Individually, "instance", "Individually",
     "A sandbox for each instance of each plug-in: the safest, and the most memory and CPU."},
};

}  // namespace

std::optional<PluginHostingMode> PluginHostingPreferences::modeNamed(const QString& name) {
    for (const Mode& mode : kModes) {
        if (name == QLatin1String(mode.key)) return mode.mode;
    }
    return std::nullopt;
}

QString PluginHostingPreferences::nameOf(PluginHostingMode mode) {
    for (const Mode& m : kModes) {
        if (m.mode == mode) return QString::fromLatin1(m.key);
    }
    return defaultMode();
}

QString PluginHostingPreferences::defaultMode() {
    const QString overridden = qEnvironmentVariable("SUBSTATION_PLUGIN_HOSTING");
    return modeNamed(overridden) ? overridden : kDefault;
}

QString PluginHostingPreferences::defaultHostProgram() {
    const QString overridden = qEnvironmentVariable("SUBSTATION_PLUGIN_HOST");
    if (!overridden.isEmpty()) return overridden;
#ifdef _WIN32
    return QCoreApplication::applicationDirPath() + QStringLiteral("/substation-plugin-host.exe");
#else
    return QCoreApplication::applicationDirPath() + QStringLiteral("/substation-plugin-host");
#endif
}

PluginHostingPreferences::PluginHostingPreferences(EngineBridge* bridge, const QString& hostProgram, QObject* parent)
    : QObject(parent), bridge_(bridge) {
    bridge_->setPluginHostProgram(hostProgram.isEmpty() ? defaultHostProgram() : hostProgram);
    const QString saved = QSettings().value(kKey, defaultMode()).toString();
    mode_ = modeNamed(saved) ? saved : defaultMode();
    bridge_->setPluginHosting(*modeNamed(mode_));
    // (Plug-ins load, crash or go: what runs where shows anew.)
    connect(bridge_, &EngineBridge::pluginsLoading, this, [this] { refresh(); });
    connect(bridge_, &EngineBridge::devicesLoaded, this, [this] { refresh(); });
    refresh();
}

void PluginHostingPreferences::setMode(const QString& mode) {
    const auto hosting = modeNamed(mode);
    if (!hosting || mode == mode_) return;
    mode_ = mode;
    QSettings().setValue(kKey, mode_);
    bridge_->setPluginHosting(*hosting);
    Q_EMIT modeChanged();
    refresh();
}

QVariantList PluginHostingPreferences::modes() const {
    QVariantList list;
    for (const Mode& mode : kModes) {
        list.append(QVariantMap{{QStringLiteral("value"), QString::fromLatin1(mode.key)},
                                {QStringLiteral("label"), QString::fromLatin1(mode.name)},
                                {QStringLiteral("toolTip"), QString::fromLatin1(mode.description)}});
    }
    return list;
}

void PluginHostingPreferences::reloadPlugins() {
    bridge_->reloadPlugins(false);
    refresh();
}

void PluginHostingPreferences::refresh() {
    int sandboxes = 0;
    int plugins = 0;
    for (const PluginSandbox& sandbox : bridge_->pluginSandboxes()) {
        if (!sandbox.running || sandbox.plugins == 0) continue;
        ++sandboxes;
        plugins += sandbox.plugins;
    }
    QString status;
    if (sandboxes == 0) {
        status = mode_ == QLatin1String("within") ? QStringLiteral("Plug-ins run within SUBstation.")
                                                   : QStringLiteral("No plug-in runs in a sandbox now.");
    } else {
        status = (plugins == 1 ? QStringLiteral("1 plug-in") : QStringLiteral("%1 plug-ins").arg(plugins)) +
                 (sandboxes == 1 ? QStringLiteral(" in 1 sandbox.") : QStringLiteral(" in %1 sandboxes.").arg(sandboxes));
    }
    if (status == status_) return;
    status_ = status;
    Q_EMIT statusChanged();
}

}  // namespace sub::app
