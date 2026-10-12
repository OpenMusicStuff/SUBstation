#pragma once
// The MCP server (Session.mcp): agents (Claude Code, Claude Desktop...) reading
// the project open in SUBstation through the Model Context Protocol, at
// http://127.0.0.1:<port>/mcp while it is on.
//
// Off until the user turns it on (Options › MCP Server); on or off and the port
// (Preferences › Agents) are saved in the settings, so it starts again with the
// application. It answers on the application's thread (Protocol, HttpServer)
// with the session's tools (McpTools): reading the project, and editing it
// (unless the user turned edits off), each edit a change of the agents'
// history (AgentHistory), apart from the user's undo stack.

#include <QObject>
#include <QString>

#include <memory>

namespace sub::app {

class Session;

namespace mcp {
class HttpServer;
class Protocol;
}  // namespace mcp

class McpServer : public QObject {
    Q_OBJECT
    // Whether it should run (saved); running: whether it does (its port may be taken).
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    Q_PROPERTY(bool running READ running NOTIFY changed)
    // The port it listens on (saved; kDefaultPort at first).
    Q_PROPERTY(int port READ port WRITE setPort NOTIFY changed)
    // Where agents reach it ("http://127.0.0.1:7381/mcp"), and why it couldn't start ("": it could).
    Q_PROPERTY(QString url READ url NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    // The command that adds it to Claude Code (claude mcp add ...).
    Q_PROPERTY(QString claudeCodeCommand READ claudeCodeCommand NOTIFY changed)
    // Whether agents may edit the project (saved; on at first): each edit is a
    // change in Session.agentChanges. Off, they only read.
    Q_PROPERTY(bool editsAllowed READ editsAllowed WRITE setEditsAllowed NOTIFY changed)

public:
    static constexpr int kDefaultPort = 7381;
    inline static const QString kEnabledKey = QStringLiteral("mcp/enabled");
    inline static const QString kPortKey = QStringLiteral("mcp/port");
    inline static const QString kEditsKey = QStringLiteral("mcp/edits");

    // Starts at once if the settings say it is on.
    explicit McpServer(Session& session, QObject* parent = nullptr);
    ~McpServer() override;

    bool enabled() const { return enabled_; }
    void setEnabled(bool enabled);
    Q_INVOKABLE void toggle() { setEnabled(!enabled_); }
    bool running() const;
    int port() const { return port_; }
    void setPort(int port);
    QString url() const;
    QString error() const { return error_; }
    QString claudeCodeCommand() const;
    bool editsAllowed() const { return editsAllowed_; }
    void setEditsAllowed(bool allowed);

    mcp::Protocol& protocol() const { return *protocol_; }

Q_SIGNALS:
    void changed();
    // Started, stopped, or why it couldn't start (the status line).
    void statusMessage(const QString& message);

private:
    // Listens if enabled (again, on its port), else stops.
    void apply(bool announce);

    std::unique_ptr<mcp::Protocol> protocol_;
    std::unique_ptr<mcp::HttpServer> http_;
    bool enabled_ = false;
    int port_ = kDefaultPort;
    bool editsAllowed_ = true;
    QString error_;
};

}  // namespace sub::app
