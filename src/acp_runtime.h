#pragma once

#include <QObject>
#include <QJsonObject>
#include <QString>
#include <functional>

#include "acp_bridge_client.h"
#include "app/app_context.h"
#include "runtime/eva_runtime.h"
#include "service/backend/xbackend.h"
#include "xconfig.h"

class NetClient;

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
    bool linkModeEnabled() const;
    bool bridgeModeEnabled() const;
    bool directRuntimeEnabled() const;
    QString configuredApiModel() const;
    bool resetConversation(QString *errorMessage);
    bool stopRuntime(QString *errorMessage);
    QJsonObject chatCompletion(const QJsonObject &request, QString *errorMessage);
    QJsonObject streamChatCompletion(const QJsonObject &request,
                                     const std::function<void(const QString &role, const QString &chunk)> &onChunk,
                                     QString *errorMessage);

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
    QJsonObject localModelObject(const QString &path) const;
    QJsonObject remoteModelObject() const;
    QString currentModelId() const;
    bool isLinkMode() const;
    void appendLogTail(const QString &chunk);
    void refreshBackendProbe();
    void syncRuntimeFromCurrentState();
    QJsonObject runtimeStatePayload() const;

    APIS apis_;
    EVA_MODE uiMode_ = LOCAL_MODE;

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
    bool directRuntimeEnabled_ = true;
    LocalServerManager *serverManager_ = nullptr;
    AcpBridgeClient *bridgeClient_ = nullptr;
    EvaRuntime *runtimeCore_ = nullptr;
    NetClient *netClient_ = nullptr;
};
