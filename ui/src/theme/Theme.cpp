#include "theme/Theme.h"

#include "input/Shortcuts.h"
#include "model/Track.h"

#include <QDir>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QHash>
#include <QJSEngine>
#include <QKeySequence>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSettings>
#include <QVariant>
#include <QWindow>

#include <algorithm>
#include <cmath>
#include <optional>

namespace sub::ui {

namespace {

// The bundled UI font's files (ui/fonts), added to the application's fonts once.
void loadUiFont() {
    static const bool loaded = [] {
        const QDir fonts(QStringLiteral(":/qt/qml/SUBstation/fonts"));
        for (const QString& file : fonts.entryList({QStringLiteral("*.ttf")}, QDir::Files))
            QFontDatabase::addApplicationFont(fonts.filePath(file));
        return true;
    }();
    Q_UNUSED(loaded);
}

}  // namespace

QFont uiFont(qreal pointSize, bool bold) { return uiFont(pointSize, bold ? QFont::Bold : QFont::Normal); }

QFont uiFont(qreal pointSize, QFont::Weight weight) {
    loadUiFont();
    QFont font(QString::fromLatin1(Theme::kUiFontFamily));
    font.setPointSizeF(pointSize);
    font.setWeight(weight);
    return font;
}

QFont monoFont(qreal pointSize) {
    QFont font(QString::fromLatin1(Theme::kMonoFontFamily));
    font.setStyleHint(QFont::Monospace);
    font.setPointSizeF(pointSize);
    return font;
}

namespace {

const NamedPalette* current = nullptr;  // the theme (null: Default)

// OKLab (Bjorn Ottosson's), from and to sRGB.
struct Lab {
    double l, a, b;
};

double linear(double c) { return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); }
double gamma(double c) {
    c = std::clamp(c, 0.0, 1.0);
    return c <= 0.0031308 ? 12.92 * c : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055;
}

Lab toLab(const QColor& color) {
    const double r = linear(color.redF()), g = linear(color.greenF()), b = linear(color.blueF());
    const double l = std::cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
    const double m = std::cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
    const double s = std::cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
    return {0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
            1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
            0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s};
}

QColor fromLab(const Lab& lab) {
    const double l = std::pow(lab.l + 0.3963377774 * lab.a + 0.2158037573 * lab.b, 3);
    const double m = std::pow(lab.l - 0.1055613458 * lab.a - 0.0638541728 * lab.b, 3);
    const double s = std::pow(lab.l - 0.0894841775 * lab.a - 1.2914855480 * lab.b, 3);
    return QColor::fromRgbF(static_cast<float>(gamma(4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s)),
                            static_cast<float>(gamma(-1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s)),
                            static_cast<float>(gamma(-0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s)));
}

// A colour's place in the track palette: app::kTrackColors' or any theme's.
std::optional<int> trackPlace(QRgb rgb) {
    rgb &= RGB_MASK;
    for (int i = 0; i < sub::app::kTrackColors.size(); ++i)
        if ((QColor::fromString(sub::app::kTrackColors[i]).rgb() & RGB_MASK) == rgb)
            return i;
    for (const NamedPalette& palette : palettes())
        for (size_t i = 0; i < palette.colors.trackColors.size(); ++i)
            if ((palette.colors.trackColors[i].rgb() & RGB_MASK) == rgb)
                return static_cast<int>(i);
    return std::nullopt;
}

const NamedPalette* find(const QString& name) {
    for (const NamedPalette& palette : palettes())
        if (palette.name.compare(name, Qt::CaseInsensitive) == 0)
            return &palette;
    return nullptr;
}

// An item and everything in it drawn again, what they work out in
// updatePolish() worked out again first (the arrangement's envelopes' looks).
void repaint(QQuickItem* item) {
    item->polish();
    if (item->flags() & QQuickItem::ItemHasContents)
        item->update();
    for (QQuickItem* child : item->childItems())
        repaint(child);
}

}  // namespace

Theme* Theme::instance() {
    static Theme* theme = new Theme;  // for the process's life, as QML's singletons are
    return theme;
}

Theme* Theme::create(QQmlEngine*, QJSEngine*) {
    Theme* theme = instance();
    QJSEngine::setObjectOwnership(theme, QJSEngine::CppOwnership);  // shared by every engine
    return theme;
}

QString Theme::name() { return current ? current->name : palettes().front().name; }

const Palette& Theme::colors() { return current ? current->colors : palettes().front().colors; }

QStringList Theme::names() {
    QStringList result;
    for (const NamedPalette& palette : palettes())
        result << palette.name;
    return result;
}

void Theme::setName(const QString& name) {
    if (!apply(name))
        apply(palettes().front().name);
    QSettings().setValue(QLatin1String(kSettingsKey), Theme::name());
}

bool Theme::apply(const QString& name) {
    const NamedPalette* palette = find(name);
    if (!palette)
        return false;
    if (palette == &palettes().front())
        palette = nullptr;
    if (palette == current)
        return true;
    current = palette;
    if (qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
        QGuiApplication::setPalette(qtPalette());
        for (QWindow* window : QGuiApplication::allWindows())
            if (auto* quick = qobject_cast<QQuickWindow*>(window))
                repaint(quick->contentItem());
    }
    Q_EMIT instance()->changed();
    return true;
}

QString Theme::savedName() {
    const NamedPalette* palette = find(QSettings().value(QLatin1String(kSettingsKey)).toString());
    return palette ? palette->name : palettes().front().name;
}

QColor Theme::trackColor(const QString& stored) { return trackColor(QColor::fromString(stored)); }

QColor Theme::trackColor(const QColor& stored) {
    if (!stored.isValid())
        return stored;
    const Palette& theme = colors();
    // (Looked up at every paint: the places found are kept, whatever the theme.)
    static QHash<QRgb, int> places;
    const QRgb rgb = stored.rgb() & RGB_MASK;
    auto place = places.constFind(rgb);
    if (place == places.constEnd())
        place = places.insert(rgb, trackPlace(rgb).value_or(-1));
    if (*place >= 0 && *place < static_cast<int>(theme.trackColors.size()))
        return theme.trackColors[static_cast<size_t>(*place)];
    if (theme.trackChroma <= 0.0)
        return stored;
    // Another colour: its lightness into the theme's range, its chroma no more than the theme's.
    Lab lab = toLab(stored);
    const double chroma = std::hypot(lab.a, lab.b);
    const double scale = chroma > theme.trackChroma ? theme.trackChroma / chroma : 1.0;
    lab.a *= scale;
    lab.b *= scale;
    lab.l = theme.trackLightMin + std::clamp(lab.l, 0.0, 1.0) * (theme.trackLightMax - theme.trackLightMin);
    return fromLab(lab);
}

QColor Theme::trackSwatch(int index) {
    return index >= 0 && index < sub::app::kTrackColors.size() ? trackColor(sub::app::kTrackColors[index]) : QColor();
}

QPalette Theme::qtPalette() {
    QPalette palette;
    const std::pair<QPalette::ColorRole, QColor> roles[] = {
        {QPalette::Window, window()},          {QPalette::WindowText, text()},
        {QPalette::Base, panel()},             {QPalette::AlternateBase, panelAlt()},
        {QPalette::Text, text()},              {QPalette::Button, surface()},
        {QPalette::ButtonText, text()},        {QPalette::Highlight, accent()},
        {QPalette::HighlightedText, accentText()}, {QPalette::ToolTipBase, panelAlt()},
        {QPalette::ToolTipText, text()},       {QPalette::PlaceholderText, textDim()},
        {QPalette::Link, accent()},
    };
    for (const auto& [role, color] : roles)
        palette.setColor(role, color);
    for (QPalette::ColorRole role : {QPalette::Text, QPalette::ButtonText, QPalette::WindowText})
        palette.setColor(QPalette::Disabled, role, textDisabled());
    return palette;
}

QFont Theme::font() const { return sub::ui::uiFont(); }

QFont Theme::smallFont() const { return sub::ui::uiFont(8.0); }
QFont Theme::listFont() const { return sub::ui::uiFont(10.0); }
QFont Theme::listHeadingFont() const {
    QFont font = sub::ui::uiFont(8.0, QFont::DemiBold);
    font.setLetterSpacing(QFont::AbsoluteSpacing, 0.6);
    return font;
}
QFont Theme::titleFont() const { return sub::ui::uiFont(9.0, QFont::DemiBold); }

QString Theme::monoFontFamily() const { return QString::fromLatin1(kMonoFontFamily); }

QFont Theme::uiFont(qreal pointSize, bool bold) const { return sub::ui::uiFont(pointSize, bold); }

QFont Theme::monoFont(qreal pointSize) const { return sub::ui::monoFont(pointSize); }

QColor Theme::automationColor(const QString& state) const {
    const QColor color = automationDotColor(state);
    return color.isValid() ? color : QColor(Qt::transparent);
}

QString Theme::shortcutText(const QVariant& shortcut) const {
    return keySequences(shortcut).value(0).toString(QKeySequence::NativeText);
}

QString Theme::withoutMnemonics(const QString& text) {
    QString result;
    result.reserve(text.size());
    for (qsizetype i = 0; i < text.size(); ++i) {
        if (text[i] == QLatin1Char('&') && i + 1 < text.size())
            ++i;  // "&x" shows x; "&&" shows one "&"
        result += text[i];
    }
    return result;
}

QStringList Theme::buttonRoles() {
    return {QString(),
            QStringLiteral("activator"),
            QStringLiteral("solo"),
            QStringLiteral("play"),
            QStringLiteral("record"),
            QStringLiteral("arm"),
            QStringLiteral("re-enable"),
            QStringLiteral("tool"),
            QStringLiteral("flat"),
            QStringLiteral("small"),
            QStringLiteral("device-header")};
}

Theme::ButtonLook Theme::buttonLook(const QString& role, bool hovered, bool pressed, bool checked, bool enabled) {
    // The stylesheet's rules in its order; pseudo-states only apply where Qt
    // would (a disabled button isn't hovered or pressed).
    hovered = hovered && enabled;
    pressed = pressed && enabled;
    ButtonLook look;
    // QPushButton { background: SURFACE; border: 1px solid BORDER; border-radius: 3px; padding: 4px 12px; }
    look.background = surface();
    look.text = text();
    if (hovered)
        look.background = surfaceHover();  // :hover
    if (pressed)
        look.background = panel();  // :pressed
    if (!enabled)
        look.text = textDisabled();  // :disabled
    if (checked) {                  // :checked
        look.background = accent();
        look.text = accentText();
    }
    if (role == QLatin1String("activator") && checked) {
        look.background = activatorOn();
        look.text = accentText();
    } else if (role == QLatin1String("solo") && checked) {
        look.background = soloOn();
        look.text = accentText();
    } else if (role == QLatin1String("play") && checked) {
        look.background = playOn();
    } else if ((role == QLatin1String("record") || role == QLatin1String("arm")) && checked) {
        look.background = recordOn();
        look.text = accentText();
    } else if (role == QLatin1String("re-enable") && checked) {
        look.background = accent();
    }
    if (role == QLatin1String("record") || role == QLatin1String("re-enable") || role == QLatin1String("tool")) {
        look.paddingH = look.paddingV = 2;
        look.minWidth = 26;
        look.minHeight = 22;
    } else if (role == QLatin1String("arm")) {  // (a box as solo's, as Ableton's, its dot the text)
        look.paddingH = look.paddingV = 0;
        look.pointSize = 8.0;
    } else if (role == QLatin1String("activator") || role == QLatin1String("solo")) {
        look.paddingH = look.paddingV = 0;
        look.pointSize = 8.0;
        look.weight = QFont::DemiBold;
    } else if (role == QLatin1String("monitor")) {
        // A track header's In, Auto and Off: small, side by side (the one chosen in the accent).
        look.paddingH = look.paddingV = 0;
        look.pointSize = 8.0;
    } else if (role == QLatin1String("flat")) {
        // Later than every state rule: no background in any state, dim text but under the mouse.
        look.paddingH = look.paddingV = 0;
        look.border = 0;
        look.background = Qt::transparent;
        look.text = hovered ? text() : textDim();
    } else if (role == QLatin1String("small")) {
        look.paddingH = 6;
        look.paddingV = 0;
        look.pointSize = 8.0;
    } else if (role == QLatin1String("device-header")) {
        look.paddingH = look.paddingV = 0;
        look.border = 0;
        look.background = Qt::transparent;
        look.text = textDim();
        if (hovered) {
            look.background = deviceHeaderHover();
            look.text = text();
        }
        if (checked) {
            look.background = accent();
            look.text = accentText();
        }
        if (!enabled) {
            look.background = Qt::transparent;
            look.text = textDisabled();
        }
    }
    return look;
}

QVariantMap Theme::buttonStyle(const QString& role, bool hovered, bool pressed, bool checked, bool enabled) const {
    const ButtonLook look = buttonLook(role, hovered, pressed, checked, enabled);
    return {{QStringLiteral("background"), look.background}, {QStringLiteral("text"), look.text},
            {QStringLiteral("border"), look.border},         {QStringLiteral("radius"), look.radius},
            {QStringLiteral("paddingH"), look.paddingH},     {QStringLiteral("paddingV"), look.paddingV},
            {QStringLiteral("minWidth"), look.minWidth},     {QStringLiteral("minHeight"), look.minHeight},
            {QStringLiteral("pointSize"), look.pointSize},   {QStringLiteral("weight"), look.weight}};
}

QColor Theme::automationDotColor(const QString& state) {
    if (state == QLatin1String("on"))
        return automationOn();
    if (state == QLatin1String("off"))
        return automationOff();
    return {};
}

}  // namespace sub::ui
