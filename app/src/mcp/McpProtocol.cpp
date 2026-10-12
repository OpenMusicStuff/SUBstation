#include "mcp/McpProtocol.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>
#include <utility>

namespace sub::app::mcp {

namespace {

const QString kMetaVersion = QStringLiteral("io.modelcontextprotocol/protocolVersion");
const QString kMetaServerInfo = QStringLiteral("io.modelcontextprotocol/serverInfo");
// How long a client may keep the tool list and what server/discover says (they
// change only with the application): 10 minutes.
constexpr int kListTtlMs = 10 * 60 * 1000;

QJsonObject response(const QJsonValue& id, const QJsonObject& result) {
    return {{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), id},
            {QStringLiteral("result"), result}};
}

QJsonObject errorResponse(const QJsonValue& id, int code, const QString& message, const QJsonValue& data = {}) {
    QJsonObject error{{QStringLiteral("code"), code}, {QStringLiteral("message"), message}};
    if (!data.isUndefined() && !data.isNull()) error.insert(QStringLiteral("data"), data);
    QJsonObject out{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("error"), error}};
    if (!id.isUndefined()) out.insert(QStringLiteral("id"), id);  // (none: an error about no request in particular)
    return out;
}

Reply error(int status, const QJsonValue& id, int code, const QString& message, const QJsonValue& data = {}) {
    return {status, errorResponse(id, code, message, data)};
}

Reply unsupported(const QJsonValue& id, const QString& requested) {
    return error(400, id, code::kUnsupportedVersion,
                 QStringLiteral("Unsupported protocol version %1 (this server speaks %2)")
                     .arg(requested, supportedVersions().join(QStringLiteral(", "))),
                 QJsonObject{{QStringLiteral("supported"), QJsonArray::fromStringList(supportedVersions())},
                             {QStringLiteral("requested"), requested}});
}

}  // namespace

QStringList supportedVersions() { return QStringList{kModernVersion} + kLegacyVersions; }

Protocol::Protocol(QString name, QString version, QString instructions)
    : name_(std::move(name)), version_(std::move(version)), instructions_(std::move(instructions)) {}

void Protocol::addTool(Tool tool) { tools_.push_back(std::move(tool)); }

QString Protocol::decodeHeaderValue(const QString& value) {
    static const QString prefix = QStringLiteral("=?base64?");
    static const QString suffix = QStringLiteral("?=");
    if (!value.startsWith(prefix) || !value.endsWith(suffix) || value.size() < prefix.size() + suffix.size()) {
        return value;
    }
    const QString encoded = value.mid(prefix.size(), value.size() - prefix.size() - suffix.size());
    return QString::fromUtf8(QByteArray::fromBase64(encoded.toLatin1()));
}

Reply Protocol::invalid(int code, const QString& message) {
    return error(400, QJsonValue(QJsonValue::Null), code, message);
}

QJsonObject Protocol::serverInfo() const {
    return {{QStringLiteral("name"), name_}, {QStringLiteral("version"), version_}};
}

QJsonObject Protocol::capabilities() const {
    return {{QStringLiteral("tools"), QJsonObject{{QStringLiteral("listChanged"), false}}}};
}

QJsonObject Protocol::result(const QJsonObject& fields, bool modern) const {
    QJsonObject out = fields;
    if (modern) {
        out.insert(QStringLiteral("resultType"), QStringLiteral("complete"));
        out.insert(QStringLiteral("_meta"), QJsonObject{{kMetaServerInfo, serverInfo()}});
    }
    return out;
}

QJsonObject Protocol::listTools(bool modern) const {
    QJsonArray tools;
    for (const Tool& tool : tools_) {
        tools.append(QJsonObject{
            {QStringLiteral("name"), tool.name},
            {QStringLiteral("title"), tool.title},
            {QStringLiteral("description"), tool.description},
            {QStringLiteral("inputSchema"), tool.inputSchema},
            {QStringLiteral("annotations"), QJsonObject{{QStringLiteral("title"), tool.title},
                                                        {QStringLiteral("readOnlyHint"), tool.readOnly},
                                                        {QStringLiteral("openWorldHint"), false}}},
        });
    }
    QJsonObject fields{{QStringLiteral("tools"), tools}};
    if (modern) {
        fields.insert(QStringLiteral("ttlMs"), kListTtlMs);
        fields.insert(QStringLiteral("cacheScope"), QStringLiteral("private"));
    }
    return result(fields, modern);
}

Reply Protocol::callTool(const QJsonValue& id, const QJsonObject& params, bool modern) {
    const QString name = params.value(QStringLiteral("name")).toString();
    const auto tool = std::find_if(tools_.begin(), tools_.end(), [&](const Tool& t) { return t.name == name; });
    if (tool == tools_.end()) return error(200, id, code::kInvalidParams, QStringLiteral("Unknown tool: %1").arg(name));
    const QJsonValue arguments = params.value(QStringLiteral("arguments"));
    if (!arguments.isUndefined() && !arguments.isNull() && !arguments.isObject()) {
        return error(200, id, code::kInvalidParams, QStringLiteral("A tool's arguments are an object."));
    }
    const ToolResult called = tool->call(arguments.toObject());
    QString text = called.text;
    if (text.isEmpty() && !called.structured.isUndefined() && !called.structured.isNull()) {
        text = QString::fromUtf8(called.structured.isArray()
                                     ? QJsonDocument(called.structured.toArray()).toJson(QJsonDocument::Indented)
                                     : QJsonDocument(called.structured.toObject()).toJson(QJsonDocument::Indented));
    }
    QJsonObject fields{
        {QStringLiteral("content"),
         QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("text")}, {QStringLiteral("text"), text}}}}};
    // (Structured content was an object before 2026-07-28: a list goes in one.)
    if (!called.structured.isUndefined() && !called.structured.isNull()) {
        fields.insert(QStringLiteral("structuredContent"),
                      called.structured.isObject() || modern
                          ? called.structured
                          : QJsonValue(QJsonObject{{QStringLiteral("result"), called.structured}}));
    }
    if (called.error) fields.insert(QStringLiteral("isError"), true);
    return {200, response(id, result(fields, modern))};
}

Reply Protocol::handle(const QJsonObject& message, const Headers& headers) {
    const QJsonValue id = message.value(QStringLiteral("id"));
    const QString method = message.value(QStringLiteral("method")).toString();
    if (message.value(QStringLiteral("jsonrpc")).toString() != QStringLiteral("2.0") || method.isEmpty()) {
        if (message.contains(QStringLiteral("result")) || message.contains(QStringLiteral("error"))) {
            return invalid(code::kInvalidRequest, QStringLiteral("This server sends no requests: it takes no responses."));
        }
        return invalid(code::kInvalidRequest, QStringLiteral("Not a JSON-RPC 2.0 request."));
    }
    const bool notification = !message.contains(QStringLiteral("id"));
    if (!notification && !id.isString() && !id.isDouble()) {
        return invalid(code::kInvalidRequest, QStringLiteral("A request's id is a string or a number."));
    }
    const QJsonValue paramsValue = message.value(QStringLiteral("params"));
    if (!paramsValue.isUndefined() && !paramsValue.isNull() && !paramsValue.isObject()) {
        return error(400, notification ? QJsonValue() : id, code::kInvalidParams, QStringLiteral("params is an object."));
    }
    const QJsonObject params = paramsValue.toObject();

    // Notifications (initialized, cancelled...) are taken: nothing here needs them.
    if (notification) return {202, std::nullopt};

    // The legacy handshake: the client's version if it is a legacy one, else the newest of them.
    if (method == QStringLiteral("initialize")) {
        const QString requested = params.value(QStringLiteral("protocolVersion")).toString();
        const QString version = kLegacyVersions.contains(requested) ? requested : kLegacyVersions.first();
        QJsonObject fields{{QStringLiteral("protocolVersion"), version},
                           {QStringLiteral("capabilities"), capabilities()},
                           {QStringLiteral("serverInfo"), serverInfo()}};
        if (!instructions_.isEmpty()) fields.insert(QStringLiteral("instructions"), instructions_);
        return {200, response(id, fields)};
    }

    // Which era the request is of: modern if it says its version in _meta (or its header names the modern one).
    const QJsonValue metaVersion = params.value(QStringLiteral("_meta")).toObject().value(kMetaVersion);
    const bool modern = metaVersion.isString() || headers.protocolVersion == kModernVersion;
    if (modern) {
        if (!metaVersion.isString()) {
            return error(400, id, code::kInvalidParams,
                         QStringLiteral("A %1 request says its protocol version in params._meta[\"%2\"].")
                             .arg(kModernVersion, kMetaVersion));
        }
        const QString version = metaVersion.toString();
        if (headers.http) {
            // The headers say what the body says (they route requests that aren't parsed).
            if (headers.protocolVersion != version) {
                return error(400, id, code::kHeaderMismatch,
                             QStringLiteral("Header mismatch: MCP-Protocol-Version %1 does not match the body's %2")
                                 .arg(headers.protocolVersion.value_or(QStringLiteral("(missing)")), version));
            }
            if (headers.method != method) {
                return error(400, id, code::kHeaderMismatch,
                             QStringLiteral("Header mismatch: Mcp-Method %1 does not match the body's %2")
                                 .arg(headers.method.value_or(QStringLiteral("(missing)")), method));
            }
            if (method == QStringLiteral("tools/call") || method == QStringLiteral("resources/read") ||
                method == QStringLiteral("prompts/get")) {
                const QString name = params.value(method == QStringLiteral("resources/read") ? QStringLiteral("uri")
                                                                                             : QStringLiteral("name"))
                                         .toString();
                if (headers.name != name) {
                    return error(400, id, code::kHeaderMismatch,
                                 QStringLiteral("Header mismatch: Mcp-Name %1 does not match the body's %2")
                                     .arg(headers.name.value_or(QStringLiteral("(missing)")), name));
                }
            }
        }
        if (version != kModernVersion) return unsupported(id, version);
    } else {
        const QString version = headers.protocolVersion.value_or(kVersionWithoutHeader);
        if (!kLegacyVersions.contains(version)) return unsupported(id, version);
    }

    if (method == QStringLiteral("server/discover")) {
        QJsonObject fields{{QStringLiteral("supportedVersions"), QJsonArray::fromStringList(supportedVersions())},
                           {QStringLiteral("capabilities"), capabilities()},
                           {QStringLiteral("ttlMs"), kListTtlMs},
                           {QStringLiteral("cacheScope"), QStringLiteral("private")}};
        if (!instructions_.isEmpty()) fields.insert(QStringLiteral("instructions"), instructions_);
        return {200, response(id, result(fields, true))};
    }
    if (method == QStringLiteral("ping") && !modern) return {200, response(id, QJsonObject())};
    if (method == QStringLiteral("tools/list")) return {200, response(id, listTools(modern))};
    if (method == QStringLiteral("tools/call")) return callTool(id, params, modern);
    // (Modern: 404, as the transport says. Legacy clients took a 404 for a session that had gone: 200 there.)
    return error(modern ? 404 : 200, id, code::kMethodNotFound, QStringLiteral("Method not found: %1").arg(method));
}

}  // namespace sub::app::mcp
