#include "acp_runtime.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonArray>
#include <QSettings>
#include <QTimer>
#include <QUrl>
#include <algorithm>

#include "app/config_migrator.h"
#include "app/default_model_finder.h"
#include "runtime/runtime_bootstrap.h"
#include "service/net/net_client.h"
#include "utils/devicemanager.h"
#include "utils/flowtracer.h"
#include "utils/openai_compat.h"
#include "utils/startuplogger.h"

namespace
{
QString canonicalPath(const QString &path)
{
    QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    if (!canonical.isEmpty()) return canonical;
    return info.absoluteFilePath();
}

QString trimTail(const QString &text, int maxChars)
{
    if (text.size() <= maxChars) return text;
    return text.right(maxChars);
}

QString readStringSetting(QSettings &settings, const QString &key, const QString &fallback)
{
    const QString value = settings.value(key, fallback).toString().trimmed();
    return value.isEmpty() ? fallback : value;
}

bool toolEnabledFromSettings(QSettings &settings, const QStringList &enabledTools, const QString &id, const QString &legacyKey)
{
    return enabledTools.contains(id) || settings.value(legacyKey, false).toBool();
}

QJsonObject capabilityPayloadFromConfig(const QString &configPath)
{
    QSettings settings(configPath, QSettings::IniFormat);
    QStringList enabledTools = settings.value(QStringLiteral("enabled_tools")).toStringList();
    enabledTools.removeDuplicates();

    const bool calculator = toolEnabledFromSettings(settings, enabledTools, QStringLiteral("calculator"), QStringLiteral("calculator_checkbox"));
    const bool knowledge = toolEnabledFromSettings(settings, enabledTools, QStringLiteral("knowledge"), QStringLiteral("knowledge_checkbox"));
    const bool controller = toolEnabledFromSettings(settings, enabledTools, QStringLiteral("controller"), QStringLiteral("controller_checkbox"));
    const bool stablediffusion = toolEnabledFromSettings(settings, enabledTools, QStringLiteral("stablediffusion"), QStringLiteral("stablediffusion_checkbox"));
    const bool engineer = toolEnabledFromSettings(settings, enabledTools, QStringLiteral("engineer"), QStringLiteral("engineer_checkbox"));
    const bool mcp = toolEnabledFromSettings(settings, enabledTools, QStringLiteral("mcp"), QStringLiteral("MCPtools_checkbox"));

    auto appendIfMissing = [&enabledTools](const QString &id, bool enabled)
    {
        if (enabled && !enabledTools.contains(id)) enabledTools.append(id);
    };
    appendIfMissing(QStringLiteral("calculator"), calculator);
    appendIfMissing(QStringLiteral("knowledge"), knowledge);
    appendIfMissing(QStringLiteral("controller"), controller);
    appendIfMissing(QStringLiteral("stablediffusion"), stablediffusion);
    appendIfMissing(QStringLiteral("engineer"), engineer);
    appendIfMissing(QStringLiteral("mcp"), mcp);

    const QString ttsModelPath = settings.value(QStringLiteral("ttscpp_modelpath")).toString().trimmed();
    const QString ttsProgramPath = DeviceManager::programPath(QStringLiteral("tts-cli"));

    QJsonObject configuredTools;
    configuredTools.insert(QStringLiteral("calculator"), calculator);
    configuredTools.insert(QStringLiteral("knowledge"), knowledge);
    configuredTools.insert(QStringLiteral("controller"), controller);
    configuredTools.insert(QStringLiteral("stablediffusion"), stablediffusion);
    configuredTools.insert(QStringLiteral("engineer"), engineer);
    configuredTools.insert(QStringLiteral("mcp"), mcp);

    QJsonObject availableTools;
    availableTools.insert(QStringLiteral("calculator"), false);
    availableTools.insert(QStringLiteral("knowledge"), false);
    availableTools.insert(QStringLiteral("controller"), false);
    availableTools.insert(QStringLiteral("stablediffusion"), false);
    availableTools.insert(QStringLiteral("engineer"), false);
    availableTools.insert(QStringLiteral("mcp"), false);

    QJsonObject tts;
    tts.insert(QStringLiteral("model_path"), ttsModelPath);
    tts.insert(QStringLiteral("model_configured"), !ttsModelPath.isEmpty() && QFileInfo::exists(ttsModelPath));
    tts.insert(QStringLiteral("program_path"), ttsProgramPath);
    tts.insert(QStringLiteral("program_available"), !ttsProgramPath.isEmpty() && QFileInfo::exists(ttsProgramPath));

    QJsonObject payload;
    payload.insert(QStringLiteral("chat"), true);
    payload.insert(QStringLiteral("stream"), true);
    payload.insert(QStringLiteral("reset"), true);
    payload.insert(QStringLiteral("stop"), true);
    payload.insert(QStringLiteral("tools"), availableTools);
    payload.insert(QStringLiteral("configured_tools"), configuredTools);
    payload.insert(QStringLiteral("tools_enabled"), false);
    payload.insert(QStringLiteral("enabled_tools"), QJsonArray());
    payload.insert(QStringLiteral("configured_tools_list"), QJsonArray::fromStringList(enabledTools));
    payload.insert(QStringLiteral("tool_call_mode"), settings.value(QStringLiteral("tool_call_mode"), DEFAULT_TOOL_CALL_MODE).toInt());
    payload.insert(QStringLiteral("full_eva_stack"), false);
    payload.insert(QStringLiteral("tool_execution_route"), QStringLiteral("not_attached"));
    payload.insert(QStringLiteral("conversation_owner"), QStringLiteral("acp_runtime"));
    payload.insert(QStringLiteral("message_input_mode"), QStringLiteral("request_messages"));
    payload.insert(QStringLiteral("knowledge"), false);
    payload.insert(QStringLiteral("mcp"), false);
    payload.insert(QStringLiteral("tts"), tts);
    payload.insert(QStringLiteral("tts_related_output"), false);
    return payload;
}

QString normalizeEndpoint(const QString &rawEndpoint)
{
    QString clean = rawEndpoint.trimmed();
    if (clean.isEmpty()) return clean;

    QUrl url = QUrl::fromUserInput(clean);
    if (!url.isValid()) return clean;
    const QString scheme = url.scheme().trimmed().toLower();
    if (scheme.isEmpty())
    {
        const QString host = url.host().trimmed().toLower();
        const bool isLocal = (host == QStringLiteral("127.0.0.1") ||
                              host == QStringLiteral("localhost") ||
                              host == QStringLiteral("::1"));
        url.setScheme(isLocal ? QStringLiteral("http") : QStringLiteral("https"));
    }

    QString path = url.path();
    while (path.endsWith(QLatin1Char('/')) && path.length() > 1) path.chop(1);
    if (path.toLower().endsWith(QStringLiteral("/v1")))
    {
        path.chop(3);
        if (path.isEmpty()) path = QStringLiteral("/");
        url.setPath(path);
    }
    return url.toString(QUrl::FullyDecoded).trimmed();
}
}

AcpRuntime::AcpRuntime(const LaunchOptions &options, QObject *parent)
    : QObject(parent),
      options_(options),
      serverManager_(new LocalServerManager(this, QCoreApplication::applicationDirPath())),
      bridgeClient_(new AcpBridgeClient(this)),
      runtimeCore_(new EvaRuntime(this)),
      netClient_(new NetClient)
{
    connect(serverManager_, &LocalServerManager::serverOutput, this, &AcpRuntime::onServerOutput);
    connect(serverManager_, &LocalServerManager::serverState, this, &AcpRuntime::onServerState);
    connect(serverManager_, &LocalServerManager::serverReady, this, &AcpRuntime::onServerReady);
    connect(serverManager_, &LocalServerManager::serverStopped, this, &AcpRuntime::onServerStopped);
    connect(serverManager_, &LocalServerManager::serverStartFailed, this, &AcpRuntime::onServerStartFailed);
}

bool AcpRuntime::initialize()
{
    ctx_ = RuntimeBootstrap::prepareContext();
    const QString mode = qEnvironmentVariable("EVA_ACP_RUNTIME_MODE").trimmed().toLower();
    directRuntimeEnabled_ = (mode != QStringLiteral("bridge") && mode != QStringLiteral("legacy"));
    QString runtimeError;
    if (directRuntimeEnabled_ && !runtimeCore_->initialize(ctx_, &runtimeError))
    {
        directRuntimeEnabled_ = false;
        lastError_ = runtimeError;
        StartupLogger::log(QStringLiteral("[acp] direct runtime disabled: %1").arg(runtimeError));
    }
    if (directRuntimeEnabled_)
    {
        runtimeCore_->attachNetworkDriver(netClient_, true);
    }
    else if (netClient_)
    {
        delete netClient_;
        netClient_ = nullptr;
    }
    loadConfig();
    refreshBackendProbe();
    applyServerSettings();
    syncRuntimeFromCurrentState();
    StartupLogger::log(QStringLiteral("[acp] runtime initialized"));
    return true;
}

QString AcpRuntime::bindHost() const
{
    return options_.bindHost;
}

quint16 AcpRuntime::bindPort() const
{
    return options_.bindPort;
}

QJsonObject AcpRuntime::healthPayload() const
{
    QJsonObject payload;
    const bool bridgeAvailable = bridgeModeEnabled();
    const bool hasRuntime = directRuntimeEnabled_ && runtimeCore_;
    const RuntimeState runtimeState = hasRuntime ? runtimeCore_->stateSnapshot() : RuntimeState();
    const QString backendState = hasRuntime ? runtimePhaseName(runtimeState.phase) : lifecycleState_;
    const bool backendReady = hasRuntime ? runtimeState.backendReady : backendReady_;
    QString health = QStringLiteral("ok");
    if (hasRuntime && runtimeState.phase == RuntimePhase::Error)
        health = QStringLiteral("degraded");
    else if (hasRuntime && runtimeState.phase == RuntimePhase::Loading)
        health = QStringLiteral("starting");
    else if (!hasRuntime && lifecycleState_ == QStringLiteral("error"))
        health = QStringLiteral("degraded");
    else if (!hasRuntime && lifecycleState_ == QStringLiteral("starting"))
        health = QStringLiteral("starting");
    payload.insert(QStringLiteral("status"), health);
    payload.insert(QStringLiteral("service"), QStringLiteral("eva-acp"));
    payload.insert(QStringLiteral("bind_host"), options_.bindHost);
    payload.insert(QStringLiteral("bind_port"), static_cast<int>(options_.bindPort));
    payload.insert(QStringLiteral("app_dir"), ctx_.appDir);
    payload.insert(QStringLiteral("config_path"), configPath());
    payload.insert(QStringLiteral("backend_state"), backendState);
    payload.insert(QStringLiteral("backend_ready"), backendReady);
    payload.insert(QStringLiteral("state_source"), bridgeAvailable ? QStringLiteral("bridge") : (directRuntimeEnabled_ ? QStringLiteral("direct_runtime") : QStringLiteral("legacy_acp")));
    payload.insert(QStringLiteral("direct_runtime"), directRuntimeEnabled_);
    payload.insert(QStringLiteral("bridge_available"), bridgeAvailable);
    payload.insert(QStringLiteral("chat_route"), bridgeAvailable ? QStringLiteral("eva_bridge") : (directRuntimeEnabled_ ? QStringLiteral("direct_runtime") : QStringLiteral("unavailable")));
    return payload;
}

QJsonObject AcpRuntime::modelsPayload() const
{
    QJsonArray data;
    QString bridgeError;
    const bool bridgeAvailable = bridgeModeEnabled();
    if (bridgeAvailable)
    {
        data = bridgeClient_->listModels(&bridgeError);
        QJsonObject payload;
        payload.insert(QStringLiteral("object"), QStringLiteral("list"));
        payload.insert(QStringLiteral("data"), data);
        payload.insert(QStringLiteral("state_source"), QStringLiteral("bridge"));
        if (!bridgeError.isEmpty()) payload.insert(QStringLiteral("bridge_error"), bridgeError);
        return payload;
    }

    const bool hasRuntime = directRuntimeEnabled_ && runtimeCore_;
    const RuntimeState runtimeState = hasRuntime ? runtimeCore_->stateSnapshot() : RuntimeState();
    const bool linkMode = hasRuntime ? runtimeState.mode == RuntimeMode::Link : isLinkMode();
    if (data.isEmpty())
    {
        if (linkMode)
        {
            const QJsonObject remote = remoteModelObject();
            if (!remote.isEmpty()) data.append(remote);
        }
        else
        {
            const QStringList models = discoverLocalModels();
            for (const QString &path : models)
            {
                data.append(localModelObject(path));
            }
        }
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("object"), QStringLiteral("list"));
    payload.insert(QStringLiteral("data"), data);
    payload.insert(QStringLiteral("state_source"), directRuntimeEnabled_ && runtimeCore_ ? QStringLiteral("direct_runtime") : QStringLiteral("legacy_acp"));
    if (!bridgeError.isEmpty()) payload.insert(QStringLiteral("bridge_error"), bridgeError);
    return payload;
}

QJsonObject AcpRuntime::backendStatePayload() const
{
    QString bridgeError;
    const bool bridgeAvailable = bridgeModeEnabled();
    if (bridgeAvailable)
    {
        QJsonObject bridgeState = bridgeClient_->getState(&bridgeError);
        if (!bridgeState.isEmpty())
        {
            bridgeState.insert(QStringLiteral("chat_route"), QStringLiteral("eva_bridge"));
            bridgeState.insert(QStringLiteral("direct_runtime_available"), directRuntimeEnabled_ && runtimeCore_);
            return bridgeState;
        }

        QJsonObject payload;
        payload.insert(QStringLiteral("mode"), isLinkMode() ? QStringLiteral("link") : QStringLiteral("local"));
        payload.insert(QStringLiteral("state"), QStringLiteral("error"));
        payload.insert(QStringLiteral("ready"), false);
        payload.insert(QStringLiteral("endpoint"), backendEndpoint());
        payload.insert(QStringLiteral("current_model"), currentModelId());
        payload.insert(QStringLiteral("state_source"), QStringLiteral("bridge"));
        payload.insert(QStringLiteral("direct_runtime"), false);
        payload.insert(QStringLiteral("direct_runtime_available"), directRuntimeEnabled_ && runtimeCore_);
        payload.insert(QStringLiteral("bridge_available"), true);
        payload.insert(QStringLiteral("chat_route"), QStringLiteral("eva_bridge"));
        payload.insert(QStringLiteral("last_error"), bridgeError.isEmpty() ? QStringLiteral("Bridge state query failed.") : bridgeError);
        return payload;
    }

    if (directRuntimeEnabled_ && runtimeCore_)
    {
        QJsonObject payload = runtimeStatePayload();
        payload.insert(QStringLiteral("bridge_available"), false);
        payload.insert(QStringLiteral("chat_route"), QStringLiteral("direct_runtime"));
        if (!bridgeError.isEmpty()) payload.insert(QStringLiteral("bridge_error"), bridgeError);
        return payload;
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("mode"), isLinkMode() ? QStringLiteral("link") : QStringLiteral("local"));
    payload.insert(QStringLiteral("state"), lifecycleState_);
    payload.insert(QStringLiteral("ready"), backendReady_);
    payload.insert(QStringLiteral("endpoint"), backendEndpoint());
    payload.insert(QStringLiteral("current_model"), currentModelId());
    payload.insert(QStringLiteral("current_model_path"), isLinkMode() ? QString() : settings_.modelpath);
    payload.insert(QStringLiteral("backend_choice"), isLinkMode() ? QStringLiteral("link") : backendChoice_);
    payload.insert(QStringLiteral("backend_resolved"), isLinkMode() ? QStringLiteral("remote") : backendResolved_);
    payload.insert(QStringLiteral("available_backends"), isLinkMode() ? QJsonArray() : QJsonArray::fromStringList(DeviceManager::availableBackends()));
    payload.insert(QStringLiteral("backend_program_path"), isLinkMode() ? QString() : DeviceManager::programPath(QStringLiteral("llama-server-main")));
    payload.insert(QStringLiteral("server_running"), !isLinkMode() && serverManager_ && serverManager_->isRunning());
    payload.insert(QStringLiteral("state_source"), bridgeError.isEmpty() ? QStringLiteral("legacy_acp") : QStringLiteral("bridge"));
    payload.insert(QStringLiteral("direct_runtime"), false);
    payload.insert(QStringLiteral("bridge_available"), bridgeError.isEmpty() && bridgeClient_ && bridgeClient_->isConnected());
    payload.insert(QStringLiteral("chat_route"), QStringLiteral("unavailable"));
    payload.insert(QStringLiteral("port"), isLinkMode() ? QString() : backendPort_);
    payload.insert(QStringLiteral("nctx"), settings_.nctx);
    payload.insert(QStringLiteral("ngl"), settings_.ngl);
    payload.insert(QStringLiteral("nthread"), settings_.nthread);
    payload.insert(QStringLiteral("parallel"), settings_.hid_parallel);
    payload.insert(QStringLiteral("mmproj_path"), settings_.mmprojpath);
    payload.insert(QStringLiteral("lora_path"), settings_.lorapath);
    payload.insert(QStringLiteral("api_endpoint"), isLinkMode() ? apis_.api_endpoint : QString());
    payload.insert(QStringLiteral("api_model"), isLinkMode() ? apis_.api_model : QString());
    payload.insert(QStringLiteral("last_error"), bridgeError.isEmpty() ? lastError_ : bridgeError);
    payload.insert(QStringLiteral("last_output_tail"), lastOutput_);
    return payload;
}

bool AcpRuntime::loadBackend(const QJsonObject &request, QString *errorMessage)
{
    if (bridgeModeEnabled())
    {
        QJsonObject state = bridgeClient_->applyLoad(request, errorMessage);
        if (!state.isEmpty())
        {
            return true;
        }
        if (errorMessage && errorMessage->isEmpty())
        {
            *errorMessage = QStringLiteral("Bridge load failed.");
        }
        return false;
    }

    const QString requestedMode = request.value(QStringLiteral("mode")).toString().trimmed().toLower();
    const bool wantsLinkMode =
        requestedMode == QStringLiteral("link") ||
        request.contains(QStringLiteral("api_endpoint")) ||
        request.contains(QStringLiteral("endpoint")) ||
        request.contains(QStringLiteral("api_model")) ||
        request.contains(QStringLiteral("api_key"));

    if (wantsLinkMode)
    {
        QString endpoint = apis_.api_endpoint;
        if (request.contains(QStringLiteral("api_endpoint")))
            endpoint = request.value(QStringLiteral("api_endpoint")).toString();
        else if (request.contains(QStringLiteral("endpoint")))
            endpoint = request.value(QStringLiteral("endpoint")).toString();
        endpoint = normalizeEndpoint(endpoint);
        if (endpoint.isEmpty())
        {
            if (errorMessage) *errorMessage = QStringLiteral("Remote api_endpoint is required in link mode.");
            return false;
        }

        const QUrl baseUrl = QUrl::fromUserInput(endpoint);
        if (!baseUrl.isValid())
        {
            if (errorMessage) *errorMessage = QStringLiteral("Invalid remote endpoint: %1").arg(endpoint);
            return false;
        }

        if (request.contains(QStringLiteral("api_key")))
            apis_.api_key = request.value(QStringLiteral("api_key")).toString().trimmed();
        if (request.contains(QStringLiteral("api_model")))
            apis_.api_model = request.value(QStringLiteral("api_model")).toString().trimmed();

        apis_.api_endpoint = endpoint;
        apis_.api_chat_endpoint = OpenAiCompat::chatCompletionsPath(baseUrl);
        apis_.api_completion_endpoint = OpenAiCompat::completionsPath(baseUrl);
        apis_.is_local_backend = false;
        uiMode_ = LINK_MODE;
        lifecycleState_ = QStringLiteral("link");
        backendReady_ = true;
        readyEndpoint_.clear();
        backendResolved_ = QStringLiteral("remote");
        lastError_.clear();
        if (serverManager_ && serverManager_->isRunning())
        {
            serverManager_->stop();
        }
        if (directRuntimeEnabled_ && runtimeCore_)
        {
            RuntimeConnectRemoteCommand command;
            command.endpoint = apis_.api_endpoint;
            command.apiKey = apis_.api_key;
            command.model = apis_.api_model;
            command.sampling = settings_;
            QString runtimeError;
            if (!runtimeCore_->connectRemote(command, &runtimeError))
            {
                if (errorMessage) *errorMessage = runtimeError;
                return false;
            }
        }
        saveConfig();
        return true;
    }

    uiMode_ = LOCAL_MODE;
    apis_.is_local_backend = true;
    if (request.contains(QStringLiteral("model_path")))
    {
        settings_.modelpath = request.value(QStringLiteral("model_path")).toString().trimmed();
    }
    else if (request.contains(QStringLiteral("modelPath")))
    {
        settings_.modelpath = request.value(QStringLiteral("modelPath")).toString().trimmed();
    }

    if (request.contains(QStringLiteral("mmproj_path")))
    {
        settings_.mmprojpath = request.value(QStringLiteral("mmproj_path")).toString().trimmed();
    }
    if (request.contains(QStringLiteral("lora_path")))
    {
        settings_.lorapath = request.value(QStringLiteral("lora_path")).toString().trimmed();
    }
    if (request.contains(QStringLiteral("backend")))
    {
        backendChoice_ = request.value(QStringLiteral("backend")).toString().trimmed().toLower();
    }
    else if (request.contains(QStringLiteral("device_backend")))
    {
        backendChoice_ = request.value(QStringLiteral("device_backend")).toString().trimmed().toLower();
    }
    if (backendChoice_.isEmpty()) backendChoice_ = QStringLiteral("auto");

    if (request.contains(QStringLiteral("port")))
    {
        backendPort_ = request.value(QStringLiteral("port")).toString().trimmed();
    }
    if (backendPort_.isEmpty()) backendPort_ = QStringLiteral(DEFAULT_SERVER_PORT);

    if (request.contains(QStringLiteral("nctx"))) settings_.nctx = request.value(QStringLiteral("nctx")).toInt(settings_.nctx);
    if (request.contains(QStringLiteral("ngl"))) settings_.ngl = request.value(QStringLiteral("ngl")).toInt(settings_.ngl);
    if (request.contains(QStringLiteral("nthread"))) settings_.nthread = request.value(QStringLiteral("nthread")).toInt(settings_.nthread);
    if (request.contains(QStringLiteral("parallel"))) settings_.hid_parallel = request.value(QStringLiteral("parallel")).toInt(settings_.hid_parallel);

    if (settings_.nctx <= 0) settings_.nctx = DEFAULT_NCTX;
    if (settings_.nthread <= 0) settings_.nthread = qMax(1u, std::thread::hardware_concurrency() / 2);
    if (settings_.hid_parallel <= 0) settings_.hid_parallel = DEFAULT_PARALLEL;

    if (settings_.modelpath.isEmpty())
    {
        const DefaultModelPaths paths = DefaultModelFinder::discover(ctx_.modelsDir);
        if (!paths.llmModel.isEmpty()) settings_.modelpath = paths.llmModel;
    }
    if (settings_.modelpath.isEmpty())
    {
        if (errorMessage) *errorMessage = QStringLiteral("No local model is configured.");
        return false;
    }
    if (!QFileInfo::exists(settings_.modelpath))
    {
        if (errorMessage) *errorMessage = QStringLiteral("Configured model path does not exist: %1").arg(settings_.modelpath);
        return false;
    }
    if (!settings_.mmprojpath.isEmpty() && !QFileInfo::exists(settings_.mmprojpath))
    {
        if (errorMessage) *errorMessage = QStringLiteral("Configured mmproj path does not exist: %1").arg(settings_.mmprojpath);
        return false;
    }
    if (!settings_.lorapath.isEmpty() && !QFileInfo::exists(settings_.lorapath))
    {
        if (errorMessage) *errorMessage = QStringLiteral("Configured lora path does not exist: %1").arg(settings_.lorapath);
        return false;
    }

    bool ok = false;
    const quint16 portValue = backendPort_.toUShort(&ok);
    if (!ok || portValue == 0)
    {
        if (errorMessage) *errorMessage = QStringLiteral("Invalid backend port: %1").arg(backendPort_);
        return false;
    }

    DeviceManager::setUserChoice(backendChoice_);
    refreshBackendProbe();
    const QString backendProgram = DeviceManager::programPath(QStringLiteral("llama-server-main"));
    if (backendProgram.isEmpty() || !QFileInfo::exists(backendProgram))
    {
        if (errorMessage) *errorMessage = QStringLiteral("Backend executable is missing for device '%1'.").arg(backendChoice_);
        return false;
    }

    applyServerSettings();
    if (directRuntimeEnabled_ && runtimeCore_)
    {
        RuntimeLoadLocalCommand command;
        command.modelPath = settings_.modelpath;
        command.mmprojPath = settings_.mmprojpath;
        command.loraPath = settings_.lorapath;
        command.backendChoice = backendChoice_;
        command.port = backendPort_;
        command.settings = settings_;
        QString runtimeError;
        if (!runtimeCore_->loadLocal(command, &runtimeError))
        {
            if (errorMessage) *errorMessage = runtimeError;
            return false;
        }
    }
    saveConfig();

    backendReady_ = false;
    readyEndpoint_.clear();
    lastError_.clear();
    lifecycleState_ = QStringLiteral("starting");
    if (directRuntimeEnabled_ && runtimeCore_)
    {
        runtimeCore_->updateBackendStatus(BackendLifecycleState::Starting,
                                          false,
                                          backendEndpoint(),
                                          backendResolved_);
    }
    FlowTracer::log(FlowChannel::Backend,
                    QStringLiteral("acp: ensure backend model=%1 backend=%2 port=%3")
                        .arg(settings_.modelpath, backendChoice_, backendPort_));
    serverManager_->ensureRunning();
    if (lifecycleState_ == QStringLiteral("error") && !lastError_.isEmpty())
    {
        if (errorMessage) *errorMessage = lastError_;
        return false;
    }
    return true;
}

void AcpRuntime::onServerOutput(const QString &chunk)
{
    appendLogTail(chunk);
}

void AcpRuntime::onServerState(const QString &line, SIGNAL_STATE type)
{
    appendLogTail(line + QLatin1Char('\n'));
    if (type == WRONG_SIGNAL)
    {
        lastError_ = trimTail(line, 2048);
        lifecycleState_ = QStringLiteral("error");
    }
}

void AcpRuntime::onServerReady(const QString &endpoint)
{
    backendReady_ = true;
    lifecycleState_ = QStringLiteral("running");
    readyEndpoint_ = endpoint;
    lastError_.clear();
    backendResolved_ = DeviceManager::lastResolvedDeviceFor(QStringLiteral("llama-server-main"));
    if (directRuntimeEnabled_ && runtimeCore_)
    {
        runtimeCore_->updateBackendStatus(BackendLifecycleState::Running,
                                          true,
                                          backendEndpoint(),
                                          backendResolved_);
    }
    FlowTracer::log(FlowChannel::Backend, QStringLiteral("acp: backend ready %1").arg(endpoint));
}

void AcpRuntime::onServerStopped()
{
    backendReady_ = false;
    if (lifecycleState_ != QStringLiteral("error"))
    {
        lifecycleState_ = QStringLiteral("stopped");
    }
    readyEndpoint_.clear();
    if (directRuntimeEnabled_ && runtimeCore_)
    {
        runtimeCore_->updateBackendStatus(BackendLifecycleState::Stopped, false, backendEndpoint(), backendResolved_);
    }
}

void AcpRuntime::onServerStartFailed(const QString &reason)
{
    backendReady_ = false;
    lifecycleState_ = QStringLiteral("error");
    readyEndpoint_.clear();
    lastError_ = trimTail(reason, 2048);
    if (directRuntimeEnabled_ && runtimeCore_)
    {
        runtimeCore_->updateBackendStatus(BackendLifecycleState::Error,
                                          false,
                                          backendEndpoint(),
                                          backendResolved_,
                                          lastError_);
    }
}

void AcpRuntime::loadConfig()
{
    QSettings settings(configPath(), QSettings::IniFormat);
    settings.setIniCodec("utf-8");
    ConfigMigrator::migrate(settings);

    uiMode_ = static_cast<EVA_MODE>(settings.value(QStringLiteral("ui_mode"), 0).toInt());
    settings_.modelpath = settings.value(QStringLiteral("modelpath"), QString()).toString();
    settings_.mmprojpath = settings.value(QStringLiteral("mmprojpath"), QString()).toString();
    settings_.lorapath = settings.value(QStringLiteral("lorapath"), QString()).toString();
    settings_.ngl = settings.value(QStringLiteral("ngl"), DEFAULT_NGL).toInt();
    settings_.nthread = settings.value(QStringLiteral("nthread"), settings_.nthread).toInt();
    settings_.nctx = settings.value(QStringLiteral("nctx"), DEFAULT_NCTX).toInt();
    settings_.hid_batch = settings.value(QStringLiteral("hid_batch"), DEFAULT_BATCH).toInt();
    settings_.hid_parallel = settings.value(QStringLiteral("hid_parallel"), DEFAULT_PARALLEL).toInt();
    settings_.hid_use_mmap = settings.value(QStringLiteral("hid_use_mmap"), DEFAULT_USE_MMAP).toBool();
    settings_.hid_use_mlock = settings.value(QStringLiteral("hid_use_mlock"), DEFAULT_USE_MLOCCK).toBool();
    settings_.hid_flash_attn = settings.value(QStringLiteral("hid_flash_attn"), DEFAULT_FLASH_ATTN).toBool();
    settings_.reasoning_effort = readStringSetting(settings, QStringLiteral("reasoning_effort"), QStringLiteral("auto"));

    apis_.api_endpoint = normalizeEndpoint(settings.value(QStringLiteral("api_endpoint"), QString()).toString());
    apis_.api_key = settings.value(QStringLiteral("api_key"), QString()).toString().trimmed();
    apis_.api_model = settings.value(QStringLiteral("api_model"), QString()).toString().trimmed();
    {
        const QUrl baseUrl = QUrl::fromUserInput(apis_.api_endpoint);
        apis_.api_chat_endpoint = OpenAiCompat::chatCompletionsPath(baseUrl);
        apis_.api_completion_endpoint = OpenAiCompat::completionsPath(baseUrl);
        apis_.is_local_backend = (uiMode_ == LOCAL_MODE);
    }

    backendPort_ = readStringSetting(settings, QStringLiteral("port"), QStringLiteral(DEFAULT_SERVER_PORT));
    backendChoice_ = readStringSetting(settings, QStringLiteral("device_backend"), QStringLiteral("auto")).toLower();

    DeviceManager::clearProgramOverrides();
    settings.beginGroup(QStringLiteral("backend_overrides"));
    const QStringList overrideKeys = settings.childKeys();
    for (const QString &key : overrideKeys)
    {
        const QString overridePath = settings.value(key).toString();
        if (!overridePath.isEmpty()) DeviceManager::setProgramOverride(key, overridePath);
    }
    settings.endGroup();
    DeviceManager::setUserChoice(backendChoice_);

    if (!isLinkMode() && settings_.modelpath.isEmpty())
    {
        const DefaultModelPaths paths = DefaultModelFinder::discover(ctx_.modelsDir);
        settings_.modelpath = paths.llmModel;
    }

    if (isLinkMode())
    {
        lifecycleState_ = QStringLiteral("link");
        backendReady_ = !apis_.api_endpoint.isEmpty();
        backendResolved_ = QStringLiteral("remote");
    }
}

void AcpRuntime::saveConfig() const
{
    QSettings settings(configPath(), QSettings::IniFormat);
    settings.setIniCodec("utf-8");
    const RuntimeState runtimeState = (directRuntimeEnabled_ && runtimeCore_) ? runtimeCore_->stateSnapshot() : RuntimeState();
    const bool hasRuntime = runtimeState.initialized;
    const RuntimeMode mode = hasRuntime ? runtimeState.mode : (isLinkMode() ? RuntimeMode::Link : RuntimeMode::Local);
    const SETTINGS runtimeSettings = hasRuntime ? runtimeState.settings : settings_;
    const APIS runtimeApis = hasRuntime ? runtimeState.apis : apis_;
    settings.setValue(QStringLiteral("ui_mode"), mode == RuntimeMode::Link ? static_cast<int>(LINK_MODE) : static_cast<int>(LOCAL_MODE));
    settings.setValue(QStringLiteral("modelpath"), runtimeSettings.modelpath);
    settings.setValue(QStringLiteral("mmprojpath"), runtimeSettings.mmprojpath);
    settings.setValue(QStringLiteral("lorapath"), runtimeSettings.lorapath);
    settings.setValue(QStringLiteral("ngl"), runtimeSettings.ngl);
    settings.setValue(QStringLiteral("nthread"), runtimeSettings.nthread);
    settings.setValue(QStringLiteral("nctx"), runtimeSettings.nctx);
    settings.setValue(QStringLiteral("hid_batch"), runtimeSettings.hid_batch);
    settings.setValue(QStringLiteral("hid_parallel"), runtimeSettings.hid_parallel);
    settings.setValue(QStringLiteral("hid_use_mmap"), runtimeSettings.hid_use_mmap);
    settings.setValue(QStringLiteral("hid_use_mlock"), runtimeSettings.hid_use_mlock);
    settings.setValue(QStringLiteral("hid_flash_attn"), runtimeSettings.hid_flash_attn);
    settings.setValue(QStringLiteral("reasoning_effort"), runtimeSettings.reasoning_effort);
    settings.setValue(QStringLiteral("port"), backendPort_);
    settings.setValue(QStringLiteral("device_backend"), backendChoice_);
    if (mode == RuntimeMode::Link)
    {
        settings.setValue(QStringLiteral("api_endpoint"), runtimeApis.api_endpoint);
        settings.setValue(QStringLiteral("api_key"), runtimeApis.api_key);
        settings.setValue(QStringLiteral("api_model"), runtimeApis.api_model);
    }
    settings.sync();
}

void AcpRuntime::applyServerSettings()
{
    if (!serverManager_) return;
    serverManager_->setSettings(settings_);
    serverManager_->setHost(QStringLiteral("127.0.0.1"));
    serverManager_->setPort(backendPort_);
    serverManager_->setModelPath(settings_.modelpath);
    serverManager_->setMmprojPath(settings_.mmprojpath);
    serverManager_->setLoraPath(settings_.lorapath);
}

QString AcpRuntime::configPath() const
{
    return QDir(ctx_.tempDir).filePath(QStringLiteral("eva_config.ini"));
}

QString AcpRuntime::backendEndpoint() const
{
    if (directRuntimeEnabled_ && runtimeCore_)
    {
        const RuntimeState state = runtimeCore_->stateSnapshot();
        if (state.mode == RuntimeMode::Link)
            return state.apis.api_endpoint.isEmpty() ? state.endpoint : state.apis.api_endpoint;
        if (!state.endpoint.isEmpty())
            return state.endpoint;
    }
    if (isLinkMode()) return apis_.api_endpoint;
    if (!readyEndpoint_.isEmpty()) return readyEndpoint_;
    return QStringLiteral("http://127.0.0.1:%1").arg(backendPort_);
}

QStringList AcpRuntime::discoverLocalModels() const
{
    QStringList models;
    const QString llmRoot = QDir(ctx_.modelsDir).filePath(QStringLiteral("llm"));
    if (QDir(llmRoot).exists())
    {
        QDirIterator it(llmRoot, QStringList() << QStringLiteral("*.gguf"), QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext())
        {
            models.append(canonicalPath(it.next()));
        }
    }
    if (!settings_.modelpath.isEmpty() && QFileInfo::exists(settings_.modelpath))
    {
        const QString current = canonicalPath(settings_.modelpath);
        if (!current.isEmpty() && !models.contains(current)) models.append(current);
    }
    if (directRuntimeEnabled_ && runtimeCore_)
    {
        const RuntimeState state = runtimeCore_->stateSnapshot();
        QString runtimeModelPath = state.currentModelPath;
        if (runtimeModelPath.isEmpty()) runtimeModelPath = state.settings.modelpath;
        if (!runtimeModelPath.isEmpty() && QFileInfo::exists(runtimeModelPath))
        {
            const QString current = canonicalPath(runtimeModelPath);
            if (!current.isEmpty() && !models.contains(current)) models.append(current);
        }
    }
    models.removeDuplicates();
    std::sort(models.begin(), models.end(), [](const QString &left, const QString &right)
    {
        return left.toLower() < right.toLower();
    });
    return models;
}

QJsonObject AcpRuntime::localModelObject(const QString &path) const
{
    QFileInfo info(path);
    const QString canonical = canonicalPath(path);
    const QString relative = ctx_.modelsDir.isEmpty() ? info.fileName() : QDir(ctx_.modelsDir).relativeFilePath(canonical);
    QString currentPath = settings_.modelpath;
    if (directRuntimeEnabled_ && runtimeCore_)
    {
        const RuntimeState state = runtimeCore_->stateSnapshot();
        currentPath = state.currentModelPath.isEmpty() ? state.settings.modelpath : state.currentModelPath;
    }
    QJsonObject model;
    model.insert(QStringLiteral("id"), relative.isEmpty() ? info.fileName() : relative);
    model.insert(QStringLiteral("object"), QStringLiteral("model"));
    model.insert(QStringLiteral("created"), static_cast<qint64>(info.lastModified().toSecsSinceEpoch()));
    model.insert(QStringLiteral("owned_by"), QStringLiteral("eva-acp"));
    model.insert(QStringLiteral("path"), canonical);
    model.insert(QStringLiteral("current"), !currentPath.isEmpty() && canonical == canonicalPath(currentPath));
    model.insert(QStringLiteral("source"), QStringLiteral("local"));
    return model;
}

QJsonObject AcpRuntime::remoteModelObject() const
{
    QString modelId = apis_.api_model;
    QString endpoint = apis_.api_endpoint;
    if (directRuntimeEnabled_ && runtimeCore_)
    {
        const RuntimeState state = runtimeCore_->stateSnapshot();
        modelId = state.apis.api_model.isEmpty() ? state.currentModel : state.apis.api_model;
        endpoint = state.apis.api_endpoint.isEmpty() ? state.endpoint : state.apis.api_endpoint;
    }
    if (modelId.isEmpty()) return QJsonObject();
    QJsonObject model;
    model.insert(QStringLiteral("id"), modelId);
    model.insert(QStringLiteral("object"), QStringLiteral("model"));
    model.insert(QStringLiteral("owned_by"), QStringLiteral("eva-acp"));
    model.insert(QStringLiteral("current"), true);
    model.insert(QStringLiteral("source"), QStringLiteral("remote"));
    model.insert(QStringLiteral("endpoint"), endpoint);
    return model;
}

QString AcpRuntime::currentModelId() const
{
    if (directRuntimeEnabled_ && runtimeCore_)
    {
        const RuntimeState state = runtimeCore_->stateSnapshot();
        if (state.mode == RuntimeMode::Link)
            return state.apis.api_model.isEmpty() ? state.currentModel : state.apis.api_model;
        const QString runtimeModelPath = state.currentModelPath.isEmpty() ? state.settings.modelpath : state.currentModelPath;
        if (!runtimeModelPath.isEmpty() && QFileInfo::exists(runtimeModelPath))
        {
            const QFileInfo info(runtimeModelPath);
            const QString canonical = canonicalPath(runtimeModelPath);
            const QString relative = ctx_.modelsDir.isEmpty() ? info.fileName() : QDir(ctx_.modelsDir).relativeFilePath(canonical);
            return relative.isEmpty() ? info.fileName() : relative;
        }
    }
    if (isLinkMode()) return apis_.api_model;
    if (settings_.modelpath.isEmpty() || !QFileInfo::exists(settings_.modelpath)) return QString();
    const QFileInfo info(settings_.modelpath);
    const QString canonical = canonicalPath(settings_.modelpath);
    const QString relative = ctx_.modelsDir.isEmpty() ? info.fileName() : QDir(ctx_.modelsDir).relativeFilePath(canonical);
    return relative.isEmpty() ? info.fileName() : relative;
}

bool AcpRuntime::linkModeEnabled() const
{
    if (directRuntimeEnabled_ && runtimeCore_)
        return runtimeCore_->stateSnapshot().mode == RuntimeMode::Link;
    return isLinkMode();
}

bool AcpRuntime::directRuntimeEnabled() const
{
    return directRuntimeEnabled_ && runtimeCore_;
}

bool AcpRuntime::bridgeModeEnabled() const
{
    if (!bridgeClient_) return false;
    if (bridgeClient_->isConnected()) return true;
    QString errorMessage;
    return const_cast<AcpBridgeClient *>(bridgeClient_)->ensureConnected(300, &errorMessage);
}

QString AcpRuntime::configuredApiModel() const
{
    if (directRuntimeEnabled_ && runtimeCore_)
    {
        const RuntimeState state = runtimeCore_->stateSnapshot();
        return state.mode == RuntimeMode::Link ? state.apis.api_model : QString();
    }
    return isLinkMode() ? apis_.api_model : QString();
}

bool AcpRuntime::resetConversation(QString *errorMessage)
{
    const bool bridgeAvailable = bridgeModeEnabled();
    if (bridgeAvailable)
    {
        if (bridgeClient_ && bridgeClient_->resetConversation(errorMessage)) return true;
        if (errorMessage && errorMessage->isEmpty()) *errorMessage = QStringLiteral("Bridge reset failed.");
        return false;
    }
    if (directRuntimeEnabled_ && runtimeCore_)
    {
        RuntimeResetCommand command;
        command.clearHistory = true;
        return runtimeCore_->resetConversation(command, errorMessage);
    }
    if (errorMessage) *errorMessage = QStringLiteral("Bridge reset unavailable.");
    return false;
}

bool AcpRuntime::stopRuntime(QString *errorMessage)
{
    const bool bridgeAvailable = bridgeModeEnabled();
    if (bridgeAvailable)
    {
        if (bridgeClient_ && bridgeClient_->stopRuntime(errorMessage)) return true;
        if (errorMessage && errorMessage->isEmpty()) *errorMessage = QStringLiteral("Bridge stop failed.");
        return false;
    }
    if (directRuntimeEnabled_ && runtimeCore_)
    {
        runtimeCore_->stop();
        return true;
    }
    if (errorMessage) *errorMessage = QStringLiteral("Bridge stop unavailable.");
    return false;
}

QJsonObject AcpRuntime::chatCompletion(const QJsonObject &request, QString *errorMessage)
{
    return streamChatCompletion(request, std::function<void(const QString &, const QString &)>(), errorMessage);
}

QJsonObject AcpRuntime::streamChatCompletion(const QJsonObject &request,
                                             const std::function<void(const QString &role, const QString &chunk)> &onChunk,
                                             QString *errorMessage)
{
    if (bridgeModeEnabled() && bridgeClient_)
    {
        QString text;
        const QJsonArray messages = request.value(QStringLiteral("messages")).toArray();
        for (int i = messages.size() - 1; i >= 0; --i)
        {
            const QJsonObject message = messages.at(i).toObject();
            if (message.value(QStringLiteral("role")).toString() == QStringLiteral("user"))
            {
                text = message.value(QStringLiteral("content")).toString();
                if (!text.isEmpty()) break;
            }
        }
        if (text.trimmed().isEmpty())
        {
            if (errorMessage) *errorMessage = QStringLiteral("Bridge mode currently requires the latest user text.");
            return QJsonObject();
        }

        AcpBridgeClient::ChatResult result;
        if (!bridgeClient_->sendTextStreaming(text, onChunk, &result, errorMessage))
        {
            return QJsonObject();
        }

        QString stateError;
        const QJsonObject liveState = bridgeClient_->getState(&stateError, 1500);
        const QString liveModel = liveState.value(QStringLiteral("current_model")).toString();

        QJsonObject message;
        message.insert(QStringLiteral("role"), QStringLiteral("assistant"));
        message.insert(QStringLiteral("content"), result.assistantText);
        if (!result.reasoningText.isEmpty())
            message.insert(QStringLiteral("reasoning"), result.reasoningText);

        QJsonObject choice;
        choice.insert(QStringLiteral("index"), 0);
        choice.insert(QStringLiteral("message"), message);
        choice.insert(QStringLiteral("finish_reason"), QStringLiteral("stop"));

        QJsonArray choices;
        choices.append(choice);

        QJsonObject response;
        response.insert(QStringLiteral("id"), QStringLiteral("chatcmpl-bridge"));
        response.insert(QStringLiteral("object"), QStringLiteral("chat.completion"));
        response.insert(QStringLiteral("created"), static_cast<qint64>(QDateTime::currentSecsSinceEpoch()));
        response.insert(QStringLiteral("model"), liveModel.isEmpty() ? currentModelId() : liveModel);
        response.insert(QStringLiteral("choices"), choices);
        response.insert(QStringLiteral("eva_route"), QStringLiteral("bridge"));
        response.insert(QStringLiteral("conversation_owner"), QStringLiteral("widget"));
        response.insert(QStringLiteral("message_input_mode"), QStringLiteral("latest_user_text"));
        return response;
    }

    if (directRuntimeEnabled_ && runtimeCore_)
    {
        const RuntimeState runtimeState = runtimeCore_->stateSnapshot();
        const bool linkMode = runtimeState.mode == RuntimeMode::Link;
        const SETTINGS settings = runtimeState.initialized ? runtimeState.settings : settings_;
        APIS runtimeApis = runtimeState.initialized ? runtimeState.apis : apis_;
        QString runtimeEndpoint = runtimeState.endpoint.trimmed();
        if (runtimeEndpoint.isEmpty()) runtimeEndpoint = backendEndpoint();

        if (!linkMode && !runtimeState.backendReady)
        {
            if (errorMessage) *errorMessage = QStringLiteral("Local backend is not ready.");
            return QJsonObject();
        }

        RuntimeSendMessageCommand command;
        command.apis = runtimeApis;
        command.apis.api_endpoint = linkMode && !runtimeApis.api_endpoint.trimmed().isEmpty()
                                        ? runtimeApis.api_endpoint.trimmed()
                                        : runtimeEndpoint;
        if (command.apis.api_model.trimmed().isEmpty())
        {
            command.apis.api_model = currentModelId().isEmpty() ? QStringLiteral("default") : currentModelId();
        }
        const QString requestedModel = request.value(QStringLiteral("model")).toString().trimmed();
        if (!requestedModel.isEmpty()) command.apis.api_model = requestedModel;
        command.apis.is_local_backend = !linkMode;
        if (!linkMode)
        {
            command.apis.api_chat_endpoint = QStringLiteral(CHAT_ENDPOINT);
            command.apis.api_completion_endpoint = QStringLiteral(COMPLETION_ENDPOINT);
        }

        command.endpoint.date_prompt.clear();
        command.endpoint.messagesArray = request.value(QStringLiteral("messages")).toArray();
        command.endpoint.tools = request.value(QStringLiteral("tools")).toArray();
        command.endpoint.tool_call_mode = command.endpoint.tools.isEmpty() ? DEFAULT_TOOL_CALL_MODE : TOOL_CALL_FUNCTION;
        command.endpoint.is_complete_state = false;
        command.endpoint.temp = static_cast<float>(request.value(QStringLiteral("temperature")).toDouble(settings.temp));
        command.endpoint.repeat = settings.repeat;
        command.endpoint.top_k = settings.top_k;
        command.endpoint.top_p = request.value(QStringLiteral("top_p")).toDouble(settings.hid_top_p);
        command.endpoint.n_predict = request.value(QStringLiteral("max_completion_tokens")).toInt(settings.hid_npredict);
        if (command.endpoint.n_predict <= 0)
        {
            command.endpoint.n_predict = request.value(QStringLiteral("max_tokens")).toInt(settings.hid_npredict);
        }
        command.endpoint.reasoning_effort = request.value(QStringLiteral("reasoning_effort")).toString(settings.reasoning_effort);
        const QJsonValue stopValue = request.value(QStringLiteral("stop"));
        if (stopValue.isArray())
        {
            const QJsonArray stops = stopValue.toArray();
            for (const QJsonValue &value : stops)
            {
                if (value.isString()) command.endpoint.stopwords.append(value.toString());
            }
        }
        else if (stopValue.isString())
        {
            command.endpoint.stopwords.append(stopValue.toString());
        }

        const quint64 turnId = static_cast<quint64>(QDateTime::currentMSecsSinceEpoch());
        command.turnId = turnId;
        command.endpoint.turn_id = turnId;
        command.languageFlag = EVA_LANG_ZH;

        QEventLoop loop;
        QString assistantText;
        QString runtimeError;
        bool finished = false;
        QMetaObject::Connection eventConn;
        eventConn = connect(runtimeCore_, &EvaRuntime::runtimeEvent, this, [&](const RuntimeEvent &event)
        {
            if (event.state.activeTurnId != 0 && event.state.activeTurnId != turnId)
            {
                return;
            }
            if (event.type == RuntimeEventType::OutputChunk)
            {
                assistantText += event.text;
                if (onChunk) onChunk(QStringLiteral("assistant"), event.text);
            }
            else if (event.type == RuntimeEventType::Error || event.type == RuntimeEventType::CommandRejected)
            {
                runtimeError = event.error.isEmpty() ? event.text : event.error;
            }
            else if (event.type == RuntimeEventType::TurnFinished)
            {
                finished = true;
                loop.quit();
            }
        });

        QString sendError;
        if (!runtimeCore_->sendMessage(command, &sendError))
        {
            disconnect(eventConn);
            if (errorMessage) *errorMessage = sendError;
            return QJsonObject();
        }

        QTimer timeout;
        timeout.setSingleShot(true);
        connect(&timeout, &QTimer::timeout, &loop, [&]()
        {
            runtimeError = QStringLiteral("Runtime chat timed out.");
            runtimeCore_->stop();
            loop.quit();
        });
        timeout.start(qMax(60000, DEFAULT_NET_IDLE_TIMEOUT_MS * 3));
        loop.exec();
        disconnect(eventConn);

        if (!finished && runtimeError.isEmpty())
        {
            runtimeError = QStringLiteral("Runtime chat ended before completion.");
        }
        if (!runtimeError.isEmpty() && assistantText.isEmpty())
        {
            if (errorMessage) *errorMessage = runtimeError;
            return QJsonObject();
        }

        QJsonObject message;
        message.insert(QStringLiteral("role"), QStringLiteral("assistant"));
        message.insert(QStringLiteral("content"), assistantText);

        QJsonObject choice;
        choice.insert(QStringLiteral("index"), 0);
        choice.insert(QStringLiteral("message"), message);
        choice.insert(QStringLiteral("finish_reason"), runtimeError.isEmpty() ? QStringLiteral("stop") : QStringLiteral("error"));

        QJsonArray choices;
        choices.append(choice);

        QJsonObject response;
        response.insert(QStringLiteral("id"), QStringLiteral("chatcmpl-runtime"));
        response.insert(QStringLiteral("object"), QStringLiteral("chat.completion"));
        response.insert(QStringLiteral("created"), static_cast<qint64>(QDateTime::currentSecsSinceEpoch()));
        response.insert(QStringLiteral("model"), command.apis.api_model);
        response.insert(QStringLiteral("choices"), choices);
        response.insert(QStringLiteral("eva_route"), QStringLiteral("direct_runtime"));
        response.insert(QStringLiteral("conversation_owner"), QStringLiteral("acp_runtime"));
        response.insert(QStringLiteral("message_input_mode"), QStringLiteral("request_messages"));
        return response;
    }

    if (errorMessage) *errorMessage = QStringLiteral("EVA runtime or bridge is unavailable; refusing to bypass EVA and call the model directly.");
    return QJsonObject();
}

bool AcpRuntime::isLinkMode() const
{
    return uiMode_ == LINK_MODE && !apis_.api_endpoint.trimmed().isEmpty();
}

void AcpRuntime::appendLogTail(const QString &chunk)
{
    if (chunk.isEmpty()) return;
    lastOutput_.append(chunk);
    lastOutput_ = trimTail(lastOutput_, 4096);
}

void AcpRuntime::refreshBackendProbe()
{
    if (isLinkMode())
    {
        backendResolved_ = QStringLiteral("remote");
        return;
    }
    backendResolved_ = DeviceManager::effectiveBackend();
    DeviceManager::programPath(QStringLiteral("llama-server-main"));
    const QString lastResolved = DeviceManager::lastResolvedDeviceFor(QStringLiteral("llama-server-main"));
    if (!lastResolved.isEmpty()) backendResolved_ = lastResolved;
}

void AcpRuntime::syncRuntimeFromCurrentState()
{
    if (!directRuntimeEnabled_ || !runtimeCore_) return;
    QString runtimeError;
    if (isLinkMode())
    {
        RuntimeConnectRemoteCommand command;
        command.endpoint = apis_.api_endpoint;
        command.apiKey = apis_.api_key;
        command.model = apis_.api_model;
        command.sampling = settings_;
        if (!runtimeCore_->connectRemote(command, &runtimeError))
        {
            lastError_ = runtimeError;
        }
        return;
    }

    if (settings_.modelpath.isEmpty() || !QFileInfo::exists(settings_.modelpath))
    {
        runtimeCore_->updateBackendStatus(BackendLifecycleState::Stopped,
                                          false,
                                          backendEndpoint(),
                                          backendResolved_,
                                          settings_.modelpath.isEmpty() ? QString() : QStringLiteral("Configured model path does not exist: %1").arg(settings_.modelpath));
        return;
    }

    RuntimeLoadLocalCommand command;
    command.modelPath = settings_.modelpath;
    command.mmprojPath = settings_.mmprojpath;
    command.loraPath = settings_.lorapath;
    command.backendChoice = backendChoice_;
    command.port = backendPort_;
    command.settings = settings_;
    if (!runtimeCore_->loadLocal(command, &runtimeError))
    {
        lastError_ = runtimeError;
    }
    runtimeCore_->updateBackendStatus(backendReady_ ? BackendLifecycleState::Running : BackendLifecycleState::Stopped,
                                      backendReady_,
                                      backendEndpoint(),
                                      backendResolved_,
                                      lastError_);
}

QJsonObject AcpRuntime::runtimeStatePayload() const
{
    const RuntimeState state = runtimeCore_->stateSnapshot();
    const bool runtimeLinkMode = state.mode == RuntimeMode::Link;
    const SETTINGS settings = state.initialized ? state.settings : settings_;
    const APIS stateApis = state.initialized ? state.apis : apis_;
    QJsonObject payload = runtimeStateToJson(state);
    payload.insert(QStringLiteral("state_source"), QStringLiteral("direct_runtime"));
    payload.insert(QStringLiteral("direct_runtime"), true);
    payload.insert(QStringLiteral("state"), runtimePhaseName(state.phase));
    payload.insert(QStringLiteral("ready"), state.backendReady);
    payload.insert(QStringLiteral("current_model"), state.currentModel.isEmpty() ? currentModelId() : state.currentModel);
    payload.insert(QStringLiteral("current_model_path"), runtimeLinkMode ? QString() : (state.currentModelPath.isEmpty() ? settings.modelpath : state.currentModelPath));
    payload.insert(QStringLiteral("backend_choice"), state.backendChoice.isEmpty() ? (runtimeLinkMode ? QStringLiteral("link") : backendChoice_) : state.backendChoice);
    payload.insert(QStringLiteral("backend_resolved"), state.backendResolved.isEmpty() ? (runtimeLinkMode ? QStringLiteral("remote") : backendResolved_) : state.backendResolved);
    payload.insert(QStringLiteral("available_backends"), runtimeLinkMode ? QJsonArray() : QJsonArray::fromStringList(DeviceManager::availableBackends()));
    payload.insert(QStringLiteral("backend_program_path"), runtimeLinkMode ? QString() : DeviceManager::programPath(QStringLiteral("llama-server-main")));
    payload.insert(QStringLiteral("server_running"), !runtimeLinkMode && serverManager_ && serverManager_->isRunning());
    payload.insert(QStringLiteral("port"), runtimeLinkMode ? QString() : backendPort_);
    payload.insert(QStringLiteral("nctx"), settings.nctx);
    payload.insert(QStringLiteral("ngl"), settings.ngl);
    payload.insert(QStringLiteral("nthread"), settings.nthread);
    payload.insert(QStringLiteral("parallel"), settings.hid_parallel);
    payload.insert(QStringLiteral("mmproj_path"), settings.mmprojpath);
    payload.insert(QStringLiteral("lora_path"), settings.lorapath);
    payload.insert(QStringLiteral("api_endpoint"), runtimeLinkMode ? stateApis.api_endpoint : QString());
    payload.insert(QStringLiteral("api_model"), runtimeLinkMode ? stateApis.api_model : QString());
    payload.insert(QStringLiteral("capabilities"), capabilityPayloadFromConfig(configPath()));
    payload.insert(QStringLiteral("last_error"), lastError_.isEmpty() ? state.lastError : lastError_);
    payload.insert(QStringLiteral("last_output_tail"), lastOutput_);
    return payload;
}
