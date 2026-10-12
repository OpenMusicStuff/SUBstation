#include "mcp/McpHttpServer.h"

#include "mcp/McpProtocol.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>
#include <QUrl>

namespace sub::app::mcp {

struct HttpServer::Connection {
    QByteArray buffer;
};

namespace {

QByteArray reason(int status) {
    switch (status) {
    case 200: return "OK";
    case 202: return "Accepted";
    case 400: return "Bad Request";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 413: return "Content Too Large";
    case 415: return "Unsupported Media Type";
    case 431: return "Request Header Fields Too Large";
    default: return "Error";
    }
}

QByteArray json(const QJsonObject& object) { return QJsonDocument(object).toJson(QJsonDocument::Compact); }

// A JSON-RPC error about no request in particular (a refused connection, a body that can't be read).
QByteArray errorBody(int code, const QString& message) {
    return json(*Protocol::invalid(code, message).body);
}

bool isLoopbackName(QString host) {
    host = host.toLower();
    return host == QStringLiteral("localhost") || host == QStringLiteral("127.0.0.1") || host == QStringLiteral("::1");
}

// A chunked body's bytes, once all of it is in `data` from `from` on; `end`: where the request ends.
// None: not all there yet; an empty optional's `bad` set: it isn't chunked as it says.
std::optional<QByteArray> unchunk(const QByteArray& data, qsizetype from, qsizetype& end, bool& bad) {
    QByteArray body;
    qsizetype at = from;
    while (true) {
        const qsizetype lineEnd = data.indexOf("\r\n", at);
        if (lineEnd < 0) return std::nullopt;
        bool ok = false;
        const qsizetype size = data.mid(at, lineEnd - at).split(';').front().trimmed().toLongLong(&ok, 16);
        if (!ok || size < 0 || body.size() + size > HttpServer::kMaxBody) {
            bad = true;
            return std::nullopt;
        }
        at = lineEnd + 2;
        if (size == 0) {
            // The trailer (ignored), to its empty line.
            while (true) {
                const qsizetype trailerEnd = data.indexOf("\r\n", at);
                if (trailerEnd < 0) return std::nullopt;
                const bool last = trailerEnd == at;
                at = trailerEnd + 2;
                if (last) break;
            }
            end = at;
            return body;
        }
        if (data.size() < at + size + 2) return std::nullopt;
        body += data.mid(at, size);
        at += size + 2;
    }
}

}  // namespace

HttpServer::HttpServer(Protocol& protocol, QObject* parent) : QObject(parent), protocol_(protocol) {
    connect(&server_, &QTcpServer::newConnection, this, &HttpServer::accept);
}

HttpServer::~HttpServer() { close(); }

bool HttpServer::listen(quint16 port) {
    close();
    error_.clear();
    if (server_.listen(QHostAddress::LocalHost, port)) return true;
    error_ = server_.errorString();
    return false;
}

void HttpServer::close() {
    server_.close();
    const auto sockets = connections_.keys();
    for (QTcpSocket* socket : sockets) socket->abort();  // (disconnected deletes them)
    qDeleteAll(connections_);
    connections_.clear();
}

bool HttpServer::isLocalHost(const QString& host) {
    QString name = host.trimmed();
    if (name.startsWith(u'[')) {
        const qsizetype close = name.indexOf(u']');
        if (close < 0) return false;
        name = name.mid(1, close - 1);
    } else if (name.count(u':') == 1) {
        name = name.section(u':', 0, 0);
    }
    return isLoopbackName(name);
}

bool HttpServer::isLocalOrigin(const QString& origin) {
    const QUrl url(origin);
    return url.isValid() && (url.scheme() == QStringLiteral("http") || url.scheme() == QStringLiteral("https")) &&
           isLoopbackName(url.host());
}

void HttpServer::accept() {
    while (QTcpSocket* socket = server_.nextPendingConnection()) {
        connections_.insert(socket, new Connection);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] { read(socket); });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
            delete connections_.take(socket);
            socket->deleteLater();
        });
    }
}

void HttpServer::read(QTcpSocket* socket) {
    Connection* connection = connections_.value(socket);
    if (!connection) return;
    connection->buffer += socket->readAll();
    while (connections_.contains(socket) && serve(socket, *connection)) {
    }
}

bool HttpServer::serve(QTcpSocket* socket, Connection& connection) {
    QByteArray& data = connection.buffer;
    const qsizetype headEnd = data.indexOf("\r\n\r\n");
    if (headEnd < 0) {
        if (data.size() > kMaxHeaders) {
            respond(socket, 431, errorBody(code::kInvalidRequest, QStringLiteral("Headers too large.")), true);
        }
        return false;
    }
    const QList<QByteArray> lines = data.left(headEnd).split('\n');
    const QList<QByteArray> requestLine = lines.front().trimmed().split(' ');
    if (requestLine.size() != 3 || !requestLine[2].startsWith("HTTP/1.")) {
        respond(socket, 400, errorBody(code::kInvalidRequest, QStringLiteral("Not an HTTP/1.1 request.")), true);
        return false;
    }
    const QByteArray method = requestLine[0];
    const QString target = QString::fromLatin1(requestLine[1]);
    QHash<QByteArray, QByteArray> headers;  // (names in lower case)
    for (qsizetype i = 1; i < lines.size(); ++i) {
        const qsizetype colon = lines[i].indexOf(':');
        if (colon <= 0) continue;
        headers.insert(lines[i].left(colon).trimmed().toLower(), lines[i].mid(colon + 1).trimmed());
    }

    // The body: chunked, or as long as Content-Length says (none: none).
    QByteArray body;
    qsizetype end = headEnd + 4;
    if (headers.value("transfer-encoding").toLower().contains("chunked")) {
        bool bad = false;
        const std::optional<QByteArray> unchunked = unchunk(data, end, end, bad);
        if (bad) {
            respond(socket, 413, errorBody(code::kInvalidRequest, QStringLiteral("Body too large, or badly chunked.")), true);
            return false;
        }
        if (!unchunked) return false;
        body = *unchunked;
    } else if (headers.contains("content-length")) {
        bool ok = false;
        const qsizetype length = headers.value("content-length").toLongLong(&ok);
        if (!ok || length < 0 || length > kMaxBody) {
            respond(socket, 413, errorBody(code::kInvalidRequest, QStringLiteral("Body too large.")), true);
            return false;
        }
        if (data.size() < end + length) return false;
        body = data.mid(end, length);
        end += length;
    }
    data.remove(0, end);

    const QByteArray connectionHeader = headers.value("connection").toLower();
    const bool close = requestLine[2] == "HTTP/1.0" ? !connectionHeader.contains("keep-alive")
                                                    : connectionHeader.contains("close");

    // Only programs on this computer, not web pages pointed at it (DNS rebinding).
    if (!isLocalHost(QString::fromLatin1(headers.value("host")))) {
        respond(socket, 403, errorBody(code::kInvalidRequest, QStringLiteral("Only for this computer (Host).")), close);
        return true;
    }
    if (headers.contains("origin") && !isLocalOrigin(QString::fromLatin1(headers.value("origin")))) {
        respond(socket, 403, errorBody(code::kInvalidRequest, QStringLiteral("Only for this computer (Origin).")), close);
        return true;
    }
    if (target.section(u'?', 0, 0) != kPath && target.section(u'?', 0, 0) != kPath + u'/') {
        respond(socket, 404, errorBody(code::kInvalidRequest, QStringLiteral("The MCP endpoint is %1.").arg(kPath)), close);
        return true;
    }
    if (method != "POST") {
        respond(socket, 405, errorBody(code::kInvalidRequest, QStringLiteral("POST JSON-RPC messages here.")), close,
                {{"Allow", "POST"}});
        return true;
    }
    if (!headers.value("content-type").toLower().startsWith("application/json")) {
        respond(socket, 415, errorBody(code::kInvalidRequest, QStringLiteral("Send application/json.")), close);
        return true;
    }

    QJsonParseError parsed{};
    const QJsonDocument document = QJsonDocument::fromJson(body, &parsed);
    Reply reply;
    if (parsed.error != QJsonParseError::NoError) {
        reply = Protocol::invalid(code::kParse, QStringLiteral("Not JSON: %1").arg(parsed.errorString()));
    } else if (!document.isObject()) {
        reply = Protocol::invalid(code::kInvalidRequest, QStringLiteral("One JSON-RPC message a request (no batches)."));
    } else {
        Headers given;
        given.http = true;
        if (headers.contains("mcp-protocol-version")) {
            given.protocolVersion = QString::fromLatin1(headers.value("mcp-protocol-version"));
        }
        if (headers.contains("mcp-method")) given.method = QString::fromLatin1(headers.value("mcp-method"));
        if (headers.contains("mcp-name")) {
            given.name = Protocol::decodeHeaderValue(QString::fromLatin1(headers.value("mcp-name")));
        }
        reply = protocol_.handle(document.object(), given);
    }
    respond(socket, reply.status, reply.body ? json(*reply.body) : QByteArray(), close);
    return true;
}

void HttpServer::respond(QTcpSocket* socket, int status, const QByteArray& body, bool close,
                         const QList<QPair<QByteArray, QByteArray>>& headers) {
    QByteArray out = "HTTP/1.1 " + QByteArray::number(status) + ' ' + reason(status) + "\r\n";
    if (!body.isEmpty()) out += "Content-Type: application/json\r\n";
    out += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    out += "Cache-Control: no-store\r\n";
    out += close ? "Connection: close\r\n" : "Connection: keep-alive\r\n";
    for (const auto& [name, value] : headers) out += name + ": " + value + "\r\n";
    out += "\r\n" + body;
    socket->write(out);
    if (close) {
        connections_.value(socket)->buffer.clear();
        socket->disconnectFromHost();
    }
}

}  // namespace sub::app::mcp
