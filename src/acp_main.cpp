#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QTextStream>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>

#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <unistd.h>
#endif

#include "acp_http_server.h"
#include "acp_runtime.h"
#include "runtime/runtime_bootstrap.h"
#include "utils/flowtracer.h"
#include "utils/startuplogger.h"
#include "xconfig.h"

namespace
{
QMutex g_acpLogMutex;
QFile *g_acpLogFile = nullptr;
QByteArray g_acpCrashLogPath;

// ACP 是独立的无界面适配器，异常退出时用户通常只能看到进程消失。
// 这里把 Qt 日志、关键生命周期和致命信号统一落到 EVA_TEMP/logs/eva_acp.log，
// 后续定位自动退出问题时只需要让用户提供这个文件。
QString acpLogLevel(QtMsgType type)
{
    switch (type)
    {
    case QtDebugMsg: return QStringLiteral("DEBUG");
    case QtInfoMsg: return QStringLiteral("INFO");
    case QtWarningMsg: return QStringLiteral("WARN");
    case QtCriticalMsg: return QStringLiteral("CRITICAL");
    case QtFatalMsg: return QStringLiteral("FATAL");
    }
    return QStringLiteral("LOG");
}

QString resolveApplicationDir(int argc, char *argv[])
{
    if (argc > 0 && argv && argv[0])
    {
        QFileInfo exe(QString::fromLocal8Bit(argv[0]));
        if (exe.isRelative())
            exe = QFileInfo(QDir::current().absoluteFilePath(exe.filePath()));
        const QString dir = exe.absolutePath();
        if (!dir.isEmpty()) return dir;
    }
    return QDir::currentPath();
}

void rotateAcpLogIfNeeded(const QString &path)
{
    // 防止常驻 Web 控制台把单个日志文件写到过大；只保留最近一份滚动备份。
    const QFileInfo info(path);
    constexpr qint64 maxBytes = 5 * 1024 * 1024;
    if (!info.exists() || info.size() < maxBytes) return;

    const QString rotatedPath = path + QStringLiteral(".1");
    QFile::remove(rotatedPath);
    QFile::rename(path, rotatedPath);
}

void closeAcpLog()
{
    QMutexLocker locker(&g_acpLogMutex);
    if (!g_acpLogFile) return;
    g_acpLogFile->flush();
    g_acpLogFile->close();
    delete g_acpLogFile;
    g_acpLogFile = nullptr;
}

void acpMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    // 消息处理器中不能再调用 qInfo/qWarning，避免递归；直接写文件和标准输出。
    const QString timestamp = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    QString line = QStringLiteral("%1 [%2] %3").arg(timestamp, acpLogLevel(type), message);
    if (context.file && context.line > 0)
        line += QStringLiteral(" (%1:%2)").arg(QString::fromUtf8(context.file), QString::number(context.line));
    line += QLatin1Char('\n');

    {
        QMutexLocker locker(&g_acpLogMutex);
        if (g_acpLogFile && g_acpLogFile->isOpen())
        {
            QTextStream stream(g_acpLogFile);
            stream.setCodec("UTF-8");
            stream << line;
            stream.flush();
            g_acpLogFile->flush();
        }
    }

    const QByteArray local = line.toLocal8Bit();
    FILE *target = (type == QtDebugMsg || type == QtInfoMsg) ? stdout : stderr;
    fwrite(local.constData(), 1, static_cast<size_t>(local.size()), target);
    fflush(target);
}

void writeCrashLine(const char *message)
{
#ifdef Q_OS_UNIX
    // 信号处理阶段 Qt 对象状态不可靠，Unix 下使用最小的 open/write 追加致命退出标记。
    if (!g_acpCrashLogPath.isEmpty())
    {
        const int fd = ::open(g_acpCrashLogPath.constData(), O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd >= 0)
        {
            ::write(fd, message, static_cast<size_t>(strlen(message)));
            ::close(fd);
        }
    }
#endif
    fputs(message, stderr);
    fflush(stderr);
}

void acpSignalHandler(int signalNumber)
{
    char buffer[128];
    snprintf(buffer, sizeof(buffer), "\n[acp][fatal] received signal %d\n", signalNumber);
    writeCrashLine(buffer);
    std::_Exit(128 + signalNumber);
}

void acpTerminateHandler()
{
    writeCrashLine("\n[acp][fatal] std::terminate called\n");
    std::_Exit(127);
}

void installAcpDiagnostics(int argc, char *argv[])
{
    // 日志目录固定放在可执行文件同级 EVA_TEMP 下，符合机体持久化约定。
    QString appDir = QCoreApplication::applicationDirPath();
    if (appDir.isEmpty()) appDir = resolveApplicationDir(argc, argv);
    const QString logDir = QDir(appDir).filePath(QStringLiteral(EVA_TEMP_DIR_RELATIVE "/logs"));
    QDir().mkpath(logDir);
    const QString logPath = QDir(logDir).filePath(QStringLiteral("eva_acp.log"));
    rotateAcpLogIfNeeded(logPath);

    {
        QMutexLocker locker(&g_acpLogMutex);
        g_acpLogFile = new QFile(logPath);
        if (!g_acpLogFile->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        {
            delete g_acpLogFile;
            g_acpLogFile = nullptr;
        }
    }
    g_acpCrashLogPath = QFile::encodeName(logPath);
    qInstallMessageHandler(acpMessageHandler);
    std::set_terminate(acpTerminateHandler);
    std::signal(SIGABRT, acpSignalHandler);
    std::signal(SIGSEGV, acpSignalHandler);
    std::signal(SIGILL, acpSignalHandler);
    std::signal(SIGFPE, acpSignalHandler);
    std::atexit(closeAcpLog);

    qInfo().noquote() << QStringLiteral("[acp] diagnostic log: %1").arg(logPath);
}
} // namespace

int main(int argc, char *argv[])
{
    RuntimeBootstrap::applyProcessEnvironment();

    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("eva_acp"));
    app.setOrganizationName(QStringLiteral("eva"));
    installAcpDiagnostics(argc, argv);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, []()
    {
        qInfo().noquote() << QStringLiteral("[acp] aboutToQuit");
    });

    StartupLogger::start();
    FlowTracer::log(FlowChannel::Lifecycle, QStringLiteral("acp: enter main"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("EVA ACP local adapter"));
    parser.addHelpOption();
    parser.addOption(QCommandLineOption(QStringList() << QStringLiteral("host"),
                                        QStringLiteral("Bind host for the ACP HTTP server."),
                                        QStringLiteral("host"),
                                        QStringLiteral("127.0.0.1")));
    parser.addOption(QCommandLineOption(QStringList() << QStringLiteral("port"),
                                        QStringLiteral("Bind port for the ACP HTTP server."),
                                        QStringLiteral("port"),
                                        QStringLiteral("19070")));
    parser.process(app);

    bool ok = false;
    const quint16 bindPort = parser.value(QStringLiteral("port")).toUShort(&ok);
    if (!ok || bindPort == 0)
    {
        qCritical().noquote() << QStringLiteral("Invalid ACP port:") << parser.value(QStringLiteral("port"));
        return 2;
    }

    AcpRuntime::LaunchOptions options;
    options.bindHost = parser.value(QStringLiteral("host")).trimmed();
    if (options.bindHost.isEmpty()) options.bindHost = QStringLiteral("127.0.0.1");
    options.bindPort = bindPort;

    AcpRuntime runtime(options);
    if (!runtime.initialize())
    {
        qCritical().noquote() << QStringLiteral("Failed to initialize ACP runtime.");
        return 3;
    }

    AcpHttpServer server(&runtime);
    QString errorMessage;
    if (!server.listen(options.bindHost, options.bindPort, &errorMessage))
    {
        qCritical().noquote() << QStringLiteral("Failed to listen on")
                              << options.bindHost << QLatin1Char(':') << options.bindPort
                              << errorMessage;
        return 4;
    }

    qInfo().noquote() << QStringLiteral("[acp] listening on http://%1:%2")
                             .arg(options.bindHost)
                             .arg(options.bindPort);
    const int exitCode = app.exec();
    qInfo().noquote() << QStringLiteral("[acp] event loop exited: %1").arg(exitCode);
    return exitCode;
}
