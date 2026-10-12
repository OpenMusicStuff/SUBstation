#pragma once
// The MCP server's transport: Streamable HTTP on this computer only
// (http://127.0.0.1:<port>/mcp), on the application's thread.
//
// Every JSON-RPC message is a POST of its own to /mcp, answered with one JSON
// object (no SSE streams: nothing here runs long enough to report progress), or
// 202 for a notification. GET (a legacy client's stream of server messages) and
// DELETE (ending a legacy session, which there never is) are 405. HTTP/1.1,
// kept alive; bodies by Content-Length or chunked, at most kMaxBody.
//
// Only local programs reach it: it listens on the loopback address alone, and
// against DNS rebinding (a web page whose name was made to point at 127.0.0.1)
// a request whose Host isn't a loopback name, or whose Origin isn't a loopback
// page, is refused with 403.

#include <QHash>
#include <QHostAddress>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTcpServer>

namespace sub::app::mcp {

class Protocol;

class HttpServer : public QObject {
    Q_OBJECT

public:
    static constexpr qsizetype kMaxBody = 4 * 1024 * 1024;
    static constexpr qsizetype kMaxHeaders = 64 * 1024;
    inline static const QString kPath = QStringLiteral("/mcp");

    // `protocol` answers the messages (it outlives the server).
    explicit HttpServer(Protocol& protocol, QObject* parent = nullptr);
    ~HttpServer() override;

    // Listens on the loopback address (port 0: any free one). Whether it could
    // (else error() says why: the port taken...).
    bool listen(quint16 port);
    void close();
    bool isListening() const { return server_.isListening(); }
    quint16 port() const { return server_.serverPort(); }
    QString error() const { return error_; }

    // Whether an Origin (or a Host) names this computer: localhost, 127.0.0.1, [::1], any port.
    static bool isLocalOrigin(const QString& origin);
    static bool isLocalHost(const QString& host);

private:
    struct Connection;
    void accept();
    void read(QTcpSocket* socket);
    // One request parsed out of the connection's buffer, answered; false: not all of it is there yet.
    bool serve(QTcpSocket* socket, Connection& connection);
    void respond(QTcpSocket* socket, int status, const QByteArray& body, bool close,
                 const QList<QPair<QByteArray, QByteArray>>& headers = {});

    Protocol& protocol_;
    QTcpServer server_;
    QString error_;
    QHash<QTcpSocket*, Connection*> connections_;
};

}  // namespace sub::app::mcp
