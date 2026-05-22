#pragma once

#include <QObject>
#include <QJsonObject>
#include <QString>

#include "app/app_context.h"
#include "service/backend/xbackend.h"
#include "xconfig.h"

class AcpRuntime : public QObject
{
    Q_OBJECT

  public:
    struct LaunchOptions
    {
        QString bindHost = QStringLiteral("127.0.0.1");
        quint16 bindPort = 19070;
    };

    explicit AcpRuntime(const LaunchOptions &options, QObject *parent = nullptr);

    bool initialize();

    QString bindHost() const;
    quint16 bindPort() const;

    QJsonObject healthPayload() const;
    QJsonObject modelsPayload() const;
    QJsonObject backendStatePayload() const;
    bool loadBackend(const QJsonObject &request, QString *errorMessage);

  private slots:
    void onServerOutput(const QString &chunk);
    void onServerState(const QString &line, SIGNAL_STATE type);
    void onServerReady(const QString &endpoint);
    void onServerStopped();
    void onServerStartFailed(const QString &reason);

  private:
    void loadConfig();
    void saveConfig() const;
    void applyServerSettings();
    QString configPath() const;
    QString backendEndpoint() const;
    QStringList discoverLocalModels() const;
    QJsonObject modelObject(const QString &path) const;
    QString currentModelId() const;
    void appendLogTail(const QString &chunk);
    void refreshBackendProbe();

    LaunchOptions options_;
    AppContext ctx_;
    SETTINGS settings_;
    QString backendPort_ = QStringLiteral(DEFAULT_SERVER_PORT);
    QString backendChoice_ = QStringLiteral("auto");
    QString backendResolved_;
    QString lifecycleState_ = QStringLiteral("stopped");
    QString readyEndpoint_;
    QString lastError_;
    QString lastOutput_;
    bool backendReady_ = false;
    LocalServerManager *serverManager_ = nullptr;
};
