#include "mcp/McpSupport.h"

#include "audio/EngineBridge.h"
#include "editor/AgentHistory.h"
#include "mcp/McpServer.h"
#include "model/Automation.h"
#include "model/Errors.h"
#include "model/Project.h"
#include "session/Session.h"

#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace sub::app::mcp {

QJsonArray strings(const std::vector<std::string>& values) {
    QJsonArray out;
    for (const std::string& value : values) out.append(QString::fromStdString(value));
    return out;
}

QJsonObject schema(const QJsonObject& properties, const QStringList& required) {
    QJsonObject out{{QStringLiteral("type"), QStringLiteral("object")},
                    {QStringLiteral("properties"), properties},
                    {QStringLiteral("additionalProperties"), false}};
    if (!required.isEmpty()) out.insert(QStringLiteral("required"), QJsonArray::fromStringList(required));
    return out;
}

QJsonObject property(const QString& type, const QString& description) {
    return {{QStringLiteral("type"), type}, {QStringLiteral("description"), description}};
}

QJsonValue orNull(const QString& value) { return value.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(value); }

// A number as the model reads it: no more digits than it means.
QString number(double value, int decimals) {
    QString text = QString::number(value, 'f', decimals);
    if (text.contains(u'.')) {
        while (text.endsWith(u'0')) text.chop(1);
        if (text.endsWith(u'.')) text.chop(1);
    }
    return text == u"-0" ? QStringLiteral("0") : text;
}

// Whether every word of `query` is in `text` (any case).
bool matches(const QString& text, const QStringList& words) {
    return std::all_of(words.begin(), words.end(), [&](const QString& word) { return text.contains(word, Qt::CaseInsensitive); });
}

// A beat as the model reads it: to a ten-thousandth (a 1/2560th note), not 579.7826893194218.
double beat(double value) { return std::round(value * 1e4) / 1e4; }

// A level, a pan, a value as the model reads it: to a hundredth.
double rounded(double value) {
    const double out = std::round(value * 100.0) / 100.0;
    return out == 0.0 ? 0.0 : out;  // (not -0)
}

// A pitch as given: a MIDI note number, or a name as notes::noteName() writes
// them ("C3" is 60; "F#2", "Bb4", "C-1"). None: neither, or out of 0..127.
std::optional<int> pitchOf(const QJsonValue& value) {
    int pitch = -1;
    if (value.isDouble()) {
        const double number = value.toDouble();
        if (number != std::floor(number)) return std::nullopt;
        pitch = static_cast<int>(number);
    } else if (value.isString()) {
        static const QRegularExpression name(QStringLiteral("^\\s*([A-Ga-g])([#b]?)(-?\\d+)\\s*$"));
        const QRegularExpressionMatch match = name.match(value.toString());
        if (!match.hasMatch()) return std::nullopt;
        static constexpr int kSteps[] = {9, 11, 0, 2, 4, 5, 7};  // A B C D E F G
        const int step = kSteps[match.captured(1).toUpper().at(0).unicode() - u'A'];
        const int accidental = match.captured(2) == u"#" ? 1 : match.captured(2) == u"b" ? -1 : 0;
        pitch = (match.captured(3).toInt() + 2) * 12 + step + accidental;
    } else {
        return std::nullopt;
    }
    if (pitch < 0 || pitch > 127) return std::nullopt;
    return pitch;
}

const QString kNoEdits = QStringLiteral(
    "Agents may not edit this project: the user turned edits off (Preferences › Agents). Tell them what you would "
    "change instead.");

const Project& ToolContext::project() const { return *session_.project(); }

double ToolContext::beatsPerBar() const { return project().timeSignature().beatsPerBar(); }
// A beat as a bar: 1 + beats / beats a bar, so bar 17 is 17 and halfway through it 17.5.
double ToolContext::bar(double beatValue) const { return beat(1.0 + beatValue / beatsPerBar()); }
// A stretch as the user reads it: "bars 17 to 24" (whole bars), "bar 5", else "beats 3.5 to 6".
QString ToolContext::stretch(double from, double to) const {
    const double first = from / beatsPerBar();
    const double last = to / beatsPerBar();
    const auto whole = [](double v) { return std::abs(v - std::round(v)) < 1e-6; };
    if (!whole(first) || !whole(last)) return QStringLiteral("beats %1 to %2").arg(number(from, 3), number(to, 3));
    const int a = static_cast<int>(std::round(first)) + 1;
    const int b = static_cast<int>(std::round(last));
    return a == b ? QStringLiteral("bar %1").arg(a) : QStringLiteral("bars %1 to %2").arg(a).arg(b);
}

Span ToolContext::span(const QJsonObject& arguments, bool required) const {
    Span out;
    const QJsonValue startBeat = arguments.value(QStringLiteral("start_beat"));
    const QJsonValue startBar = arguments.value(QStringLiteral("start_bar"));
    const QJsonValue endBeat = arguments.value(QStringLiteral("end_beat"));
    const QJsonValue endBar = arguments.value(QStringLiteral("end_bar"));
    if (!startBeat.isUndefined() && !startBar.isUndefined()) {
        out.error = QStringLiteral("Give start_beat or start_bar, not both.");
    } else if (!endBeat.isUndefined() && !endBar.isUndefined()) {
        out.error = QStringLiteral("Give end_beat or end_bar, not both.");
    } else if (required && ((startBeat.isUndefined() && startBar.isUndefined()) ||
                            (endBeat.isUndefined() && endBar.isUndefined()))) {
        out.error = QStringLiteral("Where it starts and ends is required: start_beat and end_beat, or start_bar and "
                                   "end_bar (bars 17 to 24: start_bar 17, end_bar 25, where bar 25 starts).");
    } else if ((!startBeat.isUndefined() && !startBeat.isDouble()) || (!startBar.isUndefined() && !startBar.isDouble()) ||
               (!endBeat.isUndefined() && !endBeat.isDouble()) || (!endBar.isUndefined() && !endBar.isDouble())) {
        out.error = QStringLiteral("Beats and bars are numbers.");
    } else if ((startBar.isDouble() && startBar.toDouble() < 1.0) || (endBar.isDouble() && endBar.toDouble() < 1.0)) {
        out.error = QStringLiteral("Bars count from 1.");
    }
    if (!out.error.isEmpty()) return out;
    if (startBeat.isDouble()) out.from = startBeat.toDouble();
    if (startBar.isDouble()) out.from = (startBar.toDouble() - 1.0) * beatsPerBar();
    if (endBeat.isDouble()) out.to = endBeat.toDouble();
    if (endBar.isDouble()) out.to = (endBar.toDouble() - 1.0) * beatsPerBar();
    if (out.to <= out.from) out.error = QStringLiteral("The end must come after the start.");
    return out;
}

std::vector<Param> ToolContext::params(const QString& trackId, const Device& device) const {
    std::vector<Param> out;
    if (!session_.bridge()) return out;
    for (ParamSpec& spec : session_.bridge()->deviceParamSpecs(trackId, device)) {
        const auto target = automation::parseKey(spec.key);
        const QString id = target ? target->param : spec.key;
        out.push_back({std::move(spec), id});
    }
    return out;
}

bool ToolContext::editsAllowed() const { return !session_.mcp() || session_.mcp()->editsAllowed(); }

ToolResult ToolContext::change(const QString& text, const QString& tool, QJsonObject out,
                               const std::function<void(ProjectEditor&)>& edit) {
    std::optional<int> id;
    try {
        id = session_.agentChanges()->record(text, tool, edit);
    } catch (const EditError& error) {
        return ToolResult::failure(error.message());
    }
    out.insert(QStringLiteral("changeId"), id ? QJsonValue(*id) : QJsonValue(QJsonValue::Null));
    out.insert(QStringLiteral("change"), id ? text : QStringLiteral("Nothing changed: it was so already."));
    return {out, {}, false};
}

std::pair<std::optional<double>, QString> ToolContext::wanted(const Param& param, const QJsonObject& item) {
    const ParamSpec& spec = param.spec;
    const QJsonValue value = item.value(QStringLiteral("value"));
    const QJsonValue normalized = item.value(QStringLiteral("normalized"));
    if (value.isUndefined() == normalized.isUndefined()) {
        return {std::nullopt, QStringLiteral("%1: give value or normalized (one of them).").arg(param.id)};
    }
    if (!normalized.isUndefined()) {
        const double n = normalized.toDouble(-1.0);
        if (!(n >= 0.0 && n <= 1.0)) return {std::nullopt, QStringLiteral("%1: normalized is 0 to 1.").arg(param.id)};
        return {spec.fromNormalized(spec.quantize(n)), {}};
    }
    if (value.isBool()) {
        if (spec.steps != 1) return {std::nullopt, QStringLiteral("%1 isn't a switch: give a number.").arg(param.id)};
        return {value.toBool() ? spec.maximum : spec.minimum, {}};
    }
    if (value.isString()) {
        const QString name = value.toString().trimmed();
        for (qsizetype i = 0; i < spec.labels.size(); ++i) {
            if (spec.labels[i].compare(name, Qt::CaseInsensitive) != 0) continue;
            const double n = spec.steps > 0 ? static_cast<double>(i) / spec.steps : 0.0;
            return {spec.fromNormalized(spec.quantize(n)), {}};
        }
        if (spec.labels.isEmpty()) {
            return {std::nullopt, QStringLiteral("%1 takes a number from %2 to %3.")
                                      .arg(param.id, number(spec.minimum), number(spec.maximum))};
        }
        return {std::nullopt, QStringLiteral("%1 has no choice \"%2\": it is one of %3.")
                                  .arg(param.id, name, spec.labels.join(QStringLiteral(", ")))};
    }
    if (!value.isDouble()) return {std::nullopt, QStringLiteral("%1: value is a number.").arg(param.id)};
    const double plain = value.toDouble();
    const double slack = (spec.maximum - spec.minimum) * 1e-9;
    if (plain < spec.minimum - slack || plain > spec.maximum + slack) {
        return {std::nullopt, QStringLiteral("%1 goes from %2 to %3%4 (asked for %5).")
                                  .arg(param.id, number(spec.minimum), number(spec.maximum),
                                       spec.unit.isEmpty() ? QString() : u' ' + spec.unit, number(plain))};
    }
    return {spec.fromNormalized(spec.quantize(spec.toNormalized(plain))), {}};
}

void addTool(Protocol& protocol, const std::shared_ptr<const ToolContext>& context, Tool tool) {
    if (!tool.readOnly) {
        tool.call = [context, call = std::move(tool.call)](const QJsonObject& arguments) {
            return context->editsAllowed() ? call(arguments) : ToolResult::failure(kNoEdits);
        };
    }
    protocol.addTool(std::move(tool));
}

}  // namespace sub::app::mcp
