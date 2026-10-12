// The MCP server (Session.mcp): the protocol in both eras (2026-07-28's
// per-request versions and headers, the legacy initialize handshake), its
// errors; the tools on a real session (the project, tracks with their labels,
// one track in full, finding tracks, the harmony); Streamable HTTP over a real
// socket on localhost (DNS rebinding refused, keep-alive, chunked bodies); and
// turning the server on and off, its port kept.

#include "SessionFixture.h"
#include "TestSupport.h"

#include "intelligence/TrackLabels.h"
#include "mcp/McpHttpServer.h"
#include "mcp/McpProtocol.h"
#include "mcp/McpServer.h"
#include "model/Automation.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

using namespace sub::app;
using test::SessionFixture;
namespace mcp = sub::app::mcp;

namespace {

const QString kMeta = QStringLiteral("io.modelcontextprotocol/protocolVersion");

QJsonObject request(const QString& method, const QJsonObject& params = {}, int id = 1) {
    QJsonObject out{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), id},
                    {QStringLiteral("method"), method}};
    if (!params.isEmpty()) out.insert(QStringLiteral("params"), params);
    return out;
}

// A modern request: its version in _meta (and the client's capabilities).
QJsonObject modern(const QString& method, QJsonObject params = {}, const QString& version = mcp::kModernVersion) {
    params.insert(QStringLiteral("_meta"),
                  QJsonObject{{kMeta, version}, {QStringLiteral("io.modelcontextprotocol/clientCapabilities"), QJsonObject()}});
    return request(method, params);
}

// The headers a modern client sends over HTTP for it.
mcp::Headers headersFor(const QJsonObject& message, const QString& version = mcp::kModernVersion) {
    mcp::Headers headers;
    headers.http = true;
    headers.protocolVersion = version;
    headers.method = message.value(QStringLiteral("method")).toString();
    const QJsonObject params = message.value(QStringLiteral("params")).toObject();
    if (params.contains(QStringLiteral("name"))) headers.name = params.value(QStringLiteral("name")).toString();
    return headers;
}

QJsonObject result(const mcp::Reply& reply) { return reply.body->value(QStringLiteral("result")).toObject(); }
int errorCode(const mcp::Reply& reply) {
    return reply.body->value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toInt();
}

// A tool's structured result (legacy era: no headers to send).
QJsonObject call(mcp::Protocol& protocol, const QString& tool, const QJsonObject& arguments = {}) {
    const mcp::Reply reply = protocol.handle(request(
        QStringLiteral("tools/call"), {{QStringLiteral("name"), tool}, {QStringLiteral("arguments"), arguments}}));
    return result(reply);
}

mcp::Protocol echoProtocol() {
    mcp::Protocol protocol(QStringLiteral("Test"), QStringLiteral("1.0"), QStringLiteral("Says things back."));
    protocol.addTool({QStringLiteral("echo"), QStringLiteral("Echo"), QStringLiteral("Says it back."),
                      QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}}, true,
                      [](const QJsonObject& arguments) -> mcp::ToolResult {
                          if (arguments.contains(QStringLiteral("fail"))) return mcp::ToolResult::failure(QStringLiteral("No."));
                          return {arguments, {}, false};
                      }});
    protocol.addTool({QStringLiteral("list"), QStringLiteral("List"), QStringLiteral("A list."),
                      QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}}, true,
                      [](const QJsonObject&) -> mcp::ToolResult { return {QJsonArray{1, 2}, {}, false}; }});
    return protocol;
}

// An HTTP exchange over a real socket, the event loop running meanwhile (the server is on this thread).
struct HttpResponse {
    int status = 0;
    QHash<QByteArray, QByteArray> headers;
    QByteArray body;
    QJsonObject json() const { return QJsonDocument::fromJson(body).object(); }
};

std::optional<HttpResponse> takeResponse(QByteArray& data) {
    const qsizetype headEnd = data.indexOf("\r\n\r\n");
    if (headEnd < 0) return std::nullopt;
    HttpResponse response;
    const QList<QByteArray> lines = data.left(headEnd).split('\n');
    response.status = lines.front().split(' ').value(1).toInt();
    for (qsizetype i = 1; i < lines.size(); ++i) {
        const qsizetype colon = lines[i].indexOf(':');
        response.headers.insert(lines[i].left(colon).trimmed().toLower(), lines[i].mid(colon + 1).trimmed());
    }
    const qsizetype length = response.headers.value("content-length").toLongLong();
    if (data.size() < headEnd + 4 + length) return std::nullopt;
    response.body = data.mid(headEnd + 4, length);
    data.remove(0, headEnd + 4 + length);
    return response;
}

class Client {
public:
    explicit Client(quint16 port) {
        socket_.connectToHost(QHostAddress::LocalHost, port);
        QTRY_VERIFY_WITH_TIMEOUT(socket_.state() == QAbstractSocket::ConnectedState, 5000);
    }
    std::optional<HttpResponse> send(const QByteArray& raw) {
        socket_.write(raw);
        std::optional<HttpResponse> response;
        for (int i = 0; i < 500 && !(response = takeResponse(buffer_)); ++i) {
            QTest::qWait(10);
            buffer_ += socket_.readAll();
        }
        return response;
    }
    bool closed() {
        for (int i = 0; i < 300 && socket_.state() != QAbstractSocket::UnconnectedState; ++i) QTest::qWait(10);
        return socket_.state() == QAbstractSocket::UnconnectedState;
    }

private:
    QTcpSocket socket_;
    QByteArray buffer_;
};

QByteArray post(const QJsonObject& message, const QByteArray& extraHeaders = {}, quint16 port = 0) {
    const QByteArray body = QJsonDocument(message).toJson(QJsonDocument::Compact);
    return "POST /mcp HTTP/1.1\r\nHost: 127.0.0.1:" + QByteArray::number(port) +
           "\r\nContent-Type: application/json\r\nAccept: application/json, text/event-stream\r\n" + extraHeaders +
           "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body;
}

}  // namespace

class TestMcp : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase() { test::prepareApplication(); }

    // --- The protocol ------------------------------------------------------------------------

    void legacyClientsInitializeAndList() {
        mcp::Protocol protocol = echoProtocol();
        // The client's version if it is a legacy one; else the newest of them.
        mcp::Reply reply = protocol.handle(request(
            QStringLiteral("initialize"),
            {{QStringLiteral("protocolVersion"), QStringLiteral("2025-06-18")},
             {QStringLiteral("capabilities"), QJsonObject()},
             {QStringLiteral("clientInfo"), QJsonObject{{QStringLiteral("name"), QStringLiteral("c")},
                                                        {QStringLiteral("version"), QStringLiteral("1")}}}}));
        QCOMPARE(reply.status, 200);
        QCOMPARE(result(reply).value(QStringLiteral("protocolVersion")).toString(), QStringLiteral("2025-06-18"));
        QVERIFY(result(reply).value(QStringLiteral("capabilities")).toObject().contains(QStringLiteral("tools")));
        QCOMPARE(result(reply).value(QStringLiteral("serverInfo")).toObject().value(QStringLiteral("name")).toString(),
                 QStringLiteral("Test"));
        QCOMPARE(result(reply).value(QStringLiteral("instructions")).toString(), QStringLiteral("Says things back."));
        reply = protocol.handle(request(QStringLiteral("initialize"), {{QStringLiteral("protocolVersion"), QStringLiteral("2099-01-01")}}));
        QCOMPARE(result(reply).value(QStringLiteral("protocolVersion")).toString(), mcp::kLegacyVersions.first());

        // Notifications are taken (202, nothing back).
        reply = protocol.handle({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                                 {QStringLiteral("method"), QStringLiteral("notifications/initialized")}});
        QCOMPARE(reply.status, 202);
        QVERIFY(!reply.body);

        // Requests without _meta: legacy (no resultType); without a header, 2025-03-26.
        reply = protocol.handle(request(QStringLiteral("tools/list")));
        QCOMPARE(reply.status, 200);
        const QJsonArray tools = result(reply).value(QStringLiteral("tools")).toArray();
        QCOMPARE(tools.size(), 2);
        QCOMPARE(tools[0].toObject().value(QStringLiteral("name")).toString(), QStringLiteral("echo"));  // (in order)
        QVERIFY(tools[0].toObject().value(QStringLiteral("annotations")).toObject().value(QStringLiteral("readOnlyHint")).toBool());
        QVERIFY(!result(reply).contains(QStringLiteral("resultType")));
        QCOMPARE(protocol.handle(request(QStringLiteral("ping"))).status, 200);
        // An unsupported version in the header.
        mcp::Headers old;
        old.http = true;
        old.protocolVersion = QStringLiteral("2024-11-05");
        reply = protocol.handle(request(QStringLiteral("tools/list")), old);
        QCOMPARE(reply.status, 400);
        QCOMPARE(errorCode(reply), mcp::code::kUnsupportedVersion);

        // A call: its text, its structured content (a list in an object, before 2026-07-28).
        QJsonObject called = call(protocol, QStringLiteral("echo"), {{QStringLiteral("a"), 1}});
        QCOMPARE(called.value(QStringLiteral("structuredContent")).toObject().value(QStringLiteral("a")).toInt(), 1);
        QVERIFY(called.value(QStringLiteral("content")).toArray()[0].toObject().value(QStringLiteral("text")).toString().contains(u'1'));
        called = call(protocol, QStringLiteral("list"));
        QCOMPARE(called.value(QStringLiteral("structuredContent")).toObject().value(QStringLiteral("result")).toArray().size(), 2);
        // The tool's error the model sees; an unknown tool is the protocol's.
        called = call(protocol, QStringLiteral("echo"), {{QStringLiteral("fail"), true}});
        QVERIFY(called.value(QStringLiteral("isError")).toBool());
        QCOMPARE(called.value(QStringLiteral("content")).toArray()[0].toObject().value(QStringLiteral("text")).toString(), QStringLiteral("No."));
        reply = protocol.handle(request(QStringLiteral("tools/call"), {{QStringLiteral("name"), QStringLiteral("nope")}}));
        QCOMPARE(errorCode(reply), mcp::code::kInvalidParams);
        // An unknown method: 200 for a legacy client (a 404 meant its session had gone).
        reply = protocol.handle(request(QStringLiteral("resources/list")));
        QCOMPARE(reply.status, 200);
        QCOMPARE(errorCode(reply), mcp::code::kMethodNotFound);
    }

    void modernRequestsCarryTheirVersion() {
        mcp::Protocol protocol = echoProtocol();
        // server/discover: the versions, the capabilities, how long to keep them.
        QJsonObject message = modern(QStringLiteral("server/discover"));
        mcp::Reply reply = protocol.handle(message, headersFor(message));
        QCOMPARE(reply.status, 200);
        QJsonObject discovered = result(reply);
        QCOMPARE(discovered.value(QStringLiteral("supportedVersions")).toArray().first().toString(), mcp::kModernVersion);
        QCOMPARE(discovered.value(QStringLiteral("resultType")).toString(), QStringLiteral("complete"));
        QVERIFY(discovered.value(QStringLiteral("ttlMs")).toInt() > 0);
        QCOMPARE(discovered.value(QStringLiteral("_meta")).toObject().value(QStringLiteral("io.modelcontextprotocol/serverInfo"))
                     .toObject().value(QStringLiteral("name")).toString(),
                 QStringLiteral("Test"));

        message = modern(QStringLiteral("tools/list"));
        reply = protocol.handle(message, headersFor(message));
        QCOMPARE(result(reply).value(QStringLiteral("cacheScope")).toString(), QStringLiteral("private"));
        QCOMPARE(result(reply).value(QStringLiteral("resultType")).toString(), QStringLiteral("complete"));
        QCOMPARE(result(reply).value(QStringLiteral("tools")).toArray().size(), 2);

        message = modern(QStringLiteral("tools/call"), {{QStringLiteral("name"), QStringLiteral("list")}});
        reply = protocol.handle(message, headersFor(message));
        QCOMPARE(result(reply).value(QStringLiteral("structuredContent")).toArray().size(), 2);  // (any JSON now)

        // The headers say what the body says.
        mcp::Headers headers = headersFor(message);
        headers.name = QStringLiteral("echo");
        reply = protocol.handle(message, headers);
        QCOMPARE(reply.status, 400);
        QCOMPARE(errorCode(reply), mcp::code::kHeaderMismatch);
        headers = headersFor(message);
        headers.name.reset();
        QCOMPARE(errorCode(protocol.handle(message, headers)), mcp::code::kHeaderMismatch);
        headers = headersFor(message);
        headers.method = QStringLiteral("tools/list");
        QCOMPARE(errorCode(protocol.handle(message, headers)), mcp::code::kHeaderMismatch);
        headers = headersFor(message);
        headers.protocolVersion.reset();
        QCOMPARE(errorCode(protocol.handle(message, headers)), mcp::code::kHeaderMismatch);
        // (Not over HTTP, as stdio would be: no headers to check.)
        QCOMPARE(protocol.handle(message).status, 200);
        // An encoded Mcp-Name is decoded first.
        headers = headersFor(message);
        headers.name = mcp::Protocol::decodeHeaderValue(QStringLiteral("=?base64?") + QStringLiteral("list").toUtf8().toBase64() + QStringLiteral("?="));
        QCOMPARE(protocol.handle(message, headers).status, 200);
        QCOMPARE(mcp::Protocol::decodeHeaderValue(QStringLiteral("plain")), QStringLiteral("plain"));

        // A version it doesn't speak: which ones it does.
        message = modern(QStringLiteral("tools/list"), {}, QStringLiteral("1900-01-01"));
        reply = protocol.handle(message, headersFor(message, QStringLiteral("1900-01-01")));
        QCOMPARE(reply.status, 400);
        QCOMPARE(errorCode(reply), mcp::code::kUnsupportedVersion);
        const QJsonObject data = reply.body->value(QStringLiteral("error")).toObject().value(QStringLiteral("data")).toObject();
        QCOMPARE(data.value(QStringLiteral("requested")).toString(), QStringLiteral("1900-01-01"));
        QVERIFY(data.value(QStringLiteral("supported")).toArray().contains(mcp::kModernVersion));
        // The modern header without the body's _meta.
        message = request(QStringLiteral("tools/list"));
        reply = protocol.handle(message, headersFor(message));
        QCOMPARE(reply.status, 400);
        // An unknown method: 404.
        message = modern(QStringLiteral("prompts/list"));
        reply = protocol.handle(message, headersFor(message));
        QCOMPARE(reply.status, 404);
        QCOMPARE(errorCode(reply), mcp::code::kMethodNotFound);
        // Not JSON-RPC.
        QCOMPARE(protocol.handle({{QStringLiteral("hello"), 1}}).status, 400);
        QCOMPARE(protocol.handle({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), 1},
                                  {QStringLiteral("result"), QJsonObject()}}).status,
                 400);
    }

    // --- The tools, on a session ---------------------------------------------------------------

    void theToolsReadTheProject() {
        SessionFixture f;
        mcp::Protocol& protocol = f.s().mcp()->protocol();
        const QString lead = f.editor().addMidiTrack(-1, QStringLiteral("Lead"));
        const auto clip = f.editor().addMidiClip(lead, 0, 4);
        QVERIFY(clip);
        f.editor().setClipNotes(*clip, {{60, 0, 1, 100}, {64, 0, 1, 90}, {67, 0, 1, 80}}, QStringLiteral("Notes"));
        const QString delay = f.editor().addDevice(lead, QStringLiteral("delay"));
        const QString ret = f.editor().addReturnTrack();
        f.editor().setSend(lead, ret, -6.0);
        f.editor().setTrackParam(lead, TrackField::Pan, -0.5);
        f.selection().selectTrack(lead, true);

        QJsonObject out = call(protocol, QStringLiteral("get_project")).value(QStringLiteral("structuredContent")).toObject();
        QCOMPARE(out.value(QStringLiteral("name")).toString(), QStringLiteral("Untitled"));
        QCOMPARE(out.value(QStringLiteral("tempo")).toDouble(), 120.0);
        QCOMPARE(out.value(QStringLiteral("timeSignature")).toString(), QStringLiteral("4/4"));
        QCOMPARE(out.value(QStringLiteral("trackCount")).toInt(), 1);
        QCOMPARE(out.value(QStringLiteral("returnCount")).toInt(), 1);
        QCOMPARE(out.value(QStringLiteral("selectedTrackId")).toString(), lead);
        QCOMPARE(out.value(QStringLiteral("endBeat")).toDouble(), 4.0);

        // Every track, the returns, the master; each says what it is.
        const QJsonArray tracks = call(protocol, QStringLiteral("list_tracks"))
                                      .value(QStringLiteral("structuredContent")).toObject()
                                      .value(QStringLiteral("tracks")).toArray();
        QCOMPARE(tracks.size(), 3);
        QCOMPARE(tracks[0].toObject().value(QStringLiteral("id")).toString(), lead);
        QCOMPARE(tracks[0].toObject().value(QStringLiteral("label")).toString(), f.s().trackLabels()->label(lead));
        QVERIFY(!tracks[0].toObject().value(QStringLiteral("label")).toString().isEmpty());
        QCOMPARE(tracks[0].toObject().value(QStringLiteral("pan")).toDouble(), -0.5);
        QCOMPARE(tracks[0].toObject().value(QStringLiteral("clipCount")).toInt(), 1);
        QCOMPARE(tracks[1].toObject().value(QStringLiteral("id")).toString(), ret);
        QCOMPARE(tracks[2].toObject().value(QStringLiteral("id")).toString(), kMaster);

        // One track in full.
        out = call(protocol, QStringLiteral("get_track"), {{QStringLiteral("track_id"), lead}})
                  .value(QStringLiteral("structuredContent")).toObject();
        const QJsonArray devices = out.value(QStringLiteral("devices")).toArray();
        QCOMPARE(devices.size(), 2);  // (its Synth, the delay)
        QCOMPARE(devices[1].toObject().value(QStringLiteral("id")).toString(), delay);
        QCOMPARE(devices[1].toObject().value(QStringLiteral("name")).toString(), QStringLiteral("Delay"));
        const QJsonObject c = out.value(QStringLiteral("clips")).toArray()[0].toObject();
        QCOMPARE(c.value(QStringLiteral("kind")).toString(), QStringLiteral("midi"));
        QCOMPARE(c.value(QStringLiteral("endBeat")).toDouble(), 4.0);
        QCOMPARE(c.value(QStringLiteral("noteCount")).toInt(), 3);
        const QJsonObject send = out.value(QStringLiteral("sends")).toArray()[0].toObject();
        QCOMPARE(send.value(QStringLiteral("returnId")).toString(), ret);
        QCOMPARE(send.value(QStringLiteral("levelDb")).toDouble(), -6.0);
        QVERIFY(!out.value(QStringLiteral("details")).toArray().isEmpty());
        QCOMPARE(out.value(QStringLiteral("output")).toObject().value(QStringLiteral("to")).toString(), QStringLiteral("group"));
        // A track that isn't there, or none asked for: errors the model can correct.
        QJsonObject failed = call(protocol, QStringLiteral("get_track"), {{QStringLiteral("track_id"), QStringLiteral("nope")}});
        QVERIFY(failed.value(QStringLiteral("isError")).toBool());
        QVERIFY(call(protocol, QStringLiteral("get_track")).value(QStringLiteral("isError")).toBool());
        QVERIFY(!call(protocol, QStringLiteral("get_track"), {{QStringLiteral("track_id"), kMaster}})
                     .value(QStringLiteral("isError")).toBool());

        // Found as the find bar finds them.
        const QJsonArray found = call(protocol, QStringLiteral("find_tracks"), {{QStringLiteral("query"), QStringLiteral("lead")}})
                                     .value(QStringLiteral("structuredContent")).toObject()
                                     .value(QStringLiteral("tracks")).toArray();
        QCOMPARE(found.size(), 1);
        QCOMPARE(found[0].toObject().value(QStringLiteral("id")).toString(), lead);
        QVERIFY(call(protocol, QStringLiteral("find_tracks")).value(QStringLiteral("isError")).toBool());

        // The chords its notes play (C E G: C), and part of the song.
        out = call(protocol, QStringLiteral("get_harmony")).value(QStringLiteral("structuredContent")).toObject();
        const QJsonArray chords = out.value(QStringLiteral("chords")).toArray();
        QVERIFY(!chords.isEmpty());
        QCOMPARE(chords[0].toObject().value(QStringLiteral("chord")).toString(), QStringLiteral("C"));
        QCOMPARE(out.value(QStringLiteral("tracks")).toArray().first().toString(), lead);
        out = call(protocol, QStringLiteral("get_harmony"), {{QStringLiteral("start_beat"), 100}})
                  .value(QStringLiteral("structuredContent")).toObject();
        QVERIFY(out.value(QStringLiteral("chords")).toArray().isEmpty());
        QVERIFY(call(protocol, QStringLiteral("get_harmony"), {{QStringLiteral("start_beat"), 4}, {QStringLiteral("end_beat"), 2}})
                    .value(QStringLiteral("isError")).toBool());
    }

    // --- Streamable HTTP ---------------------------------------------------------------------

    void overHttpOnLocalhost() {
        mcp::Protocol protocol = echoProtocol();
        mcp::HttpServer server(protocol);
        QVERIFY(server.listen(0));
        const quint16 port = server.port();
        Client client(port);

        // A legacy client: initialize, initialized, a call, on one connection (kept alive).
        auto response = client.send(post(request(QStringLiteral("initialize"),
                                                 {{QStringLiteral("protocolVersion"), QStringLiteral("2025-11-25")}}),
                                         {}, port));
        QVERIFY(response);
        QCOMPARE(response->status, 200);
        QCOMPARE(response->headers.value("content-type"), QByteArray("application/json"));
        QVERIFY(!response->headers.contains("mcp-session-id"));  // (no sessions)
        QCOMPARE(response->json().value(QStringLiteral("result")).toObject().value(QStringLiteral("protocolVersion")).toString(),
                 QStringLiteral("2025-11-25"));
        response = client.send(post({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                                     {QStringLiteral("method"), QStringLiteral("notifications/initialized")}},
                                    "MCP-Protocol-Version: 2025-11-25\r\n", port));
        QCOMPARE(response->status, 202);
        QVERIFY(response->body.isEmpty());

        // A modern call with its headers.
        const QJsonObject message = modern(QStringLiteral("tools/call"),
                                           {{QStringLiteral("name"), QStringLiteral("echo")},
                                            {QStringLiteral("arguments"), QJsonObject{{QStringLiteral("x"), 2}}}});
        response = client.send(post(message, "MCP-Protocol-Version: 2026-07-28\r\nMcp-Method: tools/call\r\nMcp-Name: echo\r\n", port));
        QCOMPARE(response->status, 200);
        QCOMPARE(response->json().value(QStringLiteral("result")).toObject().value(QStringLiteral("structuredContent"))
                     .toObject().value(QStringLiteral("x")).toInt(),
                 2);
        response = client.send(post(message, "MCP-Protocol-Version: 2026-07-28\r\nMcp-Method: tools/call\r\nMcp-Name: list\r\n", port));
        QCOMPARE(response->status, 400);
        QCOMPARE(response->json().value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toInt(),
                 mcp::code::kHeaderMismatch);

        // A chunked body.
        const QByteArray body = QJsonDocument(request(QStringLiteral("tools/list"))).toJson(QJsonDocument::Compact);
        const QByteArray half = body.left(body.size() / 2), rest = body.mid(body.size() / 2);
        response = client.send("POST /mcp HTTP/1.1\r\nHost: localhost:" + QByteArray::number(port) +
                               "\r\nContent-Type: application/json\r\nTransfer-Encoding: chunked\r\n\r\n" +
                               QByteArray::number(half.size(), 16) + "\r\n" + half + "\r\n" +
                               QByteArray::number(rest.size(), 16) + "\r\n" + rest + "\r\n0\r\n\r\n");
        QCOMPARE(response->status, 200);
        QCOMPARE(response->json().value(QStringLiteral("result")).toObject().value(QStringLiteral("tools")).toArray().size(), 2);

        // Not JSON; a batch; another type; GET (no streams); another path.
        response = client.send("POST /mcp HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: application/json\r\nContent-Length: 3\r\n\r\n{x}");
        QCOMPARE(response->status, 400);
        QCOMPARE(response->json().value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toInt(), mcp::code::kParse);
        response = client.send("POST /mcp HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: application/json\r\nContent-Length: 2\r\n\r\n[]");
        QCOMPARE(response->status, 400);
        response = client.send("POST /mcp HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: text/plain\r\nContent-Length: 2\r\n\r\n{}");
        QCOMPARE(response->status, 415);
        response = client.send("GET /mcp HTTP/1.1\r\nHost: 127.0.0.1\r\nAccept: text/event-stream\r\n\r\n");
        QCOMPARE(response->status, 405);
        QCOMPARE(response->headers.value("allow"), QByteArray("POST"));
        response = client.send("POST /other HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: application/json\r\nContent-Length: 2\r\n\r\n{}");
        QCOMPARE(response->status, 404);

        // DNS rebinding: a web page's Origin, or a Host that isn't this computer's name.
        response = client.send(post(request(QStringLiteral("tools/list")), "Origin: https://evil.example\r\n", port));
        QCOMPARE(response->status, 403);
        response = client.send(post(request(QStringLiteral("tools/list")), "Origin: http://localhost:5173\r\n", port));
        QCOMPARE(response->status, 200);
        response = client.send("POST /mcp HTTP/1.1\r\nHost: evil.example:" + QByteArray::number(port) +
                               "\r\nContent-Type: application/json\r\nContent-Length: 2\r\n\r\n{}");
        QCOMPARE(response->status, 403);

        // Connection: close.
        response = client.send("POST /mcp HTTP/1.1\r\nHost: [::1]:" + QByteArray::number(port) +
                               "\r\nConnection: close\r\nContent-Type: application/json\r\nContent-Length: " +
                               QByteArray::number(body.size()) + "\r\n\r\n" + body);
        QCOMPARE(response->status, 200);
        QVERIFY(client.closed());

        QVERIFY(mcp::HttpServer::isLocalHost(QStringLiteral("localhost")));
        QVERIFY(mcp::HttpServer::isLocalHost(QStringLiteral("[::1]:80")));
        QVERIFY(!mcp::HttpServer::isLocalHost(QStringLiteral("127.0.0.1.evil.example")));
        QVERIFY(!mcp::HttpServer::isLocalOrigin(QStringLiteral("null")));
    }

    // --- On and off ----------------------------------------------------------------------------

    void turnedOnAndOffItsPortKept() {
        QTcpServer probe;  // (a free port)
        QVERIFY(probe.listen(QHostAddress::LocalHost));
        const int free = probe.serverPort();
        probe.close();

        SessionFixture f;
        McpServer& server = *f.s().mcp();
        QVERIFY(!server.enabled());  // off at first
        QVERIFY(!server.running());
        QSignalSpy changed(&server, &McpServer::changed);
        server.setPort(free);
        QCOMPARE(QSettings().value(McpServer::kPortKey).toInt(), free);
        QCOMPARE(server.url(), QStringLiteral("http://127.0.0.1:%1/mcp").arg(free));
        QVERIFY(server.claudeCodeCommand().endsWith(server.url()));
        server.setEnabled(true);
        QVERIFY(server.running());
        QVERIFY(QSettings().value(McpServer::kEnabledKey).toBool());
        QVERIFY(f.lastMessage().contains(server.url()));  // (said in the status line)
        {
            Client client(static_cast<quint16>(free));
            const auto response = client.send(post(request(QStringLiteral("tools/list")), {}, static_cast<quint16>(free)));
            QVERIFY(response);
            QCOMPARE(response->json().value(QStringLiteral("result")).toObject().value(QStringLiteral("tools")).toArray().size(), 5);
        }

        // A session made now starts it as the settings say; a port taken says why.
        {
            SessionFixture again;
            QVERIFY(again.s().mcp()->enabled());
            QVERIFY(!again.s().mcp()->running());  // (the first one has the port)
            QVERIFY(!again.s().mcp()->error().isEmpty());
        }
        server.setEnabled(false);
        QVERIFY(!server.running());
        QCOMPARE(f.lastMessage(), QStringLiteral("MCP server off"));
        QVERIFY(changed.count() >= 3);
        QSettings().remove(McpServer::kEnabledKey);
    }
};

QTEST_GUILESS_MAIN(TestMcp)
#include "test_mcp.moc"
