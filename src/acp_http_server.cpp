#include "acp_http_server.h"

#include "acp_runtime.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonDocument>
#include <QTcpSocket>
#include <memory>

namespace
{
QString externalWebRoot()
{
    const QString envRoot = qEnvironmentVariable("EVA_ACP_WEB_ROOT").trimmed();
    if (!envRoot.isEmpty()) return envRoot;
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("acp_web"));
}

QByteArray reasonPhrase(int statusCode, const QByteArray &fallback)
{
    if (!fallback.isEmpty()) return fallback;
    switch (statusCode)
    {
    case 200: return QByteArrayLiteral("OK");
    case 202: return QByteArrayLiteral("Accepted");
    case 204: return QByteArrayLiteral("No Content");
    case 400: return QByteArrayLiteral("Bad Request");
    case 401: return QByteArrayLiteral("Unauthorized");
    case 403: return QByteArrayLiteral("Forbidden");
    case 404: return QByteArrayLiteral("Not Found");
    case 405: return QByteArrayLiteral("Method Not Allowed");
    case 409: return QByteArrayLiteral("Conflict");
    case 429: return QByteArrayLiteral("Too Many Requests");
    case 500: return QByteArrayLiteral("Internal Server Error");
    case 502: return QByteArrayLiteral("Bad Gateway");
    case 503: return QByteArrayLiteral("Service Unavailable");
    case 504: return QByteArrayLiteral("Gateway Timeout");
    default: return QByteArrayLiteral("OK");
    }
}

QString normalizePath(const QString &raw)
{
    const int queryIndex = raw.indexOf(QLatin1Char('?'));
    return queryIndex >= 0 ? raw.left(queryIndex) : raw;
}
}

AcpHttpServer::AcpHttpServer(AcpRuntime *runtime, QObject *parent)
    : QObject(parent),
      runtime_(runtime)
{
    connect(&server_, &QTcpServer::newConnection, this, &AcpHttpServer::handleNewConnection);
}

bool AcpHttpServer::listen(const QString &host, quint16 port, QString *errorMessage)
{
    QHostAddress address;
    if (host == QStringLiteral("localhost"))
    {
        address = QHostAddress::LocalHost;
    }
    else if (!address.setAddress(host))
    {
        if (errorMessage) *errorMessage = QStringLiteral("Invalid bind host: %1").arg(host);
        return false;
    }

    if (!server_.listen(address, port))
    {
        if (errorMessage) *errorMessage = server_.errorString();
        return false;
    }
    return true;
}

void AcpHttpServer::handleNewConnection()
{
    while (server_.hasPendingConnections())
    {
        QTcpSocket *socket = server_.nextPendingConnection();
        auto buffer = std::make_shared<QByteArray>();
        connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer]()
        {
            buffer->append(socket->readAll());
            processBuffer(socket, *buffer);
        });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }
}

void AcpHttpServer::processBuffer(QTcpSocket *socket, QByteArray &buffer)
{
    const int headerEnd = buffer.indexOf("\r\n\r\n");
    if (headerEnd < 0) return;

    const QByteArray headerBytes = buffer.left(headerEnd);
    const QList<QByteArray> headerLines = headerBytes.split('\n');
    if (headerLines.isEmpty())
    {
        writeText(socket, 400, QByteArrayLiteral("Bad Request"), QByteArrayLiteral("Malformed request"));
        return;
    }

    const QByteArray requestLine = headerLines.first().trimmed();
    const QList<QByteArray> requestParts = requestLine.split(' ');
    if (requestParts.size() < 2)
    {
        writeText(socket, 400, QByteArrayLiteral("Bad Request"), QByteArrayLiteral("Malformed request line"));
        return;
    }

    QMap<QByteArray, QByteArray> headers;
    for (int i = 1; i < headerLines.size(); ++i)
    {
        const QByteArray line = headerLines.at(i).trimmed();
        if (line.isEmpty()) continue;
        const int colon = line.indexOf(':');
        if (colon <= 0) continue;
        const QByteArray key = line.left(colon).trimmed().toLower();
        const QByteArray value = line.mid(colon + 1).trimmed();
        headers.insert(key, value);
    }

    bool contentLengthOk = true;
    int contentLength = 0;
    if (headers.contains(QByteArrayLiteral("content-length")))
    {
        contentLength = headers.value(QByteArrayLiteral("content-length")).toInt(&contentLengthOk);
        if (!contentLengthOk || contentLength < 0)
        {
            writeText(socket, 400, QByteArrayLiteral("Bad Request"), QByteArrayLiteral("Invalid Content-Length"));
            return;
        }
    }
    const int bodyStart = headerEnd + 4;
    if (buffer.size() < bodyStart + contentLength) return;

    const QByteArray method = requestParts.at(0).trimmed().toUpper();
    if (method == QByteArrayLiteral("POST") && !headers.contains(QByteArrayLiteral("content-length")) && buffer.size() > bodyStart)
    {
        writeText(socket, 400, QByteArrayLiteral("Bad Request"), QByteArrayLiteral("Missing Content-Length"));
        return;
    }
    const QString path = normalizePath(QString::fromUtf8(requestParts.at(1).trimmed()));
    const QByteArray body = buffer.mid(bodyStart, contentLength);
    handleRequest(socket, method, path, headers, body);
}

void AcpHttpServer::handleRequest(QTcpSocket *socket,
                                  const QByteArray &method,
                                  const QString &path,
                                  const QMap<QByteArray, QByteArray> &headers,
                                  const QByteArray &body)
{
    if (method == QByteArrayLiteral("OPTIONS"))
    {
        writeEmpty(socket, 204, QByteArrayLiteral("No Content"));
        return;
    }

    if (!runtime_)
    {
        QJsonObject payload;
        payload.insert(QStringLiteral("error"), QStringLiteral("Runtime unavailable"));
        writeJson(socket, 500, QByteArrayLiteral("Internal Server Error"), payload);
        return;
    }

    if (method == QByteArrayLiteral("GET") && tryServeStatic(socket, path))
    {
        return;
    }

    if (method == QByteArrayLiteral("GET") && path == QStringLiteral("/health"))
    {
        writeJson(socket, 200, QByteArrayLiteral("OK"), runtime_->healthPayload());
        return;
    }

    if (method == QByteArrayLiteral("GET") && path == QStringLiteral("/v1/models"))
    {
        proxyModels(socket, headers);
        return;
    }

    if (method == QByteArrayLiteral("POST") && path == QStringLiteral("/v1/chat/completions"))
    {
        proxyChatCompletions(socket, headers, body);
        return;
    }

    if (method == QByteArrayLiteral("GET") && path == QStringLiteral("/api/backend/state"))
    {
        writeJson(socket, 200, QByteArrayLiteral("OK"), runtime_->backendStatePayload());
        return;
    }

    if (method == QByteArrayLiteral("POST") && path == QStringLiteral("/api/runtime/reset"))
    {
        QString errorMessage;
        if (!runtime_->resetConversation(&errorMessage))
        {
            QJsonObject payload;
            payload.insert(QStringLiteral("ok"), false);
            payload.insert(QStringLiteral("accepted"), false);
            payload.insert(QStringLiteral("error"), errorMessage);
            payload.insert(QStringLiteral("state"), runtime_->backendStatePayload());
            const QString normalizedError = errorMessage.toLower();
            const bool busy = normalizedError.contains(QStringLiteral("busy")) ||
                              normalizedError.contains(QStringLiteral("blocked")) ||
                              normalizedError.contains(QStringLiteral("推理")) ||
                              normalizedError.contains(QStringLiteral("占用"));
            writeJson(socket, busy ? 409 : 400, busy ? QByteArrayLiteral("Conflict") : QByteArrayLiteral("Bad Request"), payload);
            return;
        }
        QJsonObject payload;
        payload.insert(QStringLiteral("ok"), true);
        payload.insert(QStringLiteral("accepted"), true);
        payload.insert(QStringLiteral("state"), runtime_->backendStatePayload());
        writeJson(socket, 200, QByteArrayLiteral("OK"), payload);
        return;
    }

    if (method == QByteArrayLiteral("POST") && path == QStringLiteral("/api/runtime/stop"))
    {
        QString errorMessage;
        if (!runtime_->stopRuntime(&errorMessage))
        {
            QJsonObject payload;
            payload.insert(QStringLiteral("ok"), false);
            payload.insert(QStringLiteral("accepted"), false);
            payload.insert(QStringLiteral("error"), errorMessage);
            payload.insert(QStringLiteral("state"), runtime_->backendStatePayload());
            writeJson(socket, 400, QByteArrayLiteral("Bad Request"), payload);
            return;
        }
        QJsonObject payload;
        payload.insert(QStringLiteral("ok"), true);
        payload.insert(QStringLiteral("accepted"), true);
        payload.insert(QStringLiteral("state"), runtime_->backendStatePayload());
        writeJson(socket, 202, QByteArrayLiteral("Accepted"), payload);
        return;
    }

    if (method == QByteArrayLiteral("POST") && path == QStringLiteral("/api/backend/load"))
    {
        QJsonObject request;
        if (!body.trimmed().isEmpty())
        {
            QJsonParseError parseError;
            const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
            if (parseError.error != QJsonParseError::NoError || !doc.isObject())
            {
                QJsonObject payload;
                payload.insert(QStringLiteral("error"), QStringLiteral("Invalid JSON body"));
                payload.insert(QStringLiteral("details"), parseError.errorString());
                writeJson(socket, 400, QByteArrayLiteral("Bad Request"), payload);
                return;
            }
            request = doc.object();
        }

        QString errorMessage;
        if (!runtime_->loadBackend(request, &errorMessage))
        {
            QJsonObject payload;
            payload.insert(QStringLiteral("error"), errorMessage);
            writeJson(socket, 400, QByteArrayLiteral("Bad Request"), payload);
            return;
        }

        QJsonObject payload = runtime_->backendStatePayload();
        payload.insert(QStringLiteral("accepted"), true);
        writeJson(socket, 202, QByteArrayLiteral("Accepted"), payload);
        return;
    }

    if (path == QStringLiteral("/api/backend/load") || path == QStringLiteral("/api/runtime/reset") || path == QStringLiteral("/api/runtime/stop") || path == QStringLiteral("/v1/chat/completions") || path == QStringLiteral("/v1/models"))
    {
        QJsonObject payload;
        payload.insert(QStringLiteral("error"), QStringLiteral("Method not allowed"));
        writeJson(socket, 405, QByteArrayLiteral("Method Not Allowed"), payload);
        return;
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("error"), QStringLiteral("Route not found"));
    payload.insert(QStringLiteral("path"), path);
    writeJson(socket, 404, QByteArrayLiteral("Not Found"), payload);
}

bool AcpHttpServer::tryServeStatic(QTcpSocket *socket, const QString &path)
{
    QString resourcePath;
    if (path == QStringLiteral("/") || path == QStringLiteral("/index.html"))
        resourcePath = QStringLiteral(":/acp-web/acp_web/index.html");
    else if (path == QStringLiteral("/styles.css"))
        resourcePath = QStringLiteral(":/acp-web/acp_web/styles.css");
    else if (path == QStringLiteral("/app.js"))
        resourcePath = QStringLiteral(":/acp-web/acp_web/app.js");
    else
        return false;

    const QByteArray body = staticContent(resourcePath);
    if (body.isEmpty())
    {
        QJsonObject payload;
        payload.insert(QStringLiteral("error"), QStringLiteral("Static resource missing"));
        payload.insert(QStringLiteral("path"), path);
        writeJson(socket, 500, QByteArrayLiteral("Internal Server Error"), payload);
        return true;
    }

    writeResponse(socket, 200, QByteArrayLiteral("OK"), contentTypeForPath(path), body);
    return true;
}

QByteArray AcpHttpServer::staticContent(const QString &resourcePath) const
{
    const QString diskRoot = externalWebRoot();
    QString diskPath;
    if (resourcePath.endsWith(QStringLiteral("index.html")))
        diskPath = QDir(diskRoot).filePath(QStringLiteral("index.html"));
    else if (resourcePath.endsWith(QStringLiteral("styles.css")))
        diskPath = QDir(diskRoot).filePath(QStringLiteral("styles.css"));
    else if (resourcePath.endsWith(QStringLiteral("app.js")))
        diskPath = QDir(diskRoot).filePath(QStringLiteral("app.js"));

    if (!diskPath.isEmpty() && QFileInfo::exists(diskPath))
    {
        QFile diskFile(diskPath);
        if (diskFile.open(QIODevice::ReadOnly)) return diskFile.readAll();
    }

    QFile file(resourcePath);
    if (!file.open(QIODevice::ReadOnly)) return QByteArray();
    return file.readAll();
}

QByteArray AcpHttpServer::contentTypeForPath(const QString &path) const
{
    if (path.endsWith(QStringLiteral(".css")))
        return QByteArrayLiteral("text/css; charset=utf-8");
    if (path.endsWith(QStringLiteral(".js")))
        return QByteArrayLiteral("application/javascript; charset=utf-8");
    return QByteArrayLiteral("text/html; charset=utf-8");
}

void AcpHttpServer::proxyModels(QTcpSocket *socket, const QMap<QByteArray, QByteArray> &headers)
{
    Q_UNUSED(headers);
    writeJson(socket, 200, QByteArrayLiteral("OK"), runtime_ ? runtime_->modelsPayload() : QJsonObject());
}

void AcpHttpServer::proxyChatCompletions(QTcpSocket *socket,
                                         const QMap<QByteArray, QByteArray> &headers,
                                         const QByteArray &body)
{
    if (!runtime_)
    {
        QJsonObject payload;
        payload.insert(QStringLiteral("error"), QStringLiteral("Runtime unavailable"));
        writeJson(socket, 500, QByteArrayLiteral("Internal Server Error"), payload);
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
    {
        QJsonObject payload;
        payload.insert(QStringLiteral("error"), QStringLiteral("Invalid JSON body"));
        payload.insert(QStringLiteral("details"), parseError.errorString());
        writeJson(socket, 400, QByteArrayLiteral("Bad Request"), payload);
        return;
    }

    QJsonObject requestObject = doc.object();
    if (!requestObject.contains(QStringLiteral("model")))
    {
        const QString fallbackModel = runtime_->configuredApiModel();
        if (!fallbackModel.isEmpty()) requestObject.insert(QStringLiteral("model"), fallbackModel);
    }
    const bool streaming = requestObject.value(QStringLiteral("stream")).toBool(false);

    const bool bridgeRoute = runtime_->bridgeModeEnabled();
    const bool directRoute = !bridgeRoute && runtime_->directRuntimeEnabled();
    if (bridgeRoute || directRoute)
    {
        QString errorMessage;
        if (!streaming)
        {
            const QJsonObject response = runtime_->chatCompletion(requestObject, &errorMessage);
            if (response.isEmpty())
            {
                QJsonObject payload;
                payload.insert(QStringLiteral("error"), errorMessage.isEmpty() ? QStringLiteral("Runtime chat failed.") : errorMessage);
                writeJson(socket, 502, QByteArrayLiteral("Bad Gateway"), payload);
                return;
            }
            writeJson(socket, 200, QByteArrayLiteral("OK"), response);
            return;
        }

        writeStreamHeaders(socket, 200, QByteArrayLiteral("OK"), QByteArrayLiteral("text/event-stream; charset=utf-8"));
        const QJsonObject response = runtime_->streamChatCompletion(
            requestObject,
            [socket, directRoute](const QString &role, const QString &chunkText)
            {
                if (!socket || chunkText.isEmpty()) return;
                QJsonObject delta;
                if (role == QStringLiteral("think"))
                    delta.insert(QStringLiteral("reasoning"), chunkText);
                else
                    delta.insert(QStringLiteral("content"), chunkText);
                QJsonObject choice;
                choice.insert(QStringLiteral("index"), 0);
                choice.insert(QStringLiteral("delta"), delta);
                choice.insert(QStringLiteral("finish_reason"), QJsonValue());
                QJsonArray choices;
                choices.append(choice);
                QJsonObject chunk;
                chunk.insert(QStringLiteral("id"), directRoute ? QStringLiteral("chatcmpl-runtime") : QStringLiteral("chatcmpl-bridge"));
                chunk.insert(QStringLiteral("object"), QStringLiteral("chat.completion.chunk"));
                chunk.insert(QStringLiteral("created"), static_cast<qint64>(QDateTime::currentSecsSinceEpoch()));
                chunk.insert(QStringLiteral("model"), directRoute ? QStringLiteral("runtime") : QStringLiteral("bridge"));
                chunk.insert(QStringLiteral("choices"), choices);
                socket->write("data: ");
                socket->write(QJsonDocument(chunk).toJson(QJsonDocument::Compact));
                socket->write("\n\n");
            },
            &errorMessage);
        if (response.isEmpty())
        {
            QJsonObject payload;
            payload.insert(QStringLiteral("error"), errorMessage.isEmpty() ? QStringLiteral("Runtime chat failed.") : errorMessage);
            socket->write("data: ");
            socket->write(QJsonDocument(payload).toJson(QJsonDocument::Compact));
            socket->write("\n\n");
            socket->write("data: [DONE]\n\n");
            socket->disconnectFromHost();
            return;
        }
        socket->write("data: [DONE]\n\n");
        socket->disconnectFromHost();
        return;
    }

    Q_UNUSED(headers);
    QJsonObject payload;
    payload.insert(QStringLiteral("error"), QStringLiteral("EVA runtime or bridge is unavailable; /v1/chat/completions will not bypass EVA and call the model directly."));
    payload.insert(QStringLiteral("state"), runtime_->backendStatePayload());
    writeJson(socket, 503, QByteArrayLiteral("Service Unavailable"), payload);
}

void AcpHttpServer::writeJson(QTcpSocket *socket, int statusCode, const QByteArray &reason, const QJsonObject &payload)
{
    writeResponse(socket,
                  statusCode,
                  reason,
                  QByteArrayLiteral("application/json; charset=utf-8"),
                  QJsonDocument(payload).toJson(QJsonDocument::Compact));
}

void AcpHttpServer::writeText(QTcpSocket *socket, int statusCode, const QByteArray &reason, const QByteArray &body)
{
    writeResponse(socket, statusCode, reason, QByteArrayLiteral("text/plain; charset=utf-8"), body);
}

void AcpHttpServer::writeEmpty(QTcpSocket *socket, int statusCode, const QByteArray &reason)
{
    writeResponse(socket, statusCode, reason, QByteArrayLiteral("text/plain; charset=utf-8"), QByteArray());
}

void AcpHttpServer::writeStreamHeaders(QTcpSocket *socket,
                                       int statusCode,
                                       const QByteArray &reason,
                                       const QByteArray &contentType)
{
    const QByteArray finalReason = reasonPhrase(statusCode, reason);
    QByteArray response;
    response.append("HTTP/1.1 ");
    response.append(QByteArray::number(statusCode));
    response.append(' ');
    response.append(finalReason);
    response.append("\r\n");
    response.append("Content-Type: ");
    response.append(contentType);
    response.append("\r\n");
    response.append("Connection: close\r\n");
    response.append("Cache-Control: no-store\r\n");
    response.append("Access-Control-Allow-Origin: *\r\n");
    response.append("Access-Control-Allow-Headers: Content-Type, Authorization\r\n");
    response.append("Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n");
    response.append("\r\n");
    socket->write(response);
}

void AcpHttpServer::writeResponse(QTcpSocket *socket,
                                  int statusCode,
                                  const QByteArray &reason,
                                  const QByteArray &contentType,
                                  const QByteArray &body)
{
    const QByteArray finalReason = reasonPhrase(statusCode, reason);
    QByteArray response;
    response.append("HTTP/1.1 ");
    response.append(QByteArray::number(statusCode));
    response.append(' ');
    response.append(finalReason);
    response.append("\r\n");
    response.append("Content-Type: ");
    response.append(contentType);
    response.append("\r\n");
    response.append("Content-Length: ");
    response.append(QByteArray::number(body.size()));
    response.append("\r\n");
    response.append("Connection: close\r\n");
    response.append("Cache-Control: no-store\r\n");
    response.append("Access-Control-Allow-Origin: *\r\n");
    response.append("Access-Control-Allow-Headers: Content-Type, Authorization\r\n");
    response.append("Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n");
    response.append("\r\n");
    response.append(body);
    socket->write(response);
    socket->disconnectFromHost();
}
