// The MCP server (Session.mcp): the protocol in both eras (2026-07-28's
// per-request versions and headers, the legacy initialize handshake), its
// errors; the tools on a real session (the project, tracks with their labels,
// one track in full, finding tracks, the harmony, a device's parameters, the
// notes); agents' edits (parameters, the mixer, clips, notes where they are,
// automation, tracks, devices), each a change apart from the undo stack, rewound and
// restored, and the two histories refusing to undo each other's work; Streamable HTTP over a real
// socket on localhost (DNS rebinding refused, keep-alive, chunked bodies); and
// turning the server on and off, its port kept.

#include "SessionFixture.h"
#include "TestSupport.h"

#include "editor/AgentHistory.h"
#include "intelligence/TrackLabels.h"
#include "mcp/McpArrangement.h"
#include "mcp/McpHttpServer.h"
#include "mcp/McpProtocol.h"
#include "mcp/McpServer.h"
#include "model/Automation.h"
#include "model/Notes.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QUndoCommand>

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
// A tool call's structured content.
QJsonObject structured(const QJsonObject& called) { return called.value(QStringLiteral("structuredContent")).toObject(); }
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
        // (Its text is compact JSON: indenting would cost the model tokens.)
        QCOMPARE(called.value(QStringLiteral("content")).toArray()[0].toObject().value(QStringLiteral("text")).toString(),
                 QStringLiteral("{\"a\":1}"));
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
        const QString lead = f.editor().addMidiTrack(-1, QStringLiteral("Lead"), QStringLiteral("synth"));
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
        // In bars too, and what the user did last, the latest first.
        QCOMPARE(out.value(QStringLiteral("beatsPerBar")).toDouble(), 4.0);
        QCOMPARE(out.value(QStringLiteral("endBar")).toDouble(), 2.0);
        const QJsonArray recent = out.value(QStringLiteral("recentEdits")).toArray();
        QVERIFY(!recent.isEmpty());
        QCOMPARE(recent[0].toString(), f.stack().text(f.stack().index() - 1));

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
        // What is as usual is left out.
        QVERIFY(!tracks[0].toObject().contains(QStringLiteral("mute")));
        QVERIFY(!tracks[0].toObject().contains(QStringLiteral("parent")));
        QVERIFY(!tracks[1].toObject().contains(QStringLiteral("pan")));
        QVERIFY(!tracks[1].toObject().contains(QStringLiteral("solo")));
        f.editor().setTrackParam(lead, TrackField::Mute, 1.0);
        QVERIFY(structured(call(protocol, QStringLiteral("list_tracks"))).value(QStringLiteral("tracks")).toArray()[0]
                    .toObject().value(QStringLiteral("mute")).toBool());
        f.editor().setTrackParam(lead, TrackField::Mute, 0.0);

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
        QCOMPARE(c.value(QStringLiteral("startBar")).toDouble(), 1.0);
        QCOMPARE(c.value(QStringLiteral("endBar")).toDouble(), 2.0);
        QCOMPARE(c.value(QStringLiteral("noteCount")).toInt(), 3);
        QVERIFY(!out.contains(QStringLiteral("automation")));  // (none yet)
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

        // Found as the find bar finds them: list_tracks with a query.
        const QJsonArray found = structured(call(protocol, QStringLiteral("list_tracks"), {{QStringLiteral("query"), QStringLiteral("lead")}}))
                                     .value(QStringLiteral("tracks")).toArray();
        QCOMPARE(found.size(), 1);
        QCOMPARE(found[0].toObject().value(QStringLiteral("id")).toString(), lead);
        QCOMPARE(found[0].toObject().value(QStringLiteral("label")).toString(), f.s().trackLabels()->label(lead));
        QVERIFY(structured(call(protocol, QStringLiteral("list_tracks"), {{QStringLiteral("query"), QStringLiteral("trombone")}}))
                    .value(QStringLiteral("tracks")).toArray().isEmpty());
        QCOMPARE(structured(call(protocol, QStringLiteral("list_tracks"), {{QStringLiteral("query"), QStringLiteral(" ")}}))
                     .value(QStringLiteral("tracks")).toArray().size(),
                 3);  // (no words: every track)

        // The chords its notes play (C E G: C), and part of the song.
        out = call(protocol, QStringLiteral("get_harmony")).value(QStringLiteral("structuredContent")).toObject();
        const QJsonArray chords = out.value(QStringLiteral("chords")).toArray();
        QVERIFY(!chords.isEmpty());
        QCOMPARE(chords[0].toObject().value(QStringLiteral("chord")).toString(), QStringLiteral("C"));
        QVERIFY(!out.contains(QStringLiteral("tracks")));  // (only asked for)
        out = structured(call(protocol, QStringLiteral("get_harmony"), {{QStringLiteral("include_tracks"), true}}));
        const QJsonObject heard = out.value(QStringLiteral("tracks")).toArray().first().toObject();
        QCOMPARE(heard.value(QStringLiteral("id")).toString(), lead);
        QCOMPARE(heard.value(QStringLiteral("name")).toString(), f.project().track(lead).name);
        out = call(protocol, QStringLiteral("get_harmony"), {{QStringLiteral("start_beat"), 100}})
                  .value(QStringLiteral("structuredContent")).toObject();
        QVERIFY(out.value(QStringLiteral("chords")).toArray().isEmpty());
        QVERIFY(call(protocol, QStringLiteral("get_harmony"), {{QStringLiteral("start_beat"), 4}, {QStringLiteral("end_beat"), 2}})
                    .value(QStringLiteral("isError")).toBool());
        // In bars: bar 1 is beats 0 to 4 (start_bar 1, end_bar 2: where bar 2 starts).
        out = structured(call(protocol, QStringLiteral("get_harmony"), {{QStringLiteral("start_bar"), 1}, {QStringLiteral("end_bar"), 2}}));
        QCOMPARE(out.value(QStringLiteral("chords")).toArray()[0].toObject().value(QStringLiteral("startBar")).toDouble(), 1.0);
        QVERIFY(structured(call(protocol, QStringLiteral("get_harmony"), {{QStringLiteral("start_bar"), 2}}))
                    .value(QStringLiteral("chords")).toArray().isEmpty());
        QVERIFY(call(protocol, QStringLiteral("get_harmony"), {{QStringLiteral("start_bar"), 1}, {QStringLiteral("start_beat"), 0}})
                    .value(QStringLiteral("isError")).toBool());  // (one or the other)
        QVERIFY(call(protocol, QStringLiteral("get_harmony"), {{QStringLiteral("start_bar"), 0}})
                    .value(QStringLiteral("isError")).toBool());  // (bars count from 1)

        // Automation: listed on the track, its points in the target's units (a fader's: dB).
        f.editor().setEnvelope(lead, automation::kMixerVolume, {{0.0, 0.0, 0.0}, {4.0, 1.0, 0.5}});
        const QJsonArray automated = structured(call(protocol, QStringLiteral("get_track"), {{QStringLiteral("track_id"), lead}}))
                                         .value(QStringLiteral("automation")).toArray();
        QCOMPARE(automated.size(), 1);
        QCOMPARE(automated[0].toObject().value(QStringLiteral("key")).toString(), automation::kMixerVolume);
        QCOMPARE(automated[0].toObject().value(QStringLiteral("points")).toInt(), 2);
        out = structured(call(protocol, QStringLiteral("get_automation"), {{QStringLiteral("track_id"), lead}}));
        const QJsonObject envelope = out.value(QStringLiteral("envelopes")).toArray()[0].toObject();
        QCOMPARE(envelope.value(QStringLiteral("key")).toString(), automation::kMixerVolume);
        QCOMPARE(envelope.value(QStringLiteral("unit")).toString(), QStringLiteral("dB"));
        const QJsonArray points = envelope.value(QStringLiteral("points")).toArray();
        QCOMPARE(points.size(), 2);
        QCOMPARE(points[1].toArray()[0].toDouble(), 4.0);
        QCOMPARE(points[1].toArray()[1].toDouble(), automation::kMaxVolumeDb);
        QCOMPARE(points[1].toArray()[2].toDouble(), 0.5);
        QCOMPARE(points[0].toArray().size(), 2);  // (straight: no curve)
        QCOMPARE(structured(call(protocol, QStringLiteral("get_automation"),
                                 {{QStringLiteral("track_id"), lead}, {QStringLiteral("start_beat"), 1}}))
                     .value(QStringLiteral("envelopes")).toArray()[0].toObject().value(QStringLiteral("points")).toArray().size(),
                 1);
        QVERIFY(call(protocol, QStringLiteral("get_automation"),
                     {{QStringLiteral("track_id"), lead}, {QStringLiteral("key"), automation::kMixerPan}})
                    .value(QStringLiteral("isError")).toBool());  // (not automated)
    }

    void theToolsReadParametersAndNotes() {
        SessionFixture f;
        mcp::Protocol& protocol = f.s().mcp()->protocol();
        const QString lead = f.editor().addMidiTrack(-1, QStringLiteral("Lead"), QStringLiteral("synth"));
        const QString delay = f.editor().addDevice(lead, QStringLiteral("delay"));
        const auto clip = f.editor().addMidiClip(lead, 4, 4);
        QVERIFY(clip);
        f.editor().setClipNotes(*clip, {{60, 0, 1, 100}, {67, 2, 1, 90}}, QStringLiteral("Notes"));

        // Every parameter: its value as it is, in its units and as the device shows it.
        QJsonObject out = structured(call(protocol, QStringLiteral("get_device_parameters"),
                                          {{QStringLiteral("track_id"), lead}, {QStringLiteral("device_id"), delay}}));
        const QJsonArray params = out.value(QStringLiteral("parameters")).toArray();
        const int total = out.value(QStringLiteral("total")).toInt();
        QVERIFY(total > 2);
        QCOMPARE(params.size(), total);
        QCOMPARE(params[0].toObject().value(QStringLiteral("id")).toString(), automation::kDeviceOn);
        const QJsonObject first = params[1].toObject();
        QVERIFY(first.contains(QStringLiteral("value")));
        QVERIFY(!first.value(QStringLiteral("text")).toString().isEmpty());
        QVERIFY(first.value(QStringLiteral("max")).toDouble() > first.value(QStringLiteral("min")).toDouble());
        // Found by words of their names, a page at a time.
        const QString name = first.value(QStringLiteral("name")).toString();
        out = structured(call(protocol, QStringLiteral("get_device_parameters"),
                              {{QStringLiteral("track_id"), lead}, {QStringLiteral("device_id"), delay},
                               {QStringLiteral("query"), name.toUpper()}}));
        QVERIFY(out.value(QStringLiteral("total")).toInt() >= 1);
        QCOMPARE(out.value(QStringLiteral("parameters")).toArray()[0].toObject().value(QStringLiteral("name")).toString(), name);
        out = structured(call(protocol, QStringLiteral("get_device_parameters"),
                              {{QStringLiteral("track_id"), lead}, {QStringLiteral("device_id"), delay},
                               {QStringLiteral("limit"), 1}, {QStringLiteral("offset"), 1}}));
        QCOMPARE(out.value(QStringLiteral("parameters")).toArray().size(), 1);
        QCOMPARE(out.value(QStringLiteral("nextOffset")).toInt(), 2);
        QVERIFY(call(protocol, QStringLiteral("get_device_parameters"),
                     {{QStringLiteral("track_id"), lead}, {QStringLiteral("device_id"), QStringLiteral("nope")}})
                    .value(QStringLiteral("isError")).toBool());

        // The notes, on the song's timeline: as rows ([start, length, pitch, velocity]), or objects.
        out = structured(call(protocol, QStringLiteral("get_notes"), {{QStringLiteral("track_id"), lead}}));
        QCOMPARE(out.value(QStringLiteral("fields")).toArray(),
                 (QJsonArray{QStringLiteral("startBeat"), QStringLiteral("length"), QStringLiteral("pitch"), QStringLiteral("velocity")}));
        const QJsonArray rows = out.value(QStringLiteral("notes")).toArray();
        QCOMPARE(rows.size(), 2);
        QCOMPARE(rows[1].toArray(), (QJsonArray{6.0, 1.0, 67, 90}));
        QVERIFY(!out.contains(QStringLiteral("muted")));
        out = structured(call(protocol, QStringLiteral("get_notes"),
                              {{QStringLiteral("track_id"), lead}, {QStringLiteral("format"), QStringLiteral("objects")}}));
        const QJsonArray notes = out.value(QStringLiteral("notes")).toArray();
        QCOMPARE(notes[1].toObject().value(QStringLiteral("startBeat")).toDouble(), 6.0);
        QCOMPARE(notes[1].toObject().value(QStringLiteral("name")).toString(), QStringLiteral("G3"));
        QVERIFY(call(protocol, QStringLiteral("get_notes"),
                     {{QStringLiteral("track_id"), lead}, {QStringLiteral("format"), QStringLiteral("xml")}})
                    .value(QStringLiteral("isError")).toBool());
        out = structured(call(protocol, QStringLiteral("get_notes"),
                              {{QStringLiteral("track_id"), lead}, {QStringLiteral("start_beat"), 5}}));
        QCOMPARE(out.value(QStringLiteral("notes")).toArray().size(), 1);
        // A muted note: its row is listed as muted.
        std::vector<Note> withMuted = f.project().clip(lead, clip->clipId).notes;
        withMuted[0].muted = true;
        f.editor().setClipNotes(*clip, withMuted, QStringLiteral("Mute"));
        out = structured(call(protocol, QStringLiteral("get_notes"), {{QStringLiteral("track_id"), lead}}));
        QCOMPARE(out.value(QStringLiteral("muted")).toArray(), QJsonArray{0});
        QCOMPARE(out.value(QStringLiteral("clips")).toArray()[0].toObject().value(QStringLiteral("startBar")).toDouble(), 2.0);
        // Pitches as names; a stretch in bars (bar 2 is beats 4 to 8).
        out = structured(call(protocol, QStringLiteral("get_notes"),
                              {{QStringLiteral("track_id"), lead}, {QStringLiteral("format"), QStringLiteral("names")},
                               {QStringLiteral("start_bar"), 2}, {QStringLiteral("end_bar"), 3}}));
        QCOMPARE(out.value(QStringLiteral("notes")).toArray()[1].toArray(), (QJsonArray{6.0, 1.0, QStringLiteral("G3"), 90}));
        QCOMPARE(structured(call(protocol, QStringLiteral("get_notes"),
                                 {{QStringLiteral("track_id"), lead}, {QStringLiteral("end_bar"), 2}}))
                     .value(QStringLiteral("notes")).toArray().size(),
                 0);
        // A bent note: its row is listed as bent; as an object, its bend and vibrato.
        withMuted[1].bend = {{0.0, 0.0, 0.0}, {0.5, 2.0, 0.25}};
        withMuted[1].vibrato = {{0.25, 0.5, 0.5, 6.0, 0.3}};
        f.editor().setClipNotes(*clip, withMuted, QStringLiteral("Bend"));
        out = structured(call(protocol, QStringLiteral("get_notes"), {{QStringLiteral("track_id"), lead}}));
        QCOMPARE(out.value(QStringLiteral("bent")).toArray(), QJsonArray{1});
        QVERIFY(out.contains(QStringLiteral("bendNote")));
        out = structured(call(protocol, QStringLiteral("get_notes"),
                              {{QStringLiteral("track_id"), lead}, {QStringLiteral("format"), QStringLiteral("objects")}}));
        const QJsonObject bentNote = out.value(QStringLiteral("notes")).toArray()[1].toObject();
        QCOMPARE(bentNote.value(QStringLiteral("bend")).toArray()[1].toArray(), (QJsonArray{0.5, 2.0, 0.25}));
        QCOMPARE(bentNote.value(QStringLiteral("vibrato")).toArray()[0].toObject().value(QStringLiteral("rate")).toDouble(), 6.0);
        QVERIFY(out.value(QStringLiteral("notes")).toArray()[0].toObject().value(QStringLiteral("muted")).toBool());

        // Nothing selected in time: the selection's context says so.
        QVERIFY(call(protocol, QStringLiteral("get_selection_context")).value(QStringLiteral("isError")).toBool());

        // The user's time selection, for a part written there.
        f.selection().setTimeRange(4, 8, {lead}, QSet<ClipRef>{*clip});
        out = structured(call(protocol, QStringLiteral("get_project")));
        const QJsonObject range = out.value(QStringLiteral("timeSelection")).toObject();
        QCOMPARE(range.value(QStringLiteral("startBeat")).toDouble(), 4.0);
        QCOMPARE(range.value(QStringLiteral("endBeat")).toDouble(), 8.0);
        QCOMPARE(range.value(QStringLiteral("trackIds")).toArray()[0].toString(), lead);
        QCOMPARE(range.value(QStringLiteral("clips")).toArray()[0].toObject().value(QStringLiteral("clipId")).toString(),
                 clip->clipId);

        // ...and in one call, all a part written there needs: each track in it
        // with its notes there (a group's tracks, not the group), the chords, the tempo.
        const QString pad = f.editor().addMidiTrack(-1, QStringLiteral("Pad"), QStringLiteral("synth"));
        const QString group = f.editor().groupTracks({pad});
        f.selection().setTimeRange(4, 8, {lead, group, pad}, QSet<ClipRef>{*clip});
        out = structured(call(protocol, QStringLiteral("get_selection_context")));
        QCOMPARE(out.value(QStringLiteral("startBeat")).toDouble(), 4.0);
        QCOMPARE(out.value(QStringLiteral("endBeat")).toDouble(), 8.0);
        QCOMPARE(out.value(QStringLiteral("startBar")).toDouble(), 2.0);
        QCOMPARE(out.value(QStringLiteral("endBar")).toDouble(), 3.0);
        QCOMPARE(out.value(QStringLiteral("selectedClips")).toArray().size(), 1);
        QCOMPARE(out.value(QStringLiteral("tempo")).toDouble(), 120.0);
        const QJsonArray context = out.value(QStringLiteral("tracks")).toArray();
        QCOMPARE(context.size(), 2);
        const QJsonObject leadContext = context[0].toObject();
        QCOMPARE(leadContext.value(QStringLiteral("id")).toString(), lead);
        QVERIFY(!leadContext.value(QStringLiteral("details")).toArray().isEmpty());
        QCOMPARE(leadContext.value(QStringLiteral("notes")).toArray().size(), 2);
        QCOMPARE(leadContext.value(QStringLiteral("muted")).toArray(), QJsonArray{0});
        QCOMPARE(context[1].toObject().value(QStringLiteral("id")).toString(), pad);
        QVERIFY(context[1].toObject().value(QStringLiteral("notes")).toArray().isEmpty());
        QVERIFY(out.value(QStringLiteral("harmony")).toObject().contains(QStringLiteral("chords")));
        out = structured(call(protocol, QStringLiteral("get_selection_context"), {{QStringLiteral("format"), QStringLiteral("names")}}));
        QCOMPARE(out.value(QStringLiteral("tracks")).toArray()[0].toObject().value(QStringLiteral("notes")).toArray()[0].toArray()[2].toString(),
                 QStringLiteral("C3"));
        QVERIFY(call(protocol, QStringLiteral("get_selection_context"), {{QStringLiteral("format"), QStringLiteral("objects")}})
                    .value(QStringLiteral("isError")).toBool());
    }

    // --- Edits: the agents' history ----------------------------------------------------------

    void agentEditsAreChangesApartFromUndo() {
        SessionFixture f;
        mcp::Protocol& protocol = f.s().mcp()->protocol();
        AgentHistory& history = *f.s().agentChanges();
        const QString lead = f.editor().addMidiTrack(-1, QStringLiteral("Lead"), QStringLiteral("synth"));
        const QString delay = f.editor().addDevice(lead, QStringLiteral("delay"));
        const QJsonObject param = structured(call(protocol, QStringLiteral("get_device_parameters"),
                                                  {{QStringLiteral("track_id"), lead}, {QStringLiteral("device_id"), delay}}))
                                      .value(QStringLiteral("parameters")).toArray()[1].toObject();
        const QString id = param.value(QStringLiteral("id")).toString();
        const double was = param.value(QStringLiteral("value")).toDouble();
        // (The end of its range it isn't at.)
        const double target = param.value(QStringLiteral("max")).toDouble() == was ? param.value(QStringLiteral("min")).toDouble()
                                                                                 : param.value(QStringLiteral("max")).toDouble();
        const int undoSteps = f.stack().count();
        f.s().undoStack()->setClean();

        // A parameter set: the model has it, the user's undo stack doesn't.
        QJsonObject out = structured(call(protocol, QStringLiteral("set_device_parameters"),
                                          {{QStringLiteral("track_id"), lead}, {QStringLiteral("device_id"), delay},
                                           {QStringLiteral("parameters"), QJsonArray{QJsonObject{{QStringLiteral("id"), id},
                                                                                                {QStringLiteral("value"), target}}}}}));
        const int first = out.value(QStringLiteral("changeId")).toInt();
        QCOMPARE(first, 1);
        QCOMPARE(f.project().device(lead, delay).params.value(id), target);
        QCOMPARE(f.stack().count(), undoSteps);
        QCOMPARE(history.count(), 1);
        QVERIFY(history.changes()[0].text.contains(QStringLiteral("Lead")));
        QCOMPARE(history.changes()[0].tracks, QStringList{lead});
        QVERIFY(!f.s().clean());  // (unsaved: an agent changed it)
        QVERIFY(f.lastMessage().startsWith(QStringLiteral("Agent: ")));
        // Out of range, or not there: errors the model can correct, nothing changed.
        QVERIFY(call(protocol, QStringLiteral("set_device_parameters"),
                     {{QStringLiteral("track_id"), lead}, {QStringLiteral("device_id"), delay},
                      {QStringLiteral("parameters"), QJsonArray{QJsonObject{{QStringLiteral("id"), id},
                                                                            {QStringLiteral("value"), 1e9}}}}})
                    .value(QStringLiteral("isError")).toBool());
        QVERIFY(call(protocol, QStringLiteral("set_device_parameters"),
                     {{QStringLiteral("track_id"), lead}, {QStringLiteral("device_id"), delay},
                      {QStringLiteral("parameters"), QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("nope")},
                                                                            {QStringLiteral("value"), 1}}}}})
                    .value(QStringLiteral("isError")).toBool());
        QCOMPARE(history.count(), 1);
        // The same again changes nothing: no change.
        out = structured(call(protocol, QStringLiteral("set_device_parameters"),
                              {{QStringLiteral("track_id"), lead}, {QStringLiteral("device_id"), delay},
                               {QStringLiteral("parameters"), QJsonArray{QJsonObject{{QStringLiteral("id"), id},
                                                                                     {QStringLiteral("value"), target}}}}}));
        QVERIFY(out.value(QStringLiteral("changeId")).isNull());
        QCOMPARE(history.count(), 1);

        // The mixer, and a part written over a stretch, on a copy of the track.
        out = structured(call(protocol, QStringLiteral("set_track"),
                              {{QStringLiteral("track_id"), lead}, {QStringLiteral("volume_db"), -6}, {QStringLiteral("pan"), 0.25}}));
        QCOMPARE(out.value(QStringLiteral("changeId")).toInt(), 2);
        QCOMPARE(f.project().track(lead).volumeDb, -6.0);
        QCOMPARE(f.project().track(lead).pan, 0.25);
        out = structured(call(protocol, QStringLiteral("duplicate_track"), {{QStringLiteral("track_id"), lead}}));
        const QString copy = out.value(QStringLiteral("trackId")).toString();
        QVERIFY(f.project().hasTrack(copy));
        // (A row, as get_notes gives them, and an object.)
        const QJsonArray notes{QJsonArray{8.5, 1, 64, 90},
                               QJsonObject{{QStringLiteral("pitch"), 60}, {QStringLiteral("start_beat"), 8},
                                           {QStringLiteral("length"), 2}}};
        out = structured(call(protocol, QStringLiteral("write_midi_clip"),
                              {{QStringLiteral("track_id"), copy}, {QStringLiteral("start_beat"), 8},
                               {QStringLiteral("end_beat"), 12}, {QStringLiteral("notes"), notes}}));
        const Clip* written = f.project().findClip(copy, out.value(QStringLiteral("clipId")).toString());
        QVERIFY(written);
        QCOMPARE(written->startBeat, 8.0);
        QCOMPARE(written->notes.size(), size_t{2});
        QCOMPARE(written->notes[0].pitch, 60);  // (sorted, in the clip's own beats)
        QCOMPARE(written->notes[1].start, 0.5);
        QCOMPARE(written->notes[1].velocity, 90);
        QVERIFY(call(protocol, QStringLiteral("write_midi_clip"),
                     {{QStringLiteral("track_id"), copy}, {QStringLiteral("start_beat"), 8}, {QStringLiteral("end_beat"), 12},
                      {QStringLiteral("notes"), QJsonArray{QJsonObject{{QStringLiteral("pitch"), 60},
                                                                       {QStringLiteral("start_beat"), 20},
                                                                       {QStringLiteral("length"), 1}}}}})
                    .value(QStringLiteral("isError")).toBool());  // (a note outside the clip)
        QVERIFY(call(protocol, QStringLiteral("write_midi_clip"),
                     {{QStringLiteral("track_id"), copy}, {QStringLiteral("start_beat"), 8}, {QStringLiteral("end_beat"), 12},
                      {QStringLiteral("notes"), QJsonArray{QJsonArray{8, 1}}}})
                    .value(QStringLiteral("isError")).toBool());  // (a row without its pitch)
        QCOMPARE(f.stack().count(), undoSteps);
        QCOMPARE(history.count(), 4);

        // In bars, pitches by name, muted notes kept, bends and vibrato as objects carry them.
        const QJsonArray kept{QJsonArray{16, 1, QStringLiteral("E3"), 90}, QJsonArray{17, 1, QStringLiteral("Bb2")},
                              QJsonObject{{QStringLiteral("pitch"), QStringLiteral("C4")}, {QStringLiteral("start_beat"), 18},
                                          {QStringLiteral("length"), 1},
                                          {QStringLiteral("bend"), QJsonArray{QJsonArray{0, 0}, QJsonArray{1, -2, 0.5}}},
                                          {QStringLiteral("vibrato"), QJsonArray{QJsonObject{{QStringLiteral("start"), 0.5},
                                                                                             {QStringLiteral("length"), 0.5}}}}}};
        out = structured(call(protocol, QStringLiteral("write_midi_clip"),
                              {{QStringLiteral("track_id"), copy}, {QStringLiteral("start_bar"), 5}, {QStringLiteral("end_bar"), 6},
                               {QStringLiteral("notes"), kept}, {QStringLiteral("muted"), QJsonArray{1}}}));
        QCOMPARE(out.value(QStringLiteral("replacedNotes")).toInt(), 0);
        QVERIFY(!out.contains(QStringLiteral("warning")));
        QCOMPARE(history.changes().back().text, QStringLiteral("Write 3 notes on %1, bar 5").arg(f.project().track(copy).name));
        const Clip* bars = f.project().findClip(copy, out.value(QStringLiteral("clipId")).toString());
        QVERIFY(bars);
        QCOMPARE(bars->startBeat, 16.0);
        QCOMPARE(bars->durationBeats, 4.0);
        QCOMPARE(bars->notes[0].pitch, 64);
        QCOMPARE(bars->notes[1].pitch, 58);
        QVERIFY(bars->notes[1].muted);
        QCOMPARE(bars->notes[2].pitch, 72);
        QCOMPARE(bars->notes[2].bend.size(), size_t{2});
        QCOMPARE(bars->notes[2].bend[1].semitones, -2.0);
        QCOMPARE(bars->notes[2].vibrato.size(), size_t{1});
        QCOMPARE(bars->notes[2].vibrato[0].rate, notes::kDefaultVibratoRate);
        const std::vector<Note> barNotes = bars->notes;  // (the clip goes when written over)
        // Read back and written again as objects, nothing is lost.
        const QJsonObject read = structured(call(protocol, QStringLiteral("get_notes"),
                                                 {{QStringLiteral("track_id"), copy}, {QStringLiteral("start_bar"), 5},
                                                  {QStringLiteral("end_bar"), 6}, {QStringLiteral("format"), QStringLiteral("objects")}}));
        QJsonArray again;
        for (const QJsonValue& value : read.value(QStringLiteral("notes")).toArray()) {
            QJsonObject n = value.toObject();
            n.insert(QStringLiteral("start_beat"), n.take(QStringLiteral("startBeat")));
            n.remove(QStringLiteral("name"));
            again.append(n);
        }
        out = structured(call(protocol, QStringLiteral("write_midi_clip"),
                              {{QStringLiteral("track_id"), copy}, {QStringLiteral("start_beat"), 16}, {QStringLiteral("end_beat"), 20},
                               {QStringLiteral("notes"), again}}));
        QCOMPARE(out.value(QStringLiteral("replacedNotes")).toInt(), 3);
        QVERIFY(!out.contains(QStringLiteral("warning")));
        const Clip* rewritten = f.project().findClip(copy, out.value(QStringLiteral("clipId")).toString());
        QVERIFY(rewritten);
        QCOMPARE(rewritten->notes, barNotes);
        // Written over as rows, the bend would go: it says so.
        out = structured(call(protocol, QStringLiteral("write_midi_clip"),
                              {{QStringLiteral("track_id"), copy}, {QStringLiteral("start_bar"), 5}, {QStringLiteral("end_bar"), 6},
                               {QStringLiteral("notes"), QJsonArray{QJsonArray{18, 1, 72}}}}));
        QVERIFY(out.value(QStringLiteral("warning")).toString().contains(QStringLiteral("bends")));
        // Wrong: a bad name, a bend outside its note, a muted note that isn't there.
        for (const QJsonObject& wrong :
             {QJsonObject{{QStringLiteral("notes"), QJsonArray{QJsonArray{16, 1, QStringLiteral("H3")}}}},
              QJsonObject{{QStringLiteral("notes"), QJsonArray{QJsonObject{{QStringLiteral("pitch"), 60}, {QStringLiteral("start_beat"), 16},
                                                                          {QStringLiteral("length"), 1},
                                                                          {QStringLiteral("bend"), QJsonArray{QJsonArray{2, 1}}}}}}},
              QJsonObject{{QStringLiteral("notes"), QJsonArray{QJsonArray{16, 1, 60}}}, {QStringLiteral("muted"), QJsonArray{3}}}}) {
            QJsonObject arguments = wrong;
            arguments.insert(QStringLiteral("track_id"), copy);
            arguments.insert(QStringLiteral("start_bar"), 5);
            arguments.insert(QStringLiteral("end_bar"), 6);
            QVERIFY(call(protocol, QStringLiteral("write_midi_clip"), arguments).value(QStringLiteral("isError")).toBool());
        }
        QVERIFY(call(protocol, QStringLiteral("write_midi_clip"),
                     {{QStringLiteral("track_id"), copy}, {QStringLiteral("start_bar"), 5},
                      {QStringLiteral("notes"), QJsonArray{QJsonArray{16, 1, 60}}}})
                    .value(QStringLiteral("isError")).toBool());  // (no end)
        QVERIFY(history.rewindFrom(history.changes()[4].id).isEmpty());  // (as before these)
        QCOMPARE(history.count(), 7);

        // Listed; rewound on its own, out of order, and restored.
        const QJsonArray listed = structured(call(protocol, QStringLiteral("list_agent_changes")))
                                      .value(QStringLiteral("changes")).toArray();
        QCOMPARE(listed.size(), 7);
        QCOMPARE(listed[1].toObject().value(QStringLiteral("tool")).toString(), QStringLiteral("set_track"));
        out = structured(call(protocol, QStringLiteral("rewind_agent_changes"), {{QStringLiteral("change_ids"), QJsonArray{first}}}));
        QCOMPARE(out.value(QStringLiteral("rewound")).toArray().size(), 1);
        QCOMPARE(f.project().device(lead, delay).params.value(id, was), was);
        QCOMPARE(f.project().track(lead).volumeDb, -6.0);  // (the later change stays)
        QVERIFY(!history.find(first)->applied);
        QVERIFY(call(protocol, QStringLiteral("rewind_agent_changes"), {{QStringLiteral("all"), true}, {QStringLiteral("restore"), true}})
                    .value(QStringLiteral("isError")).toBool());  // (restoring names the changes)
        out = structured(call(protocol, QStringLiteral("rewind_agent_changes"),
                              {{QStringLiteral("change_ids"), QJsonArray{first}}, {QStringLiteral("restore"), true}}));
        QCOMPARE(out.value(QStringLiteral("restored")).toArray().size(), 1);
        QCOMPARE(f.project().device(lead, delay).params.value(id), target);
        // All of them, the last first: the copy goes, the mixer is back.
        out = structured(call(protocol, QStringLiteral("rewind_agent_changes"), {{QStringLiteral("all"), true}}));
        QCOMPARE(out.value(QStringLiteral("rewound")).toArray().size(), 4);
        QVERIFY(!f.project().hasTrack(copy));
        QCOMPARE(f.project().track(lead).volumeDb, 0.0);
        QCOMPARE(history.appliedCount(), 0);

        // Edits turned off: the agent only reads, every edit refused (the new ones too)...
        out = structured(call(protocol, QStringLiteral("rewind_agent_changes"),
                              {{QStringLiteral("change_ids"), QJsonArray{first}}, {QStringLiteral("restore"), true}}));
        QCOMPARE(out.value(QStringLiteral("restored")).toArray().size(), 1);
        f.s().mcp()->setEditsAllowed(false);
        QVERIFY(call(protocol, QStringLiteral("set_track"), {{QStringLiteral("track_id"), lead}, {QStringLiteral("pan"), 1}})
                    .value(QStringLiteral("isError")).toBool());
        QCOMPARE(f.project().track(lead).pan, 0.0);
        const QJsonObject refused = call(protocol, QStringLiteral("add_track"), {{QStringLiteral("kind"), QStringLiteral("audio")}});
        QVERIFY(refused.value(QStringLiteral("isError")).toBool());
        QVERIFY(refused.value(QStringLiteral("content")).toArray()[0].toObject().value(QStringLiteral("text")).toString()
                    .contains(QStringLiteral("turned edits off")));
        QCOMPARE(f.project().tracks().size(), size_t{1});
        // ...but it can still take its changes back, not put them back.
        out = structured(call(protocol, QStringLiteral("rewind_agent_changes"), {{QStringLiteral("change_ids"), QJsonArray{first}}}));
        QCOMPARE(out.value(QStringLiteral("rewound")).toArray().size(), 1);
        QVERIFY(call(protocol, QStringLiteral("rewind_agent_changes"), {{QStringLiteral("change_ids"), QJsonArray{first}}, {QStringLiteral("restore"), true}})
                    .value(QStringLiteral("isError")).toBool());
        QVERIFY(!history.find(first)->applied);
        f.s().mcp()->setEditsAllowed(true);
        QSettings().remove(McpServer::kEditsKey);

        // Saved: clean again; another project: no changes.
        f.s().agentChanges()->setClean();
        QVERIFY(f.s().clean());
        f.s().newProject();
        QCOMPARE(history.count(), 0);
    }

    void theHistoriesDontUndoEachOther() {
        SessionFixture f;
        mcp::Protocol& protocol = f.s().mcp()->protocol();
        AgentHistory& history = *f.s().agentChanges();
        const QString lead = f.editor().addMidiTrack(-1, QStringLiteral("Lead"), QStringLiteral("synth"));
        const QString bass = f.editor().addMidiTrack(-1, QStringLiteral("Bass"), QStringLiteral("synth"));
        const auto setPan = [&](const QString& track, double pan) {
            return structured(call(protocol, QStringLiteral("set_track"),
                                   {{QStringLiteral("track_id"), track}, {QStringLiteral("pan"), pan}}))
                .value(QStringLiteral("changeId")).toInt();
        };

        // The user pans, then an agent pans the same track: Undo would put the
        // user's "before" back over the agent's pan, so it is refused (saying so)...
        f.editor().setTrackParam(lead, TrackField::Pan, -0.5);
        const int agentPan = setPan(lead, 0.5);
        const int userSteps = f.stack().index();
        f.s().undo();
        QCOMPARE(f.stack().index(), userSteps);
        QCOMPARE(f.project().track(lead).pan, 0.5);
        QVERIFY(f.lastMessage().startsWith(QStringLiteral("Can't undo")));
        QVERIFY(f.lastMessage().contains(QStringLiteral("Rewind it first")));
        // ...until the agent's change is rewound.
        QVERIFY(history.rewind(agentPan).isEmpty());
        QCOMPARE(f.project().track(lead).pan, -0.5);
        f.s().undo();
        QCOMPARE(f.stack().index(), userSteps - 1);
        QCOMPARE(f.project().track(lead).pan, 0.0);
        // Restoring it now would put the agent's pan over the user's undo: refused.
        QVERIFY(history.restore(agentPan).contains(QStringLiteral("you changed the same since")));
        f.s().redo();
        QCOMPARE(f.project().track(lead).pan, -0.5);

        // What doesn't touch the same goes either way: another track, another setting.
        const int bassPan = setPan(bass, 1.0);
        f.editor().setTrackParam(lead, TrackField::VolumeDb, -3.0);
        f.s().undo();
        QCOMPARE(f.project().track(lead).volumeDb, 0.0);
        QCOMPARE(f.project().track(bass).pan, 1.0);
        QVERIFY(history.rewind(bassPan).isEmpty());
        QCOMPARE(f.project().track(bass).pan, 0.0);

        // An agent can't take back what the user changed the same of since.
        const int agentAgain = setPan(lead, 0.75);
        f.editor().setTrackParam(lead, TrackField::Pan, -1.0);
        const QString why = history.rewind(agentAgain);
        QVERIFY(why.contains(QStringLiteral("you changed the same since")));
        QCOMPARE(f.project().track(lead).pan, -1.0);
        QVERIFY(call(protocol, QStringLiteral("rewind_agent_changes"), {{QStringLiteral("all"), true}})
                    .value(QStringLiteral("isError")).toBool());
        // A later change on the same comes first.
        f.s().undo();  // (the user's -1 goes: the agent's 0.75 is back)
        const int later = setPan(lead, 0.1);
        QVERIFY(history.rewind(agentAgain).contains(QStringLiteral("the later change")));
        QVERIFY(history.rewindFrom(agentAgain).isEmpty());  // (the later one first, then it)
        QCOMPARE(f.project().track(lead).pan, -0.5);
        QVERIFY(!history.find(later)->applied);

        // A track an agent made can't go while the user's edit of it stays; once
        // it went, that edit can't come back (it would edit a track that isn't there).
        const QString made = structured(call(protocol, QStringLiteral("duplicate_track"), {{QStringLiteral("track_id"), lead}}))
                                 .value(QStringLiteral("trackId")).toString();
        f.editor().renameTrack(made, QStringLiteral("Mine"));
        QVERIFY(history.rewindAll().contains(QStringLiteral("you changed the same since")));
        QVERIFY(f.project().hasTrack(made));
        f.s().undo();
        QVERIFY(history.rewindAll().isEmpty());
        QVERIFY(!f.project().hasTrack(made));
        f.s().redo();  // (the rename of a track that went: refused, not a crash)
        QVERIFY(f.lastMessage().startsWith(QStringLiteral("Can't redo")));
        QVERIFY(!f.project().hasTrack(made));
    }

    void footprintsOverlapByRegion() {
        using Regions = QStringList;
        QVERIFY(AgentHistory::overlaps(Regions{QStringLiteral("track/a")}, Regions{QStringLiteral("track/a/clips")}));
        QVERIFY(AgentHistory::overlaps(Regions{QStringLiteral("track/a/devices/d/param/x")},
                                       Regions{QStringLiteral("track/a/devices")}));
        QVERIFY(!AgentHistory::overlaps(Regions{QStringLiteral("track/a/devices/d/param/x")},
                                        Regions{QStringLiteral("track/a/devices/d/param/xy")}));
        QVERIFY(!AgentHistory::overlaps(Regions{QStringLiteral("track/a")}, Regions{QStringLiteral("track/ab")}));
        QVERIFY(!AgentHistory::overlaps(Regions{QStringLiteral("order/a")}, Regions{QStringLiteral("order/b")}));
        QVERIFY(AgentHistory::overlaps(Regions{QStringLiteral("order")}, Regions{QStringLiteral("order/b")}));
        QVERIFY(AgentHistory::overlaps(Regions{QStringLiteral("*")}, Regions{QStringLiteral("settings/tempo")}));
        QVERIFY(!AgentHistory::overlaps(Regions{}, Regions{QStringLiteral("*")}));

        // What the model's commands touch, macros by their parts.
        SessionFixture f;
        const QString lead = f.editor().addMidiTrack(-1, QStringLiteral("Lead"), QStringLiteral("synth"));
        f.editor().setTrackParam(lead, TrackField::Pan, 0.5);
        const QUndoCommand* pan = f.stack().command(f.stack().index() - 1);
        QCOMPARE(AgentHistory::footprint(*pan, f.project()),
                 QStringList{QStringLiteral("track/%1/field/pan").arg(lead)});
        f.editor().addMidiClip(lead, 0, 4);
        QVERIFY(AgentHistory::footprint(*f.stack().command(f.stack().index() - 1), f.project())
                    .contains(QStringLiteral("track/%1/clips").arg(lead)));
        const QString bass = f.editor().addMidiTrack(-1, QStringLiteral("Bass"), QStringLiteral("synth"));
        f.editor().groupTracks({lead, bass});  // (a macro)
        const QStringList grouped = AgentHistory::footprint(*f.stack().command(f.stack().index() - 1), f.project());
        QVERIFY(grouped.contains(QStringLiteral("order")));
        QVERIFY(!grouped.contains(QStringLiteral("*")));
    }

    // --- The arrangement at a glance ----------------------------------------------------------

    void theArrangementMapShowsWherePartsPlay() {
        SessionFixture f;
        mcp::Protocol& protocol = f.s().mcp()->protocol();
        // Drums in bars 1-8 and 17-24, a bass in 9-24, an empty clip in 25-26 (4/4: a bar is 4 beats).
        const QString drums = f.editor().addMidiTrack(-1, QStringLiteral("Drums"), QStringLiteral("synth"));
        const QString bass = f.editor().addMidiTrack(-1, QStringLiteral("Bass"), QStringLiteral("synth"));
        const QString pad = f.editor().addMidiTrack(-1, QStringLiteral("Pad"), QStringLiteral("synth"));
        const QString empty = f.editor().addMidiTrack(-1, QStringLiteral("Empty"), QStringLiteral("synth"));
        const auto part = [&](const QString& track, double start, double length) {
            const auto clip = f.editor().addMidiClip(track, start, length);
            QVERIFY(clip);
            std::vector<Note> notes;
            for (double b = 0; b < length; b += 1.0) notes.push_back({36, b, 0.5, 100});
            f.editor().setClipNotes(*clip, notes, QStringLiteral("Notes"));
        };
        part(drums, 0, 32);
        part(drums, 64, 32);
        part(bass, 32, 64);
        QVERIFY(f.editor().addMidiClip(pad, 96, 8));
        const QString group = f.editor().groupTracks({drums});

        mcp::ArrangementMap map = mcp::arrangementMap(f.project());
        QCOMPARE(map.firstBar, 1);
        QCOMPARE(map.lastBar, 26);
        QCOMPARE(map.barsPerCell, 1);
        QCOMPARE(map.emptyTracks, 1);  // (Empty: no clips)
        QCOMPARE(map.rows.size(), size_t{4});  // the group, its drums, the bass, the pad
        QCOMPARE(map.rows[0].id, group);
        QCOMPARE(map.rows[1].id, drums);
        QCOMPARE(map.rows[1].depth, 1);
        const QString drumLine = QStringLiteral("########........########..");
        QCOMPARE(map.rows[0].cells, drumLine);  // (a group's line: what its tracks play)
        QCOMPARE(map.rows[1].cells, drumLine);
        QCOMPARE(map.rows[2].cells, QStringLiteral("........################.."));
        QCOMPARE(map.rows[3].cells, QStringLiteral("........................--"));  // (a clip, silent)
        // Sections from which tracks play: drums, bass, both, nothing.
        QCOMPARE(map.sections.size(), size_t{4});
        QCOMPARE(map.sections[0].name, QStringLiteral("A"));
        QCOMPARE(map.sections[0].firstBar, 1);
        QCOMPARE(map.sections[0].lastBar, 8);
        QCOMPARE(map.sections[0].trackIds, QStringList{drums});
        QCOMPARE(map.sections[1].name, QStringLiteral("B"));
        QCOMPARE(map.sections[2].name, QStringLiteral("C"));
        QCOMPARE(map.sections[2].trackIds, (QStringList{drums, bass}));
        QCOMPARE(map.sections[3].name, QStringLiteral("silent"));
        QCOMPARE(map.sections[3].lastBar, 26);

        // A muted track isn't heard: the drums' parts are the same part again.
        f.editor().setTrackParam(bass, TrackField::Mute, 1.0);
        map = mcp::arrangementMap(f.project());
        QVERIFY(map.rows[2].muted);
        QCOMPARE(map.sections.size(), size_t{4});
        QCOMPARE(map.sections[1].name, QStringLiteral("silent"));
        QCOMPARE(map.sections[2].name, QStringLiteral("A"));
        f.editor().setTrackParam(bass, TrackField::Mute, 0.0);

        // Two bars a character; part of the song.
        map = mcp::arrangementMap(f.project(), 1, 0, 2);
        QCOMPARE(map.rows[1].cells, QStringLiteral("####....####."));
        map = mcp::arrangementMap(f.project(), 9, 16);
        QCOMPARE(map.rows.size(), size_t{1});  // (only the bass plays there)
        QCOMPARE(map.rows[0].cells, QStringLiteral("########"));
        QCOMPARE(map.emptyTracks, 3);

        // The tool: the grid as text, with ids, and the sections by bars.
        const QJsonObject called = call(protocol, QStringLiteral("get_arrangement_map"));
        const QString text = called.value(QStringLiteral("content")).toArray()[0].toObject().value(QStringLiteral("text")).toString();
        QVERIFY(text.contains(QStringLiteral("1       9       17      25")));
        QVERIFY(text.contains(drumLine + QStringLiteral("  ") + drums));
        QVERIFY(text.contains(QStringLiteral("  Drums")));  // (in its group)
        QVERIFY(text.contains(QStringLiteral("bars 17-24: Drums, Bass")));
        const QJsonObject out = structured(called);
        QCOMPARE(out.value(QStringLiteral("sections")).toArray().size(), 4);
        QCOMPARE(out.value(QStringLiteral("tracks")).toArray()[1].toObject().value(QStringLiteral("cells")).toString(), drumLine);
        QVERIFY(call(protocol, QStringLiteral("get_arrangement_map"), {{QStringLiteral("first_bar"), 0}})
                    .value(QStringLiteral("isError")).toBool());
        QVERIFY(call(protocol, QStringLiteral("get_arrangement_map"), {{QStringLiteral("first_bar"), 5}, {QStringLiteral("last_bar"), 4}})
                    .value(QStringLiteral("isError")).toBool());

        // An empty project says so.
        f.s().newProject();
        QVERIFY(call(protocol, QStringLiteral("get_arrangement_map"))
                    .value(QStringLiteral("content")).toArray()[0].toObject().value(QStringLiteral("text")).toString()
                    .contains(QStringLiteral("empty")));
    }

    // --- Prompts and resources ---------------------------------------------------------------

    void promptsAndResources() {
        SessionFixture f;
        mcp::Protocol& protocol = f.s().mcp()->protocol();
        f.editor().addMidiTrack(-1, QStringLiteral("Lead"), QStringLiteral("synth"));
        // Offered, and listed.
        const QJsonObject capabilities = result(protocol.handle(request(QStringLiteral("initialize"))))
                                             .value(QStringLiteral("capabilities")).toObject();
        QVERIFY(capabilities.contains(QStringLiteral("prompts")));
        QVERIFY(capabilities.contains(QStringLiteral("resources")));
        const QJsonArray prompts = result(protocol.handle(request(QStringLiteral("prompts/list"))))
                                       .value(QStringLiteral("prompts")).toArray();
        QCOMPARE(prompts.size(), 3);
        QCOMPARE(prompts[0].toObject().value(QStringLiteral("name")).toString(), QStringLiteral("write_part"));
        QVERIFY(prompts[0].toObject().value(QStringLiteral("arguments")).toArray()[0].toObject().value(QStringLiteral("required")).toBool());

        // A prompt: one user message with its arguments in it; one it needs missing, or none such: errors.
        mcp::Reply reply = protocol.handle(request(
            QStringLiteral("prompts/get"), {{QStringLiteral("name"), QStringLiteral("write_part")},
                                            {QStringLiteral("arguments"), QJsonObject{{QStringLiteral("request"), QStringLiteral("a funkier bass")}}}}));
        const QJsonArray messages = result(reply).value(QStringLiteral("messages")).toArray();
        QCOMPARE(messages.size(), 1);
        QCOMPARE(messages[0].toObject().value(QStringLiteral("role")).toString(), QStringLiteral("user"));
        const QString text = messages[0].toObject().value(QStringLiteral("content")).toObject().value(QStringLiteral("text")).toString();
        QVERIFY(text.contains(QStringLiteral("a funkier bass")));
        QVERIFY(text.contains(QStringLiteral("get_selection_context")));
        reply = protocol.handle(request(QStringLiteral("prompts/get"), {{QStringLiteral("name"), QStringLiteral("write_part")}}));
        QCOMPARE(errorCode(reply), mcp::code::kInvalidParams);
        reply = protocol.handle(request(QStringLiteral("prompts/get"), {{QStringLiteral("name"), QStringLiteral("nope")}}));
        QCOMPARE(errorCode(reply), mcp::code::kInvalidParams);
        QVERIFY(!result(protocol.handle(request(QStringLiteral("prompts/get"), {{QStringLiteral("name"), QStringLiteral("mix_review")}})))
                     .value(QStringLiteral("messages")).toArray().isEmpty());

        // Resources: listed, read as they are now; one that isn't there.
        const QJsonArray resources = result(protocol.handle(request(QStringLiteral("resources/list"))))
                                         .value(QStringLiteral("resources")).toArray();
        QCOMPARE(resources.size(), 2);
        reply = protocol.handle(request(QStringLiteral("resources/read"), {{QStringLiteral("uri"), QStringLiteral("substation://project")}}));
        const QJsonObject content = result(reply).value(QStringLiteral("contents")).toArray()[0].toObject();
        QCOMPARE(content.value(QStringLiteral("mimeType")).toString(), QStringLiteral("application/json"));
        QCOMPARE(QJsonDocument::fromJson(content.value(QStringLiteral("text")).toString().toUtf8()).object()
                     .value(QStringLiteral("trackCount")).toInt(),
                 1);
        reply = protocol.handle(request(QStringLiteral("resources/read"), {{QStringLiteral("uri"), QStringLiteral("substation://arrangement")}}));
        QVERIFY(result(reply).value(QStringLiteral("contents")).toArray()[0].toObject().value(QStringLiteral("text")).toString()
                    .contains(QStringLiteral("empty")));  // (no clips yet)
        reply = protocol.handle(request(QStringLiteral("resources/read"), {{QStringLiteral("uri"), QStringLiteral("substation://nope")}}));
        QCOMPARE(errorCode(reply), mcp::code::kResourceNotFound);

        // Modern: Mcp-Name says the prompt's name (or the resource's URI), as for a tool.
        QJsonObject message = modern(QStringLiteral("prompts/get"), {{QStringLiteral("name"), QStringLiteral("describe_song")}});
        mcp::Headers headers = headersFor(message);
        QCOMPARE(protocol.handle(message, headers).status, 200);
        headers.name = QStringLiteral("write_part");
        QCOMPARE(protocol.handle(message, headers).status, 400);
        message = modern(QStringLiteral("resources/read"), {{QStringLiteral("uri"), QStringLiteral("substation://arrangement")}});
        headers = headersFor(message);
        headers.name = QStringLiteral("substation://arrangement");
        reply = protocol.handle(message, headers);
        QCOMPARE(reply.status, 200);
        QCOMPARE(result(reply).value(QStringLiteral("resultType")).toString(), QStringLiteral("complete"));
    }

    // --- Edits of notes, automation, tracks and devices ----------------------------------------

    void editNotesWhereTheyAre() {
        SessionFixture f;
        mcp::Protocol& protocol = f.s().mcp()->protocol();
        AgentHistory& history = *f.s().agentChanges();
        const QString lead = f.editor().addMidiTrack(-1, QStringLiteral("Lead"), QStringLiteral("synth"));
        const auto first = f.editor().addMidiClip(lead, 0, 4);
        const auto second = f.editor().addMidiClip(lead, 8, 4);
        QVERIFY(first && second);
        Note bent{60, 0, 1, 100};
        bent.bend = {{0.0, 0.0, 0.0}, {0.5, 1.0, 0.0}};
        f.editor().setClipNotes(*first, {bent, {64, 1, 1, 80}, {67, 2, 0.9, 60}}, QStringLiteral("Notes"));
        f.editor().setClipNotes(*second, {{36, 0.1, 0.5, 100}}, QStringLiteral("Notes"));
        const int undoSteps = f.stack().count();
        const auto edit = [&](const QJsonObject& arguments) {
            QJsonObject all = arguments;
            all.insert(QStringLiteral("track_id"), lead);
            return call(protocol, QStringLiteral("edit_notes"), all);
        };
        const auto notesOf = [&](const ClipRef& ref) { return f.project().clip(lead, ref.clipId).notes; };

        // Transposed: only the pitch asked for; its bend stays.
        QJsonObject out = structured(edit({{QStringLiteral("pitches"), QJsonArray{QStringLiteral("C3")}},
                                           {QStringLiteral("operations"), QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("transpose")},
                                                                                                {QStringLiteral("semitones"), 12}}}}}));
        QCOMPARE(out.value(QStringLiteral("notes")).toInt(), 1);
        QCOMPARE(notesOf(*first)[0].pitch, 72);
        QCOMPARE(notesOf(*first)[0].bend, bent.bend);
        QCOMPARE(notesOf(*first)[1].pitch, 64);
        QCOMPARE(f.stack().count(), undoSteps);  // (the agents' history, not the user's undo)
        QVERIFY(history.changes().back().text.contains(QStringLiteral("transpose +12")));

        // Quantized on the song's grid, in a stretch of bars (bar 3: the second clip).
        out = structured(edit({{QStringLiteral("start_bar"), 3},
                               {QStringLiteral("operations"), QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("quantize")},
                                                                                    {QStringLiteral("grid"), 0.5}}}}}));
        QCOMPARE(notesOf(*second)[0].start, 0.0);
        QCOMPARE(notesOf(*first)[0].start, 0.0);  // (not in the stretch)

        // Several in order: velocities set, then halved; lengths; legato.
        edit({{QStringLiteral("clip_ids"), QJsonArray{first->clipId}},
              {QStringLiteral("operations"), QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("velocity")}, {QStringLiteral("set"), 100}},
                                                        QJsonObject{{QStringLiteral("type"), QStringLiteral("velocity")}, {QStringLiteral("factor"), 0.5}},
                                                        QJsonObject{{QStringLiteral("type"), QStringLiteral("length")}, {QStringLiteral("beats"), 0.25}}}}});
        for (const Note& note : notesOf(*first)) {
            QCOMPARE(note.velocity, 50);
            QCOMPARE(note.length, 0.25);
        }
        edit({{QStringLiteral("end_bar"), 2}, {QStringLiteral("operations"), QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("legato")}}}}});
        QCOMPARE(notesOf(*first)[0].length, 1.0);  // (up to the next)
        QCOMPARE(notesOf(*first)[2].length, 2.0);  // (the last: to the clip's end)
        edit({{QStringLiteral("operations"), QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("velocity")}, {QStringLiteral("change"), -100}}}}});
        QCOMPARE(notesOf(*second)[0].velocity, 1);  // (held to 1..127)

        // Shifted, timing scaled, humanized (the same seed: the same nudges).
        edit({{QStringLiteral("pitches"), QJsonArray{67}},
              {QStringLiteral("operations"), QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("shift")}, {QStringLiteral("beats"), -0.5}}}}});
        QCOMPARE(notesOf(*first)[2].start, 1.5);
        edit({{QStringLiteral("clip_ids"), QJsonArray{first->clipId}},
              {QStringLiteral("operations"), QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("scale_time")}, {QStringLiteral("factor"), 0.5}}}}});
        QCOMPARE(notesOf(*first)[1].start, 0.5);
        out = structured(edit({{QStringLiteral("operations"), QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("humanize_timing")},
                                                                                    {QStringLiteral("seed"), 7}}}}}));
        QVERIFY(out.contains(QStringLiteral("changeId")));
        // Humanized velocities, if the model is there; else a failure saying so.
        const QJsonObject humanized = edit({{QStringLiteral("operations"), QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("humanize_velocity")}}}}});
        QVERIFY(!humanized.value(QStringLiteral("isError")).toBool() ||
                humanized.value(QStringLiteral("content")).toArray()[0].toObject().value(QStringLiteral("text")).toString().contains(QStringLiteral("velocity model")));

        // Muted, unmuted, deleted.
        const auto withPitch = [&](int pitch) {
            for (const Note& note : notesOf(*first)) {
                if (note.pitch == pitch) return note;
            }
            return Note{};
        };
        edit({{QStringLiteral("pitches"), QJsonArray{64}}, {QStringLiteral("operations"), QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("mute")}}}}});
        QVERIFY(withPitch(64).muted);
        QVERIFY(!withPitch(72).muted);
        edit({{QStringLiteral("pitches"), QJsonArray{64}}, {QStringLiteral("operations"), QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("delete")}}}}});
        QCOMPARE(notesOf(*first).size(), size_t{2});
        QCOMPARE(withPitch(64).pitch, 60);  // (gone: the default note)

        // Wrong: no such operation, off the keyboard, no notes there, not a MIDI track; nothing changed.
        const int changes = history.count();
        QVERIFY(edit({{QStringLiteral("operations"), QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("explode")}}}}})
                    .value(QStringLiteral("isError")).toBool());
        QVERIFY(edit({{QStringLiteral("operations"), QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("transpose")},
                                                                            {QStringLiteral("semitones"), 100}}}}})
                    .value(QStringLiteral("isError")).toBool());
        QVERIFY(edit({{QStringLiteral("start_bar"), 20}, {QStringLiteral("operations"), QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("mute")}}}}})
                    .value(QStringLiteral("isError")).toBool());
        QVERIFY(edit({}).value(QStringLiteral("isError")).toBool());
        const QString audio = f.editor().addAudioTrack();
        QVERIFY(call(protocol, QStringLiteral("edit_notes"),
                     {{QStringLiteral("track_id"), audio}, {QStringLiteral("operations"), QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("mute")}}}}})
                    .value(QStringLiteral("isError")).toBool());
        QCOMPARE(history.count(), changes);
        // Rewound, all of it: the notes as they were.
        QVERIFY(history.rewindAll().isEmpty());
        QCOMPARE(notesOf(*first).size(), size_t{3});
        QCOMPARE(notesOf(*first)[0], bent);
        QCOMPARE(notesOf(*second)[0].start, 0.1);
    }

    void writeAutomationDrawsEnvelopes() {
        SessionFixture f;
        mcp::Protocol& protocol = f.s().mcp()->protocol();
        const QString lead = f.editor().addMidiTrack(-1, QStringLiteral("Lead"), QStringLiteral("synth"));
        const auto write = [&](const QJsonObject& arguments) {
            QJsonObject all = arguments;
            all.insert(QStringLiteral("track_id"), lead);
            return call(protocol, QStringLiteral("write_automation"), all);
        };
        const auto pointsOf = [&](const QString& key) {
            for (const QJsonValue& e : structured(call(protocol, QStringLiteral("get_automation"), {{QStringLiteral("track_id"), lead}}))
                                           .value(QStringLiteral("envelopes")).toArray()) {
                if (e.toObject().value(QStringLiteral("key")).toString() == key) return e.toObject().value(QStringLiteral("points")).toArray();
            }
            return QJsonArray();
        };

        // Points in the target's units (a fader: dB), read back the same.
        QJsonObject out = structured(write({{QStringLiteral("mixer"), QStringLiteral("volume")},
                                            {QStringLiteral("points"), QJsonArray{QJsonArray{0, -70, 0.5}, QJsonArray{8, 0}}}}));
        QCOMPARE(out.value(QStringLiteral("key")).toString(), automation::kMixerVolume);
        QJsonArray points = pointsOf(automation::kMixerVolume);
        QCOMPARE(points.size(), 2);
        QCOMPARE(points[0].toArray()[1].toDouble(), -70.0);
        QCOMPARE(points[1].toArray(), (QJsonArray{8, 0}));
        QCOMPARE(points[0].toArray()[2].toDouble(), 0.5);  // (the segment from it: curved)
        QVERIFY(f.s().agentChanges()->changes().back().text.contains(QStringLiteral("bars 1 to 2")));
        // Points in a stretch replace it, the rest stays.
        write({{QStringLiteral("mixer"), QStringLiteral("volume")}, {QStringLiteral("start_beat"), 2}, {QStringLiteral("end_beat"), 6},
               {QStringLiteral("points"), QJsonArray{QJsonArray{4, -6}}}});
        const Envelope volume = f.project().envelope(lead, automation::kMixerVolume);
        QCOMPARE(volume.front().beat, 0.0);
        QCOMPARE(volume.back().beat, 8.0);
        QVERIFY(std::abs(*automation::valueAt(volume, 4.0) - automation::volumeToNormalized(-6.0)) < 1e-6);
        // One point goes in among the others.
        write({{QStringLiteral("mixer"), QStringLiteral("volume")}, {QStringLiteral("points"), QJsonArray{QJsonArray{7, -3}}}});
        QCOMPARE(f.project().envelope(lead, automation::kMixerVolume).size(), volume.size() + 1);

        // A ramp over bars 2 to 3 (a pan sweep).
        write({{QStringLiteral("mixer"), QStringLiteral("pan")}, {QStringLiteral("start_bar"), 2}, {QStringLiteral("end_bar"), 4},
               {QStringLiteral("shape"), QJsonObject{{QStringLiteral("type"), QStringLiteral("ramp")}, {QStringLiteral("from"), -1}, {QStringLiteral("to"), 1}}}});
        points = pointsOf(automation::kMixerPan);
        QCOMPARE(points.size(), 2);
        QCOMPARE(points[0].toArray(), (QJsonArray{4, -1}));
        QCOMPARE(points[1].toArray(), (QJsonArray{12, 1}));

        // An LFO on a device's parameter: a cycle a beat over a bar, from low to high and back.
        const QString delay = f.editor().addDevice(lead, QStringLiteral("delay"));
        const QJsonObject param = structured(call(protocol, QStringLiteral("get_device_parameters"),
                                                  {{QStringLiteral("track_id"), lead}, {QStringLiteral("device_id"), delay}}))
                                      .value(QStringLiteral("parameters")).toArray()[1].toObject();
        const QString paramId = param.value(QStringLiteral("id")).toString();
        const double low = param.value(QStringLiteral("min")).toDouble();
        const double high = param.value(QStringLiteral("max")).toDouble();
        out = structured(write({{QStringLiteral("device_id"), delay}, {QStringLiteral("parameter_id"), paramId},
                                {QStringLiteral("start_bar"), 1}, {QStringLiteral("end_bar"), 2},
                                {QStringLiteral("shape"), QJsonObject{{QStringLiteral("type"), QStringLiteral("lfo")}, {QStringLiteral("wave"), QStringLiteral("triangle")},
                                                                      {QStringLiteral("low"), low}, {QStringLiteral("high"), high},
                                                                      {QStringLiteral("cycle_beats"), 1}}}}));
        const QString key = out.value(QStringLiteral("key")).toString();
        const Envelope lfo = f.project().envelope(lead, key);
        QCOMPARE(lfo.size(), size_t{9});  // (low, high, low... from beat 0 to 4)
        QCOMPARE(lfo[0].value, 0.0);
        QCOMPARE(lfo[1].beat, 0.5);
        QCOMPARE(lfo[1].value, 1.0);
        QCOMPARE(lfo.back().beat, 4.0);
        // A square: steps (two points at a beat).
        write({{QStringLiteral("key"), key}, {QStringLiteral("start_bar"), 1}, {QStringLiteral("end_bar"), 2},
               {QStringLiteral("shape"), QJsonObject{{QStringLiteral("type"), QStringLiteral("lfo")}, {QStringLiteral("wave"), QStringLiteral("square")},
                                                     {QStringLiteral("low"), low}, {QStringLiteral("high"), high}, {QStringLiteral("cycle_beats"), 2}}}});
        const Envelope square = f.project().envelope(lead, key);
        QCOMPARE(*automation::valueAt(square, 0.5), 1.0);
        QCOMPARE(*automation::valueAt(square, 1.5), 0.0);
        // Cleared in a stretch, then all of it.
        write({{QStringLiteral("key"), key}, {QStringLiteral("clear"), true}, {QStringLiteral("start_beat"), 1}, {QStringLiteral("end_beat"), 3}});
        QVERIFY(!automation::hasPointsIn(f.project().envelope(lead, key), 1.01, 2.99));
        write({{QStringLiteral("key"), key}, {QStringLiteral("clear"), true}});
        QVERIFY(f.project().envelope(lead, key).empty());

        // Wrong: two targets or none, a value out of range, a shape without an end, points outside the stretch.
        for (const QJsonObject& wrong : {
                 QJsonObject{{QStringLiteral("mixer"), QStringLiteral("pan")}, {QStringLiteral("send"), QStringLiteral("x")}, {QStringLiteral("clear"), true}},
                 QJsonObject{{QStringLiteral("points"), QJsonArray{QJsonArray{0, 0}}}},
                 QJsonObject{{QStringLiteral("mixer"), QStringLiteral("pan")}, {QStringLiteral("points"), QJsonArray{QJsonArray{0, 5}}}},
                 QJsonObject{{QStringLiteral("mixer"), QStringLiteral("pan")},
                             {QStringLiteral("shape"), QJsonObject{{QStringLiteral("type"), QStringLiteral("ramp")}, {QStringLiteral("from"), 0}, {QStringLiteral("to"), 1}}}},
                 QJsonObject{{QStringLiteral("mixer"), QStringLiteral("pan")}, {QStringLiteral("end_beat"), 2},
                             {QStringLiteral("points"), QJsonArray{QJsonArray{4, 0}}}},
                 QJsonObject{{QStringLiteral("mixer"), QStringLiteral("pan")}, {QStringLiteral("points"), QJsonArray{QJsonArray{0, 0}}},
                             {QStringLiteral("clear"), true}}}) {
            QVERIFY2(write(wrong).value(QStringLiteral("isError")).toBool(), qPrintable(QJsonDocument(wrong).toJson(QJsonDocument::Compact)));
        }
        QCOMPARE(f.s().agentChanges()->count(), 8);
        QCOMPARE(f.project().envelope(lead, automation::kMixerPan).size(), size_t{2});
    }

    void tracksAddedMovedAndGrouped() {
        SessionFixture f;
        mcp::Protocol& protocol = f.s().mcp()->protocol();
        AgentHistory& history = *f.s().agentChanges();
        const QString drums = f.editor().addMidiTrack(-1, QStringLiteral("Drums"), QStringLiteral("synth"));
        const int undoSteps = f.stack().count();
        const auto ids = [&] {
            QStringList out;
            for (const Track& track : f.project().tracks()) out << track.id;
            return out;
        };

        // A MIDI track with an instrument, named; an empty one; an audio track after the first; a return.
        QJsonObject out = structured(call(protocol, QStringLiteral("add_track"),
                                          {{QStringLiteral("kind"), QStringLiteral("midi")}, {QStringLiteral("name"), QStringLiteral("Bass")},
                                           {QStringLiteral("instrument"), QStringLiteral("synth")}}));
        const QString bass = out.value(QStringLiteral("trackId")).toString();
        QCOMPARE(f.project().track(bass).name, QStringLiteral("Bass"));
        QCOMPARE(f.project().track(bass).devices.size(), size_t{1});
        QCOMPARE(f.project().track(bass).devices[0].kind, QStringLiteral("synth"));
        const QString empty = structured(call(protocol, QStringLiteral("add_track"), {{QStringLiteral("kind"), QStringLiteral("midi")}}))
                                  .value(QStringLiteral("trackId")).toString();
        QVERIFY(f.project().track(empty).devices.empty());
        const QString audio = structured(call(protocol, QStringLiteral("add_track"),
                                              {{QStringLiteral("kind"), QStringLiteral("audio")}, {QStringLiteral("after_track_id"), drums}}))
                                  .value(QStringLiteral("trackId")).toString();
        QCOMPARE(ids(), (QStringList{drums, audio, bass, empty}));
        const QString ret = structured(call(protocol, QStringLiteral("add_track"),
                                            {{QStringLiteral("kind"), QStringLiteral("return")}, {QStringLiteral("name"), QStringLiteral("Verb")}}))
                                .value(QStringLiteral("trackId")).toString();
        QVERIFY(f.project().hasReturn(ret));
        // Wrong: an instrument on audio, an effect as an instrument, one that isn't there, a kind that isn't.
        for (const QJsonObject& wrong : {
                 QJsonObject{{QStringLiteral("kind"), QStringLiteral("audio")}, {QStringLiteral("instrument"), QStringLiteral("synth")}},
                 QJsonObject{{QStringLiteral("kind"), QStringLiteral("midi")}, {QStringLiteral("instrument"), QStringLiteral("delay")}},
                 QJsonObject{{QStringLiteral("kind"), QStringLiteral("midi")}, {QStringLiteral("instrument"), QStringLiteral("plugin:nope")}},
                 QJsonObject{{QStringLiteral("kind"), QStringLiteral("video")}},
                 QJsonObject{{QStringLiteral("kind"), QStringLiteral("midi")}, {QStringLiteral("after_track_id"), QStringLiteral("nope")}}}) {
            QVERIFY(call(protocol, QStringLiteral("add_track"), wrong).value(QStringLiteral("isError")).toBool());
        }

        // Grouped (named), moved into the group, before a track, last and out of it; ungrouped.
        out = structured(call(protocol, QStringLiteral("group_tracks"),
                              {{QStringLiteral("track_ids"), QJsonArray{drums, audio}}, {QStringLiteral("name"), QStringLiteral("Rhythm")}}));
        const QString group = out.value(QStringLiteral("groupId")).toString();
        QCOMPARE(f.project().track(group).name, QStringLiteral("Rhythm"));
        QCOMPARE(f.project().track(drums).parent, std::optional<QString>(group));
        call(protocol, QStringLiteral("move_tracks"), {{QStringLiteral("track_ids"), QJsonArray{bass}}, {QStringLiteral("group_id"), group}});
        QCOMPARE(f.project().track(bass).parent, std::optional<QString>(group));
        QCOMPARE(ids(), (QStringList{group, drums, audio, bass, empty}));
        call(protocol, QStringLiteral("move_tracks"), {{QStringLiteral("track_ids"), QJsonArray{bass}}, {QStringLiteral("before_track_id"), drums}});
        QCOMPARE(ids(), (QStringList{group, bass, drums, audio, empty}));
        call(protocol, QStringLiteral("move_tracks"), {{QStringLiteral("track_ids"), QJsonArray{bass}}});
        QCOMPARE(ids(), (QStringList{group, drums, audio, empty, bass}));
        QVERIFY(!f.project().track(bass).parent);
        call(protocol, QStringLiteral("move_tracks"), {{QStringLiteral("track_ids"), QJsonArray{empty}}, {QStringLiteral("after_track_id"), bass}});
        QCOMPARE(ids(), (QStringList{group, drums, audio, bass, empty}));
        QVERIFY(call(protocol, QStringLiteral("move_tracks"), {{QStringLiteral("track_ids"), QJsonArray{group}}, {QStringLiteral("group_id"), group}})
                    .value(QStringLiteral("isError")).toBool());  // (a group into itself)
        QVERIFY(call(protocol, QStringLiteral("group_tracks"), {{QStringLiteral("ungroup"), QJsonArray{drums}}})
                    .value(QStringLiteral("isError")).toBool());  // (not a group)
        QVERIFY(call(protocol, QStringLiteral("group_tracks"), {{QStringLiteral("ungroup"), QJsonArray{group}}, {QStringLiteral("track_ids"), QJsonArray{bass}}})
                    .value(QStringLiteral("isError")).toBool());  // (one or the other)
        call(protocol, QStringLiteral("group_tracks"), {{QStringLiteral("ungroup"), QJsonArray{group}}});
        QVERIFY(!f.project().hasTrack(group));
        QVERIFY(!f.project().track(drums).parent);

        // Named, colored and its mixer set in one change; a color that isn't one, the master's name: nothing changed.
        const int before = history.count();
        call(protocol, QStringLiteral("set_track"), {{QStringLiteral("track_id"), bass}, {QStringLiteral("name"), QStringLiteral("Sub")},
                                                    {QStringLiteral("color"), QStringLiteral("#FF94A6")}, {QStringLiteral("volume_db"), -3}});
        QCOMPARE(history.count(), before + 1);
        QCOMPARE(f.project().track(bass).name, QStringLiteral("Sub"));
        QCOMPARE(f.project().track(bass).color, QStringLiteral("#ff94a6"));
        QCOMPARE(f.project().track(bass).volumeDb, -3.0);
        QVERIFY(history.changes().back().text.contains(QStringLiteral("name to Sub")));
        QVERIFY(call(protocol, QStringLiteral("set_track"), {{QStringLiteral("track_id"), drums}, {QStringLiteral("color"), QStringLiteral("red")}})
                    .value(QStringLiteral("isError")).toBool());
        QVERIFY(call(protocol, QStringLiteral("set_track"), {{QStringLiteral("track_id"), kMaster}, {QStringLiteral("name"), QStringLiteral("Main")}})
                    .value(QStringLiteral("isError")).toBool());
        QVERIFY(call(protocol, QStringLiteral("set_track"), {{QStringLiteral("track_id"), drums}})
                    .value(QStringLiteral("isError")).toBool());  // (nothing to change)
        QCOMPARE(history.count(), before + 1);

        // Deleted (a return too); the master can't be.
        call(protocol, QStringLiteral("delete_tracks"), {{QStringLiteral("track_ids"), QJsonArray{empty, ret}}});
        QVERIFY(!f.project().hasTrack(empty));
        QVERIFY(!f.project().hasReturn(ret));
        QVERIFY(call(protocol, QStringLiteral("delete_tracks"), {{QStringLiteral("track_ids"), QJsonArray{kMaster}}})
                    .value(QStringLiteral("isError")).toBool());

        // None of it on the user's undo stack; all of it rewound: as it was.
        QCOMPARE(f.stack().count(), undoSteps);
        QVERIFY(history.rewindAll().isEmpty());
        QCOMPARE(ids(), QStringList{drums});
        QVERIFY(f.project().returns().empty());
    }

    void devicesListedAddedMovedRemoved() {
        SessionFixture f;
        mcp::Protocol& protocol = f.s().mcp()->protocol();
        AgentHistory& history = *f.s().agentChanges();
        const QString lead = f.editor().addMidiTrack(-1, QStringLiteral("Lead"), QStringLiteral("synth"));
        const QString audio = f.editor().addAudioTrack();
        const auto kinds = [&](const QString& track) {
            QStringList out;
            for (const Device& device : f.project().track(track).devices) out << device.kind;
            return out;
        };

        // Listed: built-in ones by words, instruments alone, a page at a time.
        QJsonObject out = structured(call(protocol, QStringLiteral("list_devices"), {{QStringLiteral("query"), QStringLiteral("reverb")}}));
        QJsonArray found = out.value(QStringLiteral("devices")).toArray();
        QVERIFY(std::any_of(found.begin(), found.end(), [](const QJsonValue& v) { return v.toObject().value(QStringLiteral("id")) == QStringLiteral("reverb"); }));
        found = structured(call(protocol, QStringLiteral("list_devices"), {{QStringLiteral("role"), QStringLiteral("instrument")}}))
                    .value(QStringLiteral("devices")).toArray();
        QVERIFY(!found.isEmpty());
        for (const QJsonValue& v : found) QVERIFY(v.toObject().value(QStringLiteral("instrument")).toBool());
        out = structured(call(protocol, QStringLiteral("list_devices"), {{QStringLiteral("limit"), 2}}));
        QCOMPARE(out.value(QStringLiteral("devices")).toArray().size(), 2);
        QCOMPARE(out.value(QStringLiteral("nextOffset")).toInt(), 2);
        QVERIFY(call(protocol, QStringLiteral("list_devices"), {{QStringLiteral("type"), QStringLiteral("hardware")}}).value(QStringLiteral("isError")).toBool());

        // Added: last, before another, an instrument replacing the one there.
        const QString delay = structured(call(protocol, QStringLiteral("add_device"),
                                              {{QStringLiteral("track_id"), lead}, {QStringLiteral("device"), QStringLiteral("delay")}}))
                                  .value(QStringLiteral("deviceId")).toString();
        const QString compressor = structured(call(protocol, QStringLiteral("add_device"),
                                                   {{QStringLiteral("track_id"), lead}, {QStringLiteral("device"), QStringLiteral("compressor")},
                                                    {QStringLiteral("before_device_id"), delay}}))
                                       .value(QStringLiteral("deviceId")).toString();
        QCOMPARE(kinds(lead), (QStringList{QStringLiteral("synth"), QStringLiteral("compressor"), QStringLiteral("delay")}));
        out = structured(call(protocol, QStringLiteral("add_device"), {{QStringLiteral("track_id"), lead}, {QStringLiteral("device"), QStringLiteral("sampler")}}));
        QVERIFY(out.value(QStringLiteral("replaced")).toString().contains(QStringLiteral("Synth")));
        QCOMPARE(kinds(lead).first(), QStringLiteral("sampler"));
        // Wrong: an instrument on an audio track, a device that isn't there, a place that isn't.
        QVERIFY(call(protocol, QStringLiteral("add_device"), {{QStringLiteral("track_id"), audio}, {QStringLiteral("device"), QStringLiteral("synth")}})
                    .value(QStringLiteral("isError")).toBool());
        QVERIFY(call(protocol, QStringLiteral("add_device"), {{QStringLiteral("track_id"), audio}, {QStringLiteral("device"), QStringLiteral("nope")}})
                    .value(QStringLiteral("isError")).toBool());
        QVERIFY(call(protocol, QStringLiteral("add_device"), {{QStringLiteral("track_id"), audio}, {QStringLiteral("device"), QStringLiteral("delay")},
                                                             {QStringLiteral("before_device_id"), delay}})
                    .value(QStringLiteral("isError")).toBool());  // (on another track)

        // Into a rack's chain.
        const QString rack = structured(call(protocol, QStringLiteral("add_device"), {{QStringLiteral("track_id"), audio}, {QStringLiteral("device"), QStringLiteral("rack")}}))
                                 .value(QStringLiteral("deviceId")).toString();
        const QString chain = f.editor().addRackChain(audio, rack);
        const QString chorus = structured(call(protocol, QStringLiteral("add_device"), {{QStringLiteral("track_id"), audio}, {QStringLiteral("device"), QStringLiteral("chorus")},
                                                                                      {QStringLiteral("chain_id"), chain}}))
                                   .value(QStringLiteral("deviceId")).toString();
        QCOMPARE(f.project().track(audio).devices[0].chains[0].devices[0].id, chorus);

        // Moved: last on its track, to another track; refused: an instrument off its track.
        call(protocol, QStringLiteral("move_devices"), {{QStringLiteral("track_id"), lead}, {QStringLiteral("device_ids"), QJsonArray{compressor}},
                                                       {QStringLiteral("chain_id"), QString()}});
        QCOMPARE(kinds(lead), (QStringList{QStringLiteral("sampler"), QStringLiteral("delay"), QStringLiteral("compressor")}));
        call(protocol, QStringLiteral("move_devices"), {{QStringLiteral("track_id"), lead}, {QStringLiteral("device_ids"), QJsonArray{delay}},
                                                       {QStringLiteral("to_track_id"), audio}, {QStringLiteral("before_device_id"), rack}});
        QCOMPARE(kinds(audio), (QStringList{QStringLiteral("delay"), kRackKind}));
        QCOMPARE(kinds(lead), (QStringList{QStringLiteral("sampler"), QStringLiteral("compressor")}));
        QVERIFY(call(protocol, QStringLiteral("move_devices"), {{QStringLiteral("track_id"), lead}, {QStringLiteral("device_ids"), QJsonArray{f.project().track(lead).devices[0].id}},
                                                               {QStringLiteral("to_track_id"), audio}})
                    .value(QStringLiteral("isError")).toBool());
        QVERIFY(call(protocol, QStringLiteral("move_devices"), {{QStringLiteral("track_id"), lead}, {QStringLiteral("device_ids"), QJsonArray{compressor}}})
                    .value(QStringLiteral("isError")).toBool());  // (where to?)

        // Removed (a rack with what is in it).
        call(protocol, QStringLiteral("remove_devices"), {{QStringLiteral("track_id"), audio}, {QStringLiteral("device_ids"), QJsonArray{rack}}});
        QCOMPARE(kinds(audio), QStringList{QStringLiteral("delay")});
        QVERIFY(call(protocol, QStringLiteral("remove_devices"), {{QStringLiteral("track_id"), audio}, {QStringLiteral("device_ids"), QJsonArray{QStringLiteral("nope")}}})
                    .value(QStringLiteral("isError")).toBool());

        // Rewound: back to the rack the user added a chain to since, which stays (their work comes first)...
        out = structured(call(protocol, QStringLiteral("rewind_agent_changes"), {{QStringLiteral("all"), true}}));
        QVERIFY(out.value(QStringLiteral("stopped")).toString().contains(QStringLiteral("you changed the same since")));
        QCOMPARE(kinds(audio), QStringList{kRackKind});
        // ...until they undo their chain: then all of it, the tracks' devices as they were.
        f.s().undo();
        QVERIFY(history.rewindAll().isEmpty());
        QCOMPARE(kinds(lead), QStringList{QStringLiteral("synth")});
        QVERIFY(kinds(audio).isEmpty());
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

        // Bodies that can't be told where they end: refused, the connection closed.
        {
            Client gzip(port);
            response = gzip.send("POST /mcp HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: application/json\r\n"
                                 "Transfer-Encoding: gzip\r\nContent-Length: 2\r\n\r\n{}");
            QCOMPARE(response->status, 501);
            QVERIFY(gzip.closed());
            Client both(port);
            response = both.send("POST /mcp HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: application/json\r\n"
                                 "Transfer-Encoding: chunked\r\nContent-Length: 7\r\n\r\n2\r\n{}\r\n0\r\n\r\n");
            QCOMPARE(response->status, 400);
            QVERIFY(both.closed());
        }

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
            QCOMPARE(response->json().value(QStringLiteral("result")).toObject().value(QStringLiteral("tools")).toArray().size(), 26);
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
