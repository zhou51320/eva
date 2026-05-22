#include "acp_runtime.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonArray>
#include <QSettings>
#include <algorithm>

#include "app/app_bootstrap.h"
#include "app/config_migrator.h"
#include "app/default_model_finder.h"
#include "utils/devicemanager.h"
#include "utils/flowtracer.h"
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
}

AcpRuntime::AcpRuntime(const LaunchOptions &options, QObject *parent)
    : QObject(parent),
      options_(options),
      serverManager_(new LocalServerManager(this, QCoreApplication::applicationDirPath()))
{
    connect(serverManager_, &LocalServerManager::serverOutput, this, &AcpRuntime::onServerOutput);
    connect(serverManager_, &LocalServerManager::serverState, this, &AcpRuntime::onServerState);
    connect(serverManager_, &LocalServerManager::serverReady, this, &AcpRuntime::onServerReady);
    connect(serverManager_, &LocalServerManager::serverStopped, this, &AcpRuntime::onServerStopped);
    connect(serverManager_, &LocalServerManager::serverStartFailed, this, &AcpRuntime::onServerStartFailed);
}

bool AcpRuntime::initialize()
{
    ctx_ = AppBootstrap::buildContext();
    AppBootstrap::ensureTempDir(ctx_);
    AppBootstrap::ensureDefaultConfig(ctx_);
    loadConfig();
    refreshBackendProbe();
    applyServerSettings();
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
    QString health = QStringLiteral("ok");
    if (lifecycleState_ == QStringLiteral("error"))
        health = QStringLiteral("degraded");
    else if (lifecycleState_ == QStringLiteral("starting"))
        health = QStringLiteral("starting");
    payload.insert(QStringLiteral("status"), health);
    payload.insert(QStringLiteral("service"), QStringLiteral("eva-acp"));
    payload.insert(QStringLiteral("bind_host"), options_.bindHost);
    payload.insert(QStringLiteral("bind_port"), static_cast<int>(options_.bindPort));
    payload.insert(QStringLiteral("app_dir"), ctx_.appDir);
    payload.insert(QStringLiteral("config_path"), configPath());
    payload.insert(QStringLiteral("backend_state"), lifecycleState_);
    payload.insert(QStringLiteral("backend_ready"), backendReady_);
    return payload;
}

QJsonObject AcpRuntime::modelsPayload() const
{
    QJsonArray data;
    const QStringList models = discoverLocalModels();
    for (const QString &path : models)
    {
        data.append(modelObject(path));
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("object"), QStringLiteral("list"));
    payload.insert(QStringLiteral("data"), data);
    return payload;
}

QJsonObject AcpRuntime::backendStatePayload() const
{
    QJsonObject payload;
    payload.insert(QStringLiteral("state"), lifecycleState_);
    payload.insert(QStringLiteral("ready"), backendReady_);
    payload.insert(QStringLiteral("endpoint"), backendEndpoint());
    payload.insert(QStringLiteral("current_model"), currentModelId());
    payload.insert(QStringLiteral("current_model_path"), settings_.modelpath);
    payload.insert(QStringLiteral("backend_choice"), backendChoice_);
    payload.insert(QStringLiteral("backend_resolved"), backendResolved_);
    payload.insert(QStringLiteral("available_backends"), QJsonArray::fromStringList(DeviceManager::availableBackends()));
    payload.insert(QStringLiteral("backend_program_path"), DeviceManager::programPath(QStringLiteral("llama-server-main")));
    payload.insert(QStringLiteral("server_running"), serverManager_ && serverManager_->isRunning());
    payload.insert(QStringLiteral("port"), backendPort_);
    payload.insert(QStringLiteral("nctx"), settings_.nctx);
    payload.insert(QStringLiteral("ngl"), settings_.ngl);
    payload.insert(QStringLiteral("nthread"), settings_.nthread);
    payload.insert(QStringLiteral("parallel"), settings_.hid_parallel);
    payload.insert(QStringLiteral("mmproj_path"), settings_.mmprojpath);
    payload.insert(QStringLiteral("lora_path"), settings_.lorapath);
    payload.insert(QStringLiteral("last_error"), lastError_);
    payload.insert(QStringLiteral("last_output_tail"), lastOutput_);
    return payload;
}

bool AcpRuntime::loadBackend(const QJsonObject &request, QString *errorMessage)
{
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

    saveConfig();
    applyServerSettings();

    backendReady_ = false;
    readyEndpoint_.clear();
    lastError_.clear();
    lifecycleState_ = QStringLiteral("starting");
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
}

void AcpRuntime::onServerStartFailed(const QString &reason)
{
    backendReady_ = false;
    lifecycleState_ = QStringLiteral("error");
    readyEndpoint_.clear();
    lastError_ = trimTail(reason, 2048);
}

void AcpRuntime::loadConfig()
{
    QSettings settings(configPath(), QSettings::IniFormat);
    settings.setIniCodec("utf-8");
    ConfigMigrator::migrate(settings);

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

    if (settings_.modelpath.isEmpty())
    {
        const DefaultModelPaths paths = DefaultModelFinder::discover(ctx_.modelsDir);
        settings_.modelpath = paths.llmModel;
    }
}

void AcpRuntime::saveConfig() const
{
    QSettings settings(configPath(), QSettings::IniFormat);
    settings.setIniCodec("utf-8");
    settings.setValue(QStringLiteral("ui_mode"), 0);
    settings.setValue(QStringLiteral("modelpath"), settings_.modelpath);
    settings.setValue(QStringLiteral("mmprojpath"), settings_.mmprojpath);
    settings.setValue(QStringLiteral("lorapath"), settings_.lorapath);
    settings.setValue(QStringLiteral("ngl"), settings_.ngl);
    settings.setValue(QStringLiteral("nthread"), settings_.nthread);
    settings.setValue(QStringLiteral("nctx"), settings_.nctx);
    settings.setValue(QStringLiteral("hid_batch"), settings_.hid_batch);
    settings.setValue(QStringLiteral("hid_parallel"), settings_.hid_parallel);
    settings.setValue(QStringLiteral("hid_use_mmap"), settings_.hid_use_mmap);
    settings.setValue(QStringLiteral("hid_use_mlock"), settings_.hid_use_mlock);
    settings.setValue(QStringLiteral("hid_flash_attn"), settings_.hid_flash_attn);
    settings.setValue(QStringLiteral("reasoning_effort"), settings_.reasoning_effort);
    settings.setValue(QStringLiteral("port"), backendPort_);
    settings.setValue(QStringLiteral("device_backend"), backendChoice_);
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
    if (!settings_.modelpath.isEmpty())
    {
        const QString current = canonicalPath(settings_.modelpath);
        if (!current.isEmpty() && !models.contains(current)) models.append(current);
    }
    models.removeDuplicates();
    std::sort(models.begin(), models.end(), [](const QString &left, const QString &right)
    {
        return left.toLower() < right.toLower();
    });
    return models;
}

QJsonObject AcpRuntime::modelObject(const QString &path) const
{
    QFileInfo info(path);
    const QString canonical = canonicalPath(path);
    const QString relative = ctx_.modelsDir.isEmpty() ? info.fileName() : QDir(ctx_.modelsDir).relativeFilePath(canonical);
    QJsonObject model;
    model.insert(QStringLiteral("id"), relative.isEmpty() ? info.fileName() : relative);
    model.insert(QStringLiteral("object"), QStringLiteral("model"));
    model.insert(QStringLiteral("created"), static_cast<qint64>(info.lastModified().toSecsSinceEpoch()));
    model.insert(QStringLiteral("owned_by"), QStringLiteral("eva-acp"));
    model.insert(QStringLiteral("path"), canonical);
    model.insert(QStringLiteral("current"), canonical == canonicalPath(settings_.modelpath));
    return model;
}

QString AcpRuntime::currentModelId() const
{
    if (settings_.modelpath.isEmpty()) return QString();
    const QFileInfo info(settings_.modelpath);
    const QString canonical = canonicalPath(settings_.modelpath);
    const QString relative = ctx_.modelsDir.isEmpty() ? info.fileName() : QDir(ctx_.modelsDir).relativeFilePath(canonical);
    return relative.isEmpty() ? info.fileName() : relative;
}

void AcpRuntime::appendLogTail(const QString &chunk)
{
    if (chunk.isEmpty()) return;
    lastOutput_.append(chunk);
    lastOutput_ = trimTail(lastOutput_, 4096);
}

void AcpRuntime::refreshBackendProbe()
{
    backendResolved_ = DeviceManager::effectiveBackend();
    DeviceManager::programPath(QStringLiteral("llama-server-main"));
    const QString lastResolved = DeviceManager::lastResolvedDeviceFor(QStringLiteral("llama-server-main"));
    if (!lastResolved.isEmpty()) backendResolved_ = lastResolved;
}
