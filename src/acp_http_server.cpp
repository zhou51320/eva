#include "acp_http_server.h"

#include "acp_runtime.h"

#include <QHostAddress>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QTcpSocket>
#include <memory>

namespace
{
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
      runtime_(runtime),
      network_(new QNetworkAccessManager(this))
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

    if (method == QByteArrayLiteral("GET") && path == QStringLiteral("/"))
    {
        QJsonObject payload;
        payload.insert(QStringLiteral("service"), QStringLiteral("eva-acp"));
        payload.insert(QStringLiteral("health"), QStringLiteral("/health"));
        payload.insert(QStringLiteral("models"), QStringLiteral("/v1/models"));
        payload.insert(QStringLiteral("backend_state"), QStringLiteral("/api/backend/state"));
        payload.insert(QStringLiteral("backend_load"), QStringLiteral("/api/backend/load"));
        writeJson(socket, 200, QByteArrayLiteral("OK"), payload);
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

    if (path == QStringLiteral("/api/backend/load") || path == QStringLiteral("/v1/chat/completions") || path == QStringLiteral("/v1/models"))
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

void AcpHttpServer::proxyModels(QTcpSocket *socket, const QMap<QByteArray, QByteArray> &headers)
{
    if (!runtime_ || !runtime_->linkModeEnabled())
    {
        writeJson(socket, 200, QByteArrayLiteral("OK"), runtime_ ? runtime_->modelsPayload() : QJsonObject());
        return;
    }

    const QString endpoint = runtime_->modelsEndpoint();
    const QUrl url = QUrl::fromUserInput(endpoint);
    if (!url.isValid())
    {
        QJsonObject payload;
        payload.insert(QStringLiteral("error"), QStringLiteral("Invalid remote models endpoint"));
        payload.insert(QStringLiteral("endpoint"), endpoint);
        writeJson(socket, 502, QByteArrayLiteral("Bad Gateway"), payload);
        return;
    }

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (headers.contains(QByteArrayLiteral("authorization")))
        request.setRawHeader("Authorization", headers.value(QByteArrayLiteral("authorization")));
    else if (!runtime_->configuredApiKey().isEmpty())
        request.setRawHeader("Authorization", QByteArray("Bearer ") + runtime_->configuredApiKey().toUtf8());
    if (headers.contains(QByteArrayLiteral("accept")))
        request.setRawHeader("Accept", headers.value(QByteArrayLiteral("accept")));

    QNetworkReply *reply = network_->get(request);
    forwardReply(socket, reply, false);
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

    const QString endpoint = runtime_->chatCompletionsEndpoint();
    const QUrl url = QUrl::fromUserInput(endpoint);
    if (!url.isValid())
    {
        QJsonObject payload;
        payload.insert(QStringLiteral("error"), QStringLiteral("Invalid chat endpoint"));
        payload.insert(QStringLiteral("endpoint"), endpoint);
        writeJson(socket, 502, QByteArrayLiteral("Bad Gateway"), payload);
        return;
    }

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (headers.contains(QByteArrayLiteral("authorization")))
        request.setRawHeader("Authorization", headers.value(QByteArrayLiteral("authorization")));
    else if (!runtime_->configuredApiKey().isEmpty())
        request.setRawHeader("Authorization", QByteArray("Bearer ") + runtime_->configuredApiKey().toUtf8());
    if (headers.contains(QByteArrayLiteral("accept")))
        request.setRawHeader("Accept", headers.value(QByteArrayLiteral("accept")));

    QNetworkReply *reply = network_->post(request, QJsonDocument(requestObject).toJson(QJsonDocument::Compact));
    forwardReply(socket, reply, streaming);
}

void AcpHttpServer::forwardReply(QTcpSocket *socket, QNetworkReply *reply, bool streaming)
{
    QPointer<QTcpSocket> safeSocket(socket);
    QPointer<QNetworkReply> safeReply(reply);
    auto headersSent = std::make_shared<bool>(false);

    if (safeSocket)
    {
        connect(safeSocket, &QTcpSocket::disconnected, reply, [safeReply]()
        {
            if (safeReply) safeReply->abort();
        });
    }

    if (streaming)
    {
        connect(reply, &QNetworkReply::readyRead, this, [this, safeSocket, safeReply, headersSent]()
        {
            if (!safeSocket || !safeReply) return;
            const int statusCode = safeReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (!*headersSent)
            {
                const QVariant ct = safeReply->header(QNetworkRequest::ContentTypeHeader);
                const QByteArray contentType = ct.isValid() ? ct.toString().toUtf8() : QByteArrayLiteral("text/event-stream; charset=utf-8");
                writeStreamHeaders(safeSocket, statusCode > 0 ? statusCode : 200, QByteArray(), contentType);
                *headersSent = true;
            }
            const QByteArray chunk = safeReply->readAll();
            if (!chunk.isEmpty()) safeSocket->write(chunk);
        });
    }

    connect(reply, &QNetworkReply::finished, this, [this, safeSocket, safeReply, headersSent, streaming]()
    {
        if (!safeReply)
        {
            if (safeSocket) safeSocket->disconnectFromHost();
            return;
        }

        const int statusCode = safeReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QVariant ct = safeReply->header(QNetworkRequest::ContentTypeHeader);
        const QByteArray contentType = ct.isValid() ? ct.toString().toUtf8() : QByteArrayLiteral("application/json; charset=utf-8");
        const QByteArray responseBody = safeReply->readAll();
        const bool hasHttpStatus = statusCode > 0;

        if (streaming && *headersSent)
        {
            if (!responseBody.isEmpty() && safeSocket) safeSocket->write(responseBody);
            if (safeSocket) safeSocket->disconnectFromHost();
            safeReply->deleteLater();
            return;
        }

        if (hasHttpStatus)
        {
            if (safeSocket)
                writeResponse(safeSocket, statusCode, QByteArray(), contentType, responseBody);
        }
        else if (safeReply->error() != QNetworkReply::NoError)
        {
            QJsonObject payload;
            payload.insert(QStringLiteral("error"), safeReply->errorString());
            if (safeSocket)
                writeJson(safeSocket, 502, QByteArrayLiteral("Bad Gateway"), payload);
        }
        else if (safeSocket)
        {
            writeResponse(safeSocket, 200, QByteArrayLiteral("OK"), contentType, responseBody);
        }
        safeReply->deleteLater();
    });
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
