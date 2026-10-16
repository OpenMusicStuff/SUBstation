#include "browser/BrowserSearch.h"

#include <QVariantMap>

#include "Model.h"
#include "browser/PathKeys.h"

namespace sub::app {

QVariantList sortOrders() {
    return {QVariantMap{{QStringLiteral("value"), kRankSort}, {QStringLiteral("label"), QStringLiteral("Rank")}},
            QVariantMap{{QStringLiteral("value"), QStringLiteral("name")}, {QStringLiteral("label"), QStringLiteral("Name")}}};
}

bool isSortOrder(const QString& sort) {
    return sort == kRankSort || sort == QStringLiteral("name");
}

QStringList Scope::toList() const { return sub.isEmpty() && kind != QStringLiteral("place") ? QStringList{kind} : QStringList{kind, sub}; }

Scope Scope::fromList(const QStringList& list) {
    if (list.isEmpty()) return {};
    return {list[0], list.size() > 1 ? list[1] : QString()};
}

ScopeQuery scopeQuery(const Scope& scope) {
    if (scope.kind == QStringLiteral("all")) return {{kBuiltinGroup, kPluginsGroup, kPresetsGroup, kAudioGroup}, {}, {}};
    if (scope.kind == QStringLiteral("builtin")) return {{kBuiltinGroup}, scope.sub, {}};
    if (scope.kind == QStringLiteral("plugins")) return {{kPluginsGroup}, scope.sub, {}};
    if (scope.kind == QStringLiteral("presets")) return {{kPresetsGroup}, scope.sub, {}};
    if (scope.kind == QStringLiteral("place")) return {{kAudioGroup}, {}, placePrefix(scope.sub)};
    if (scope.kind == QStringLiteral("sounds")) return {{kAudioGroup}, {}, {}, scope.sub};
    if (scope.kind == QStringLiteral("hidden"))
        return {{kBuiltinGroup, kPluginsGroup, kPresetsGroup, kAudioGroup}, {}, {}, {}, true};
    return {{kAudioGroup}, {}, {}};
}

std::string placePrefix(const QString& place) { return browser::placePrefix(toBackendPath(normalPath(place))); }

QString pluginTag(const BrowserItem& item) {
    return item.plugin && item.plugin->instrument ? QStringLiteral("Instruments") : QStringLiteral("Audio Effects");
}

}  // namespace sub::app
