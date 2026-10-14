#pragma once
// Preferences › Plug-ins › Plug-in Hosting, without its widgets: where plug-ins
// run (docs/engine/sandboxes.md, docs/guide/plugins.md). As Bitwig offers it:
//
// - within SUBstation: in the application's own process (the least memory and
//   CPU; a plug-in that crashes takes SUBstation down, and one that takes its
//   time to load holds up the window meanwhile);
// - together (the default): every plug-in in one sandbox, a process of its own;
// - by manufacturer: a sandbox for each plug-in maker;
// - by plug-in: a sandbox for each plug-in (its instances share it);
// - individually: a sandbox for each instance (the safest, the most memory and CPU).
//
// In a sandbox a plug-in loads while the window goes on, and one that crashes
// takes down only its sandbox: the plug-ins there say so in the device view, and
// load again with Reload. Kept in QSettings (plugins/hosting), and applied to the
// engine at once: to plug-ins as they load (a project opened, a device added),
// and to those loaded already when they are reloaded (reloadPlugins).

#include <QObject>
#include <QString>
#include <QVariantList>

#include <optional>

#include "audio/BridgeTypes.h"

namespace sub::app {

class EngineBridge;

class PluginHostingPreferences : public QObject {
    Q_OBJECT
    // "within", "together", "manufacturer", "plugin" or "instance".
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY modeChanged)
    // [{value (the mode), label, toolTip (what it means)}], in the order the preferences list them.
    Q_PROPERTY(QVariantList modes READ modes CONSTANT)
    // What runs where now: "3 plug-ins in 2 sandboxes", "Plug-ins run within SUBstation."
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)

public:
    inline static const QString kKey = QStringLiteral("plugins/hosting");
    inline static const QString kDefault = QStringLiteral("together");

    // Applies the preference (and the host program: `hostProgram`, else
    // substation-plugin-host next to the application, or SUBSTATION_PLUGIN_HOST) to the engine.
    PluginHostingPreferences(EngineBridge* bridge, const QString& hostProgram, QObject* parent = nullptr);

    QString mode() const { return mode_; }
    void setMode(const QString& mode);
    QVariantList modes() const;
    QString status() const { return status_; }

    // Every plug-in loaded again, where the mode says now.
    Q_INVOKABLE void reloadPlugins();
    // The status again (the page shows).
    Q_INVOKABLE void refresh();

    static std::optional<PluginHostingMode> modeNamed(const QString& name);
    static QString nameOf(PluginHostingMode mode);
    // Without a preference: kDefault, or SUBSTATION_PLUGIN_HOSTING's (the tests' "within").
    static QString defaultMode();
    static QString defaultHostProgram();

Q_SIGNALS:
    void modeChanged();
    void statusChanged();

private:
    EngineBridge* bridge_;
    QString mode_;
    QString status_;
};

}  // namespace sub::app
