#pragma once

#include <QObject>
#include <QByteArray>
#include <QJsonObject>
#include <QMap>
#include <QTcpServer>

class QTcpSocket;
class AcpRuntime;

class AcpHttpServer : public QObject
{
    Q_OBJECT

  public:
    explicit AcpHttpServer(AcpRuntime *runtime, QObject *parent = nullptr);

    bool listen(const QString &host, quint16 port, QString *errorMessage);

  private:
    void handleNewConnection();
    void processBuffer(QTcpSocket *socket, QByteArray &buffer);
    void handleRequest(QTcpSocket *socket,
                       const QByteArray &method,
                       const QString &path,
                       const QMap<QByteArray, QByteArray> &headers,
                       const QByteArray &body);
    bool tryServeStatic(QTcpSocket *socket, const QString &path);
    QByteArray staticContent(const QString &resourcePath) const;
    QByteArray contentTypeForPath(const QString &path) const;
    void proxyModels(QTcpSocket *socket, const QMap<QByteArray, QByteArray> &headers);
    void proxyChatCompletions(QTcpSocket *socket,
                              const QMap<QByteArray, QByteArray> &headers,
                              const QByteArray &body);
    void writeJson(QTcpSocket *socket, int statusCode, const QByteArray &reason, const QJsonObject &payload);
    void writeText(QTcpSocket *socket, int statusCode, const QByteArray &reason, const QByteArray &body);
    void writeEmpty(QTcpSocket *socket, int statusCode, const QByteArray &reason);
    void writeResponse(QTcpSocket *socket,
                       int statusCode,
                       const QByteArray &reason,
                       const QByteArray &contentType,
                       const QByteArray &body);
    void writeStreamHeaders(QTcpSocket *socket, int statusCode, const QByteArray &reason, const QByteArray &contentType);

    AcpRuntime *runtime_ = nullptr;
    QTcpServer server_;
};
