#pragma once
// The Model Context Protocol's messages (JSON-RPC 2.0), without a transport:
// what the MCP server (McpServer, Session.mcp) answers agents. A transport
// (McpHttpServer: Streamable HTTP on localhost) hands it each message with the
// headers that came with it and sends back what it answers.
//
// It speaks both eras of the protocol (a "dual-era" server, as the 2026-07-28
// revision calls it):
//
// - Modern (2026-07-28): no handshake and no session. Every request carries its
//   protocol version (and the client's capabilities) in params._meta; over HTTP
//   the MCP-Protocol-Version, Mcp-Method and Mcp-Name headers must say the same,
//   else 400 and HeaderMismatch. server/discover says what the server is.
//   Results have resultType "complete" and the server's identity in _meta;
//   lists say how long they may be cached (ttlMs, cacheScope).
// - Legacy (2025-03-26 to 2025-11-25): an initialize handshake first, answered
//   with the client's version if it is one of these (else the newest of them),
//   then requests without _meta. No session is kept (the server assigns no
//   Mcp-Session-Id, which those revisions allow): every request stands alone.
//
// Tools are added with addTool(); a tool's call gets its arguments and returns
// a ToolResult (its structured content, and the text the model reads). An error
// in the tool's use (a track that isn't there) is a result with isError, which
// the model sees; an unknown tool is a protocol error. Everything runs on the
// application's thread.

#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>
#include <vector>

namespace sub::app::mcp {

inline const QString kModernVersion = QStringLiteral("2026-07-28");
// The legacy versions answered by initialize, the newest first.
inline const QStringList kLegacyVersions{QStringLiteral("2025-11-25"), QStringLiteral("2025-06-18"),
                                         QStringLiteral("2025-03-26")};
// What a request without an MCP-Protocol-Version header is (the transport says so before 2025-06-18).
inline const QString kVersionWithoutHeader = QStringLiteral("2025-03-26");

// The JSON-RPC and MCP error codes it answers with.
namespace code {
inline constexpr int kParse = -32700;
inline constexpr int kInvalidRequest = -32600;
inline constexpr int kMethodNotFound = -32601;
inline constexpr int kInvalidParams = -32602;
inline constexpr int kInternal = -32603;
inline constexpr int kHeaderMismatch = -32020;
inline constexpr int kUnsupportedVersion = -32022;
}  // namespace code

// Every version it speaks, the modern one first.
QStringList supportedVersions();

// What a tool returns: its result as JSON (structuredContent), and the text
// the model reads (content; none: the JSON, indented). With `error`, the call
// failed in a way the model can correct (isError).
struct ToolResult {
    QJsonValue structured;
    QString text;
    bool error = false;

    static ToolResult failure(const QString& message) { return {QJsonValue(), message, true}; }
};

struct Tool {
    QString name;         // "list_tracks"
    QString title;        // "List Tracks"
    QString description;  // for the model: what it gives, when to use it
    QJsonObject inputSchema;  // a JSON Schema object ({"type": "object", ...})
    bool readOnly = true;
    std::function<ToolResult(const QJsonObject& arguments)> call;
};

// The headers a transport received with a message (none: not sent, or no HTTP).
struct Headers {
    bool http = false;  // checked only over HTTP
    std::optional<QString> protocolVersion;  // MCP-Protocol-Version
    std::optional<QString> method;           // Mcp-Method
    std::optional<QString> name;             // Mcp-Name (decoded from its =?base64?...?= form)
};

// The answer to one message: the HTTP status, and the JSON-RPC response (none:
// a notification taken, 202 with no body).
struct Reply {
    int status = 200;
    std::optional<QJsonObject> body;
};

class Protocol {
public:
    // The server's identity (its name, its version) and what it tells agents about itself.
    Protocol(QString name, QString version, QString instructions);

    // Tools are listed in the order they were added (deterministic, as the spec asks).
    void addTool(Tool tool);
    const std::vector<Tool>& tools() const { return tools_; }

    // One JSON-RPC message (a request or a notification) and its headers.
    Reply handle(const QJsonObject& message, const Headers& headers = {});
    // A body that isn't a JSON object (a parse error, a batch): the error to send back.
    static Reply invalid(int code, const QString& message);

    // An Mcp-Name (or Mcp-Param-*) header's value as sent: decoded if in its =?base64?...?= form.
    static QString decodeHeaderValue(const QString& value);

private:
    QJsonObject serverInfo() const;
    QJsonObject capabilities() const;
    QJsonObject result(const QJsonObject& fields, bool modern) const;
    QJsonObject listTools(bool modern) const;
    Reply callTool(const QJsonValue& id, const QJsonObject& params, bool modern);

    QString name_;
    QString version_;
    QString instructions_;
    std::vector<Tool> tools_;
};

}  // namespace sub::app::mcp
