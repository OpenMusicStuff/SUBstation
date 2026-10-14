#include "devices/DeviceParam.h"

#include "audio/EngineBridge.h"
#include "controls/Automation.h"
#include "controls/ValueBoxItem.h"
#include "devices/ModulatorValues.h"
#include "model/Devices.h"
#include "session/ModulationLearn.h"
#include "editor/ProjectEditor.h"
#include "model/Automation.h"
#include "model/Device.h"
#include "model/Project.h"

#include <QHash>
#include <QLocale>

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

namespace sub::ui {

using sub::app::EngineBridge;
using sub::app::Project;

DeviceParam::DeviceParam(QObject* parent) : QObject(parent) {
    connect(ModulatorValues::instance(), &ModulatorValues::ticked, this, &DeviceParam::refreshModulationValue);
}

DeviceParam::~DeviceParam() {
    if (!modulator_.isEmpty())
        ModulatorValues::release(modulator_);
}

void DeviceParam::setSession(sub::app::Session* session) {
    if (session == session_)
        return;
    session_ = session;
    connectSession();
    Q_EMIT targetChanged();
    refresh();
}

void DeviceParam::setTrackId(const QString& trackId) {
    if (trackId == trackId_)
        return;
    trackId_ = trackId;
    Q_EMIT targetChanged();
    refresh();
}

void DeviceParam::setDeviceId(const QString& deviceId) {
    if (deviceId == deviceId_)
        return;
    deviceId_ = deviceId;
    Q_EMIT targetChanged();
    refresh();
}

void DeviceParam::setParamId(const QString& paramId) {
    if (paramId == paramId_)
        return;
    paramId_ = paramId;
    Q_EMIT targetChanged();
    refresh();
}

QString DeviceParam::key() const { return sub::app::automation::deviceKey(deviceId_, paramId_); }

void DeviceParam::connectSession() {
    for (const QMetaObject::Connection& connection : std::as_const(connections_))
        disconnect(connection);
    connections_.clear();
    if (!session_)
        return;
    Project* project = session_->project();
    EngineBridge* bridge = session_->bridge();
    connections_ << connect(project, &Project::deviceParamChanged, this,
                            [this](const QString& trackId, const QString& deviceId, const QString& paramId) {
                                if (trackId == trackId_ && deviceId == deviceId_ && paramId == paramId_)
                                    refreshValue();
                            });
    // The device (or its processor) came or went, was replaced (a preset): read it all again.
    connections_ << connect(project, &Project::devicesChanged, this, [this](const QString& trackId) {
        if (trackId == trackId_)
            refresh();
    });
    connections_ << connect(project, &Project::reset, this, &DeviceParam::refresh);
    connections_ << connect(bridge, &EngineBridge::devicesLoaded, this, [this](const QString& trackId) {
        if (trackId == trackId_)
            refresh();
    });
    connections_ << connect(project, &Project::automationChanged, this, [this](const QString& owner, const QString&) {
        if (owner == trackId_) {
            refreshAutomation();
            refreshValue();
        }
    });
    connections_ << connect(bridge, &EngineBridge::automationStateChanged, this, [this](const QString& owner) {
        if (owner == trackId_ || owner.isEmpty()) {
            refreshAutomation();
            refreshValue();
        }
    });
    connections_ << connect(session_->modulationLearn(), &sub::app::ModulationLearn::changed, this,
                            &DeviceParam::refreshMappable);
    // While its automation plays, it follows it.
    connections_ << connect(bridge, &EngineBridge::positionChanged, this, [this] {
        if (automation_ == QLatin1String("on"))
            refreshValue();
    });
}

void DeviceParam::refresh() {
    refreshSpec();
    refreshAutomation();
    refreshValue();
    refreshModulation();
    refreshMappable();
}

void DeviceParam::refreshModulation() {
    QString modulator;
    sub::app::ModulationMapping mapping;
    if (session_ && session_->project()->hasOwner(trackId_)) {
        for (const sub::app::Device* d : sub::app::iterDevices(session_->project()->track(trackId_).devices)) {
            if (!d->enabled)
                continue;  // (switched off, it moves nothing: the bridge sends the engine none of its mappings)
            for (const sub::app::ModulationMapping& m : d->modulations) {
                if (m.deviceId == deviceId_ && m.paramId == paramId_) {
                    modulator = d->id;
                    mapping = m;
                    break;
                }
            }
            if (!modulator.isEmpty())
                break;
        }
    }
    if (modulator == modulator_ && mapping == mapping_)
        return;
    if (modulator != modulator_) {
        if (!modulator_.isEmpty())
            ModulatorValues::instance()->unwatch(modulator_);
        if (!modulator.isEmpty())
            ModulatorValues::instance()->watch(session_, trackId_, modulator);
        modulationValue_ = -1.0;
    }
    modulator_ = modulator;
    mapping_ = mapping;
    Q_EMIT modulationChanged();
    refreshModulationValue();
}

void DeviceParam::refreshModulationValue() {
    double value = -1.0;
    if (!modulator_.isEmpty()) {
        if (const auto now = ModulatorValues::instance()->value(modulator_))
            value = mapping_.target(*now);
    }
    if (value == modulationValue_)
        return;
    modulationValue_ = value;
    Q_EMIT modulationValueChanged();
}

void DeviceParam::refreshMappable() {
    const sub::app::ModulationLearn* learn = session_ ? session_->modulationLearn() : nullptr;
    const bool mappable = learn && learn->active() && learn->trackId() == trackId_ &&
                          learn->modulatorId() != deviceId_ && canModulate();
    if (mappable == mappable_)
        return;
    mappable_ = mappable;
    Q_EMIT mappableChanged();
}

void DeviceParam::refreshSpec() {
    std::optional<sub::app::ProcessorParam> spec;
    int index = -1;
    if (session_ && !paramId_.isEmpty() && session_->project()->findDevice(trackId_, deviceId_)) {
        const QList<sub::app::ProcessorParam> params = session_->bridge()->deviceParams(trackId_, deviceId_);
        for (qsizetype i = 0; i < params.size(); ++i) {
            if (params[i].id == paramId_) {
                spec = params[i];
                index = static_cast<int>(i);
                break;
            }
        }
    }
    if (spec == spec_ && index == specIndex_)
        return;
    spec_ = spec;
    specIndex_ = index;
    Q_EMIT specChanged();
    Q_EMIT valueChanged();  // (its text, in its units)
}

void DeviceParam::refreshAutomation() {
    const QString state = session_ && !deviceId_.isEmpty() ? automationState(*session_->bridge(), trackId_, key())
                                                            : QString();
    if (state == automation_)
        return;
    automation_ = state;
    Q_EMIT automationChanged();
}

void DeviceParam::refreshValue() {
    double value = defaultValue();
    if (session_) {
        if (const sub::app::Device* device = session_->project()->findDevice(trackId_, deviceId_)) {
            std::optional<double> current;
            if (automation_ == QLatin1String("on"))
                current = session_->bridge()->currentValue(trackId_, key());
            value = current ? *current : device->params.value(paramId_, defaultValue());
        }
    }
    if (value == value_)
        return;
    value_ = value;
    Q_EMIT valueChanged();
}

QString DeviceParam::name() const { return spec_ ? spec_->name : paramId_; }
QString DeviceParam::unit() const { return spec_ ? spec_->unit : QString(); }
double DeviceParam::minimum() const { return spec_ ? spec_->minValue : 0.0; }
double DeviceParam::maximum() const { return spec_ ? spec_->maxValue : 1.0; }
double DeviceParam::defaultValue() const { return spec_ ? spec_->defaultValue : 0.0; }
bool DeviceParam::logScale() const { return spec_ && spec_->logScale; }
int DeviceParam::steps() const { return spec_ ? spec_->steps : 0; }
QStringList DeviceParam::labels() const { return spec_ ? spec_->valueLabels : QStringList(); }

bool DeviceParam::bipolar() const {
    if (!spec_)
        return false;
    const QString& u = spec_->unit;
    return spec_->minValue < 0 && 0 < spec_->maxValue &&
           (u.isEmpty() || u == QLatin1String("st") || u == QLatin1String("ct"));
}

int DeviceParam::index() const {
    const QStringList names = labels();
    const int index = static_cast<int>(std::nearbyint(value_));
    return names.isEmpty() ? index : std::clamp(index, 0, int(names.size()) - 1);
}

QString DeviceParam::format(double value) const {
    const QStringList names = labels();
    if (!names.isEmpty())
        return names.at(std::clamp(static_cast<int>(std::nearbyint(value)), 0, int(names.size()) - 1));
    if (spec_ && spec_->ownText && session_)  // (Vital's: as the device shows it)
        return session_->bridge()->deviceParamText(trackId_, deviceId_, specIndex_, value);
    return sub::app::formatValue(value, unit());
}

namespace {

// A text's form: each number's figures after its first as '#' ("-12.5 dB" is "-1#.# dB").
QString textForm(QString text) {
    bool inNumber = false;
    for (QChar& c : text) {
        if (c.isDigit()) {
            if (inNumber)
                c = QLatin1Char('#');
            inNumber = true;
        } else if (c != QLatin1Char('.')) {
            inNumber = false;
        }
    }
    return text;
}

// Every form `text(value)` takes from `lo` to `hi` (on a log scale if `log`), in the order found (see textForms()).
QStringList formsOver(double lo, double hi, bool log, const std::function<QString(double)>& text) {
    log = log && lo > 0;
    auto formAt = [&](double t) { return textForm(text(log ? lo * std::pow(hi / lo, t) : lo + (hi - lo) * t)); };
    // Whether no other form lies between two: they are the same, or differ only in one figure, by one.
    auto adjoining = [](const QString& a, const QString& b) {
        if (a.size() != b.size())
            return false;
        qsizetype differs = -1;
        for (qsizetype i = 0; i < a.size(); ++i) {
            if (a[i] != b[i]) {
                if (differs >= 0)
                    return false;
                differs = i;
            }
        }
        return differs < 0 || (a[differs].isDigit() && b[differs].isDigit()
                               && std::abs(a[differs].unicode() - b[differs].unicode()) == 1);
    };
    QStringList forms;
    auto add = [&](const QString& form) {
        if (!forms.contains(form))
            forms << form;
    };
    std::function<void(double, const QString&, double, const QString&, int)> between =
        [&](double t0, const QString& a, double t1, const QString& b, int depth) {
            if (depth == 0 || adjoining(a, b))
                return;
            const double t = (t0 + t1) / 2;
            const QString form = formAt(t);
            add(form);
            between(t0, a, t, form, depth - 1);
            between(t, form, t1, b, depth - 1);
        };
    std::vector<double> points;
    for (int i = 0; i <= 32; ++i)
        points.push_back(i / 32.0);
    for (int n = -3; n <= 6; ++n) {
        for (const double power : {std::pow(10.0, n), -std::pow(10.0, n)}) {
            if (const double under = power * (1 - 1e-9); under > lo && under < hi)
                points.push_back(log ? std::log(under / lo) / std::log(hi / lo) : (under - lo) / (hi - lo));
        }
    }
    std::sort(points.begin(), points.end());
    double t0 = points.front();
    QString a = formAt(t0);
    add(a);
    for (size_t i = 1; i < points.size(); ++i) {
        const QString b = formAt(points[i]);
        add(b);
        between(t0, a, points[i], b, 6);
        t0 = points[i];
        a = b;
    }
    return forms;
}

}  // namespace

QStringList DeviceParam::textForms(const QJSValue& formatter) const {
    if (!valid())
        return {};
    if (formatter.isCallable()) {
        QJSValue call = formatter;
        return formsOver(minimum(), maximum(), logScale(), [&](double value) {
            return call.call({QJSValue(steps() > 0 ? std::round(value) : value)}).toString();
        });
    }
    // (the same for every parameter with the same text rule: worked out once)
    static QHash<QString, QStringList> known;
    const QString key = QStringList{unit(), labels().join(QChar(1)), QString::number(minimum(), 'g', 17),
                                    QString::number(maximum(), 'g', 17), QString::number(int(logScale())),
                                    QString::number(steps())}
                            .join(QChar(0));
    if (const auto found = known.constFind(key); found != known.cend())
        return *found;
    const QStringList forms = formsOver(minimum(), maximum(), logScale(),
                                        [&](double value) { return format(steps() > 0 ? std::round(value) : value); });
    known.insert(key, forms);
    return forms;
}

QVariant DeviceParam::parse(const QString& text) const {
    if (unit() == QLatin1String("Hz")) {  // a frequency: "1.5k", "1500 Hz", "2 kHz"
        QString cleaned = text.trimmed().toLower();
        if (cleaned.endsWith(QLatin1String("hz")))
            cleaned = cleaned.chopped(2).trimmed();
        double scale = 1.0;
        if (cleaned.endsWith(QLatin1Char('k'))) {
            cleaned = cleaned.chopped(1).trimmed();
            scale = 1000.0;
        }
        bool ok = false;
        const double value = QLocale::c().toDouble(cleaned, &ok);
        return ok && std::isfinite(value) ? QVariant(value * scale) : QVariant::fromValue(nullptr);
    }
    return ValueBoxItem::parseNumber(text);
}

void DeviceParam::set(double value, const QString& mergeKey) {
    if (!session_ || !session_->project()->findDevice(trackId_, deviceId_))
        return;
    if (steps() > 0 || isList())
        value = std::nearbyint(value);
    session_->editor()->setDeviceParam(trackId_, deviceId_, paramId_, value, mergeKey);
    refreshValue();
}

void DeviceParam::touch() {
    if (session_ && session_->project()->hasOwner(trackId_))
        session_->editor()->touchParameter(trackId_, key());
}

AutomationTarget DeviceParam::automationTarget() const { return {session_, trackId_, key()}; }

bool DeviceParam::canAutomate() const { return automationTarget().canAutomate(); }

bool DeviceParam::hasEnvelope() const { return automationTarget().hasEnvelope(); }

bool DeviceParam::isOverridden() const { return automationTarget().isOverridden(); }

void DeviceParam::showAutomation() { automationTarget().show(); }

void DeviceParam::deleteAutomation() { automationTarget().deleteEnvelope(); }

void DeviceParam::reEnableAutomation() { automationTarget().reEnable(); }

QString DeviceParam::rackId() const {
    if (!session_)
        return {};
    const Project* project = session_->project();
    const sub::app::Track* track = project->findTrack(trackId_);
    if (!track)
        return {};
    const std::optional<QString> chain = sub::app::containerOf(track->devices, deviceId_);
    if (!chain)
        return {};
    try {
        return project->chainRack(trackId_, *chain).id;
    } catch (const std::exception&) {
        return {};
    }
}

QStringList DeviceParam::macroNames() const {
    QStringList names;
    const QString rack = rackId();
    const sub::app::Device* device = rack.isEmpty() ? nullptr : session_->project()->findDevice(trackId_, rack);
    for (int i = 0; device != nullptr && i < sub::app::macroCount(*device); ++i)
        names << sub::app::macroName(*device, i);
    return names;
}

int DeviceParam::macro() const {
    if (!session_)
        return -1;
    const auto mapped = session_->editor()->macroOf(trackId_, deviceId_, paramId_);
    return mapped ? mapped->second : -1;
}

QString DeviceParam::macroRack() const {
    if (!session_)
        return {};
    const auto mapped = session_->editor()->macroOf(trackId_, deviceId_, paramId_);
    return mapped ? mapped->first : QString();
}

void DeviceParam::mapToMacro(int index) {
    const QString rack = rackId();
    if (session_ && !rack.isEmpty())
        session_->editor()->tryMapMacro(trackId_, rack, index, deviceId_, paramId_);
}

QString DeviceParam::modulatorName() const {
    const sub::app::Device* device =
        session_ && !modulator_.isEmpty() ? session_->project()->findDevice(trackId_, modulator_) : nullptr;
    return device ? sub::app::deviceName(*device) : QString();
}

double DeviceParam::modulationFrom() const { return modulated() ? std::min(mapping_.low, mapping_.high) : 0.0; }

double DeviceParam::modulationTo() const { return modulated() ? std::max(mapping_.low, mapping_.high) : 1.0; }

bool DeviceParam::canModulate() const {
    return session_ && session_->project()->findDevice(trackId_, deviceId_) &&
           session_->editor()->canModulate(trackId_, deviceId_, paramId_);
}

QVariantList DeviceParam::modulatorChoices() const {
    QVariantList choices;
    if (!canModulate())
        return choices;
    for (const QString& id : session_->editor()->modulators(trackId_)) {
        if (id == deviceId_)
            continue;
        if (const sub::app::Device* device = session_->project()->findDevice(trackId_, id)) {
            choices.append(QVariantMap{{QStringLiteral("id"), id}, {QStringLiteral("name"), sub::app::deviceName(*device)}});
        }
    }
    return choices;
}

void DeviceParam::modulateWith(const QString& modulatorId) {
    if (session_)
        session_->editor()->tryMapModulation(trackId_, modulatorId, deviceId_, paramId_);
}

void DeviceParam::unmodulate() {
    if (session_)
        session_->editor()->unmapModulation(trackId_, deviceId_, paramId_);
}

void DeviceParam::setModulationInverted(bool inverted) {
    if (session_ && modulated())
        session_->editor()->setModulationInverted(trackId_, modulator_, deviceId_, paramId_, inverted);
}

void DeviceParam::setModulationAmount(double amount, double base, const QString& mergeKey) {
    if (session_ && modulated() && spec_) {
        session_->editor()->setModulationAmount(trackId_, modulator_, deviceId_, paramId_, amount,
                                                spec_->toNormalized(base), mergeKey);
    }
}

void DeviceParam::toggleModulationPolarity(double base) {
    if (session_ && modulated() && spec_) {
        session_->editor()->setModulationBipolar(trackId_, modulator_, deviceId_, paramId_, !mapping_.bipolar,
                                                 spec_->toNormalized(base));
    }
}

void DeviceParam::unmapFromMacro() {
    if (!session_)
        return;
    const auto mapped = session_->editor()->macroOf(trackId_, deviceId_, paramId_);
    if (mapped)
        session_->editor()->unmapMacro(trackId_, mapped->first, deviceId_, paramId_);
}

}  // namespace sub::ui
