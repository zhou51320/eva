#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>

#include "acp_http_server.h"
#include "acp_runtime.h"
#include "app/app_bootstrap.h"
#include "utils/flowtracer.h"
#include "utils/startuplogger.h"

int main(int argc, char *argv[])
{
    StartupLogger::start();
    FlowTracer::log(FlowChannel::Lifecycle, QStringLiteral("acp: enter main"));
    AppBootstrap::applyEarlyEnv();
    AppBootstrap::applyLinuxRuntimeEnv();

    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("eva_acp"));
    app.setOrganizationName(QStringLiteral("eva"));

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
    return app.exec();
}
