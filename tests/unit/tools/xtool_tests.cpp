#include <QApplication>
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest/QtTest>
#include <memory>

#include "../common/TestHarness.h"
#include "xtool.h"

using eva::test::createTestTool;
using eva::test::makeToolCall;
using eva::test::makeUniqueWorkRoot;

void envelopeFromPush(const QString &message, QJsonObject *out)
{
    QVERIFY2(out, "missing output object");
    const int newline = message.indexOf(QLatin1Char('\n'));
    QVERIFY2(newline >= 0, "tool push message does not contain a JSON envelope separator");
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(message.mid(newline + 1).toUtf8(), &err);
    QVERIFY2(err.error == QJsonParseError::NoError, qPrintable(QStringLiteral("invalid tool result envelope JSON: %1").arg(err.errorString())));
    QVERIFY2(doc.isObject(), "tool result envelope is not a JSON object");
    *out = doc.object();
}

void verifyEnvelopeShape(const QJsonObject &envelope)
{
    QVERIFY2(envelope.contains(QStringLiteral("ok")), "envelope missing ok");
    QVERIFY2(envelope.contains(QStringLiteral("summary")), "envelope missing summary");
    QVERIFY2(envelope.value(QStringLiteral("data")).isObject(), "envelope missing data object");
    QVERIFY2(envelope.contains(QStringLiteral("artifacts")), "envelope missing artifacts");
    QVERIFY2(envelope.contains(QStringLiteral("warnings")), "envelope missing warnings");
    QVERIFY2(envelope.contains(QStringLiteral("error")), "envelope missing error");
    QVERIFY2(envelope.contains(QStringLiteral("recovery_hints")), "envelope missing recovery_hints");
}

class XToolCalculatorTest : public QObject
{
    Q_OBJECT

  private slots:
    void calculatorProducesResult();
};

void XToolCalculatorTest::calculatorProducesResult()
{
    QTemporaryDir tempDir;
    QVERIFY2(tempDir.isValid(), "Failed to create temporary directory for calculator test");

    auto tool = createTestTool(tempDir.path(), makeUniqueWorkRoot(tempDir));

    QSignalSpy pushSpy(tool.get(), &xTool::tool2ui_pushover);

    const mcp::json args = mcp::json::object({{"expression", "1 + 2"}}); // NOLINT
    tool->Exec(makeToolCall("calculator", args));

    const bool pushOk = pushSpy.count() > 0 || pushSpy.wait(2000);
    QVERIFY2(pushOk, "calculator tool did not produce a push notification");

    const auto firstPush = pushSpy.takeFirst();
    const QString pushMessage = firstPush.at(0).toString();
    QVERIFY2(pushMessage.contains(QStringLiteral("calculator")),
             "Push message missing tool name for calculator");
    QVERIFY2(pushMessage.contains(QStringLiteral("3")),
             "Calculator result not present in push message");
}

class XToolExecuteCommandTest : public QObject
{
    Q_OBJECT

  private slots:
    void executeCommandSendsTerminalSignals();
    void executeCommandHonorsCwdEnvAndExpectedOutputs();
    void executeCommandReportsNonZeroAndRepeatedFailure();
    void executeCommandReportsTimeoutAndCommandIdentity();
    void executeCommandReportsManualCancel();
    void executeCommandReportsMissingExecutable();
    void executeCommandHandlesNonAsciiOutputAndPath();
    void executeCommandRejectsSymlinkCwdEscape();
};

void XToolExecuteCommandTest::executeCommandSendsTerminalSignals()
{
    QTemporaryDir tempDir;
    QVERIFY2(tempDir.isValid(), "Failed to create temporary directory for execute_command test");

    auto tool = createTestTool(tempDir.path(), makeUniqueWorkRoot(tempDir));

    QSignalSpy startSpy(tool.get(), &xTool::tool2ui_terminalCommandStarted);
    QSignalSpy stdoutSpy(tool.get(), &xTool::tool2ui_terminalStdout);
    QSignalSpy finishedSpy(tool.get(), &xTool::tool2ui_terminalCommandFinished);
    QSignalSpy pushSpy(tool.get(), &xTool::tool2ui_pushover);

    const mcp::json args = mcp::json::object({{"content", "echo EVA_TEST_OUTPUT"}}); // NOLINT
    tool->Exec(makeToolCall("execute_command", args));

    const bool startOk = startSpy.count() > 0 || startSpy.wait(2000);
    QVERIFY2(startOk, "execute_command did not emit the start signal");

    const bool finishedOk = finishedSpy.count() > 0 || finishedSpy.wait(5000);
    QVERIFY2(finishedOk, "execute_command did not finish within timeout");

    if (stdoutSpy.count() == 0)
    {
        stdoutSpy.wait(500);
    }

    const bool pushOk = pushSpy.count() > 0 || pushSpy.wait(2000);
    QVERIFY2(pushOk, "execute_command did not emit final push message");

    const QString pushMessage = pushSpy.takeFirst().at(0).toString();
    QVERIFY2(pushMessage.contains(QStringLiteral("EVA_TEST_OUTPUT")),
             "execute_command push message does not contain command output");
    QJsonObject execEnvelope;
    envelopeFromPush(pushMessage, &execEnvelope);
    verifyEnvelopeShape(execEnvelope);
    QVERIFY2(execEnvelope.value(QStringLiteral("ok")).toBool(), "execute_command envelope should report ok=true");
    QVERIFY2(execEnvelope.value(QStringLiteral("data")).toObject().value(QStringLiteral("legacy_text")).toString().contains(QStringLiteral("EVA_TEST_OUTPUT")),
             "execute_command envelope missing legacy command output");

    const auto finishedArgs = finishedSpy.takeFirst();
    QCOMPARE(finishedArgs.at(0).toInt(), 0);
    QCOMPARE(finishedArgs.at(1).toBool(), false);
}

void XToolExecuteCommandTest::executeCommandHonorsCwdEnvAndExpectedOutputs()
{
    QTemporaryDir tempDir;
    QVERIFY2(tempDir.isValid(), "Failed to create temporary directory for execute_command cwd/env test");
    const QString workRoot = makeUniqueWorkRoot(tempDir);
    QVERIFY2(QDir().mkpath(QDir(workRoot).filePath(QStringLiteral("subdir"))), "Failed to create subdir");

    auto tool = createTestTool(tempDir.path(), workRoot);
    QSignalSpy pushSpy(tool.get(), &xTool::tool2ui_pushover);

    const mcp::json args = mcp::json::object({
        {"content", "printf \"%s\" \"$EVA_TEST_ENV\" > artifact.txt && pwd"},
        {"cwd", "subdir"},
        {"env", mcp::json::object({{"EVA_TEST_ENV", "ENV_OK"}})},
        {"expected_outputs", mcp::json::array({"subdir/artifact.txt"})},
        {"timeout_ms", 5000},
        {"shell", "sh"},
        {"label", "cwd env expected output"}
    });
    tool->Exec(makeToolCall("execute_command", args));

    QVERIFY2(pushSpy.count() > 0 || pushSpy.wait(5000), "execute_command cwd/env test produced no push message");
    QJsonObject envelope;
    envelopeFromPush(pushSpy.takeFirst().at(0).toString(), &envelope);
    verifyEnvelopeShape(envelope);
    QVERIFY2(envelope.value(QStringLiteral("ok")).toBool(), "execute_command cwd/env envelope should report ok=true");
    const QJsonObject data = envelope.value(QStringLiteral("data")).toObject();
    QVERIFY2(data.value(QStringLiteral("stdout")).toString().contains(QStringLiteral("subdir")), "stdout should include cwd path");
    QVERIFY2(data.value(QStringLiteral("cwd")).toString().contains(QStringLiteral("subdir")), "envelope cwd should include requested subdir");
    QVERIFY2(envelope.value(QStringLiteral("artifacts")).toArray().size() == 1, "expected output should be reported as artifact");

    QFile artifact(QDir(workRoot).filePath(QStringLiteral("subdir/artifact.txt")));
    QVERIFY2(artifact.open(QIODevice::ReadOnly | QIODevice::Text), "artifact was not created");
    QCOMPARE(QString::fromUtf8(artifact.readAll()), QStringLiteral("ENV_OK"));
}

void XToolExecuteCommandTest::executeCommandReportsNonZeroAndRepeatedFailure()
{
    QTemporaryDir tempDir;
    QVERIFY2(tempDir.isValid(), "Failed to create temporary directory for execute_command failure test");
    auto tool = createTestTool(tempDir.path(), makeUniqueWorkRoot(tempDir));
    QSignalSpy pushSpy(tool.get(), &xTool::tool2ui_pushover);

    const mcp::json args = mcp::json::object({{"content", "echo FAIL_ON_PURPOSE >&2; exit 7"}, {"shell", "sh"}}); // NOLINT
    tool->Exec(makeToolCall("execute_command", args));
    QVERIFY2(pushSpy.count() > 0 || pushSpy.wait(5000), "first failing command produced no push message");
    QJsonObject first;
    envelopeFromPush(pushSpy.takeFirst().at(0).toString(), &first);
    verifyEnvelopeShape(first);
    QVERIFY2(!first.value(QStringLiteral("ok")).toBool(), "non-zero command should report ok=false");
    QCOMPARE(first.value(QStringLiteral("data")).toObject().value(QStringLiteral("exit_code")).toInt(), 7);
    QVERIFY2(first.value(QStringLiteral("data")).toObject().value(QStringLiteral("stderr")).toString().contains(QStringLiteral("FAIL_ON_PURPOSE")),
             "stderr should be captured separately");
    QVERIFY2(first.value(QStringLiteral("recovery_hints")).toArray().size() >= 1, "failure should include recovery hints");

    tool->Exec(makeToolCall("execute_command", args));
    QVERIFY2(pushSpy.count() > 0 || pushSpy.wait(5000), "second failing command produced no push message");
    QJsonObject second;
    envelopeFromPush(pushSpy.takeFirst().at(0).toString(), &second);
    verifyEnvelopeShape(second);
    QVERIFY2(second.value(QStringLiteral("data")).toObject().value(QStringLiteral("repeated_failed_command")).toBool(),
             "second identical failure should be flagged as repeated_failed_command");
    QVERIFY2(second.value(QStringLiteral("warnings")).toArray().size() >= 1, "repeated failure should include warning");
}

void XToolExecuteCommandTest::executeCommandReportsTimeoutAndCommandIdentity()
{
    QTemporaryDir tempDir;
    QVERIFY2(tempDir.isValid(), "Failed to create temporary directory for execute_command timeout test");
    auto tool = createTestTool(tempDir.path(), makeUniqueWorkRoot(tempDir));
    QSignalSpy pushSpy(tool.get(), &xTool::tool2ui_pushover);
    QSignalSpy stateSpy(tool.get(), &xTool::tool2ui_state);

    const mcp::json args = mcp::json::object({{"content", "sleep 2"}, {"timeout_ms", 1000}, {"shell", "sh"}}); // NOLINT
    tool->Exec(makeToolCall("execute_command", args));

    QVERIFY2(pushSpy.count() > 0 || pushSpy.wait(4000), "timeout command produced no final envelope");
    QJsonObject envelope;
    envelopeFromPush(pushSpy.takeFirst().at(0).toString(), &envelope);
    verifyEnvelopeShape(envelope);
    QVERIFY2(!envelope.value(QStringLiteral("ok")).toBool(), "timeout command should report ok=false");
    const QJsonObject data = envelope.value(QStringLiteral("data")).toObject();
    QVERIFY2(data.value(QStringLiteral("timed_out")).toBool(), "timeout envelope should report timed_out=true");
    QVERIFY2(data.value(QStringLiteral("command_id")).toInt() > 0, "timeout envelope should include command_id");
    QVERIFY2(!data.value(QStringLiteral("cancellation_id")).toString().isEmpty(), "timeout envelope should include cancellation_id");
    QCOMPARE(envelope.value(QStringLiteral("error")).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("timeout"));

    bool sawRunning = false;
    for (const auto &argsVariant : stateSpy)
    {
        if (argsVariant.at(0).toString().contains(QStringLiteral("execute_command running")))
        {
            sawRunning = true;
            break;
        }
    }
    QVERIFY2(sawRunning, "long-running command should emit a running progress state");
}

void XToolExecuteCommandTest::executeCommandReportsManualCancel()
{
    QTemporaryDir tempDir;
    QVERIFY2(tempDir.isValid(), "Failed to create temporary directory for execute_command cancel test");
    auto tool = createTestTool(tempDir.path(), makeUniqueWorkRoot(tempDir));
    QSignalSpy pushSpy(tool.get(), &xTool::tool2ui_pushover);
    QSignalSpy startSpy(tool.get(), &xTool::tool2ui_terminalCommandStarted);

    const mcp::json args = mcp::json::object({{"content", "sleep 5"}, {"timeout_ms", 5000}, {"shell", "sh"}}); // NOLINT
    tool->Exec(makeToolCall("execute_command", args));
    QVERIFY2(startSpy.count() > 0 || startSpy.wait(1000), "cancel command did not start");
    QTimer::singleShot(100, tool.get(), [&tool]() { tool->cancelExecuteCommand(); });

    QVERIFY2(pushSpy.count() > 0 || pushSpy.wait(4000), "cancel command produced no final envelope");
    QJsonObject envelope;
    envelopeFromPush(pushSpy.takeFirst().at(0).toString(), &envelope);
    verifyEnvelopeShape(envelope);
    QVERIFY2(!envelope.value(QStringLiteral("ok")).toBool(), "cancelled command should report ok=false");
    const QJsonObject data = envelope.value(QStringLiteral("data")).toObject();
    QVERIFY2(data.value(QStringLiteral("interrupted")).toBool(), "cancel envelope should report interrupted=true");
    QVERIFY2(!data.value(QStringLiteral("timed_out")).toBool(), "manual cancel should not report timed_out=true");
    QVERIFY2(data.value(QStringLiteral("command_id")).toInt() > 0, "cancel envelope should include command_id");
    QVERIFY2(!data.value(QStringLiteral("cancellation_id")).toString().isEmpty(), "cancel envelope should include cancellation_id");
}

void XToolExecuteCommandTest::executeCommandReportsMissingExecutable()
{
    QTemporaryDir tempDir;
    QVERIFY2(tempDir.isValid(), "Failed to create temporary directory for execute_command missing executable test");
    auto tool = createTestTool(tempDir.path(), makeUniqueWorkRoot(tempDir));
    QSignalSpy pushSpy(tool.get(), &xTool::tool2ui_pushover);

    const mcp::json args = mcp::json::object({{"content", "definitely_missing_eva_command_12345"}, {"shell", "sh"}}); // NOLINT
    tool->Exec(makeToolCall("execute_command", args));
    QVERIFY2(pushSpy.count() > 0 || pushSpy.wait(5000), "missing executable command produced no push message");
    QJsonObject envelope;
    envelopeFromPush(pushSpy.takeFirst().at(0).toString(), &envelope);
    verifyEnvelopeShape(envelope);
    QVERIFY2(!envelope.value(QStringLiteral("ok")).toBool(), "missing executable should report ok=false");
    QVERIFY2(envelope.value(QStringLiteral("data")).toObject().value(QStringLiteral("stderr")).toString().contains(QStringLiteral("definitely_missing_eva_command_12345")),
             "stderr should mention the missing executable");
    QVERIFY2(envelope.value(QStringLiteral("recovery_hints")).toArray().size() >= 1, "missing executable should include recovery hints");
}

void XToolExecuteCommandTest::executeCommandHandlesNonAsciiOutputAndPath()
{
    QTemporaryDir tempDir;
    QVERIFY2(tempDir.isValid(), "Failed to create temporary directory for execute_command non-ascii test");
    const QString workRoot = makeUniqueWorkRoot(tempDir);
    const QString unicodeDir = QStringLiteral("中文 空格");
    QVERIFY2(QDir().mkpath(QDir(workRoot).filePath(unicodeDir)), "Failed to create non-ascii directory");

    auto tool = createTestTool(tempDir.path(), workRoot);
    QSignalSpy pushSpy(tool.get(), &xTool::tool2ui_pushover);

    const QString expected = QStringLiteral("你好 EVA");
    const mcp::json args = mcp::json::object({
        {"content", "printf \"你好 EVA\""},
        {"cwd", unicodeDir.toStdString()},
        {"shell", "sh"}
    });
    tool->Exec(makeToolCall("execute_command", args));
    QVERIFY2(pushSpy.count() > 0 || pushSpy.wait(5000), "non-ascii command produced no push message");
    QJsonObject envelope;
    envelopeFromPush(pushSpy.takeFirst().at(0).toString(), &envelope);
    verifyEnvelopeShape(envelope);
    QVERIFY2(envelope.value(QStringLiteral("ok")).toBool(), "non-ascii command should report ok=true");
    const QJsonObject data = envelope.value(QStringLiteral("data")).toObject();
    QVERIFY2(data.value(QStringLiteral("stdout")).toString().contains(expected), "stdout should preserve non-ascii output");
    QVERIFY2(data.value(QStringLiteral("cwd")).toString().contains(unicodeDir), "cwd should preserve non-ascii path");
}

void XToolExecuteCommandTest::executeCommandRejectsSymlinkCwdEscape()
{
#ifndef Q_OS_WIN
    QTemporaryDir tempDir;
    QVERIFY2(tempDir.isValid(), "Failed to create temporary directory for execute_command symlink test");
    const QString workRoot = makeUniqueWorkRoot(tempDir);
    const QString outsideRoot = QDir(tempDir.path()).filePath(QStringLiteral("outside"));
    QVERIFY2(QDir().mkpath(workRoot), "Failed to create work root");
    QVERIFY2(QDir().mkpath(outsideRoot), "Failed to create outside dir");
    QVERIFY2(QFile::link(outsideRoot, QDir(workRoot).filePath(QStringLiteral("escape_link"))), "Failed to create symlink escape");

    auto tool = createTestTool(tempDir.path(), workRoot);
    QSignalSpy pushSpy(tool.get(), &xTool::tool2ui_pushover);

    const mcp::json args = mcp::json::object({{"content", "pwd"}, {"cwd", "escape_link"}, {"shell", "sh"}}); // NOLINT
    tool->Exec(makeToolCall("execute_command", args));
    QVERIFY2(pushSpy.count() > 0 || pushSpy.wait(5000), "symlink escape command produced no push message");
    QJsonObject envelope;
    envelopeFromPush(pushSpy.takeFirst().at(0).toString(), &envelope);
    verifyEnvelopeShape(envelope);
    QVERIFY2(!envelope.value(QStringLiteral("ok")).toBool(), "symlink cwd escape should report ok=false");
    QCOMPARE(envelope.value(QStringLiteral("error")).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("path"));
#else
    QSKIP("Symlink cwd escape test is Unix-only.");
#endif
}

class XToolPtcTest : public QObject
{
    Q_OBJECT

  private:
    static QString resolvePythonSpec()
    {
        const QStringList candidates = {QStringLiteral("python3"), QStringLiteral("python"), QStringLiteral("py")};
        for (const QString &candidate : candidates)
        {
            const QString exe = QStandardPaths::findExecutable(candidate);
            if (exe.isEmpty()) continue;
            if (candidate == QStringLiteral("py"))
            {
                return QStringLiteral("%1 -3").arg(exe);
            }
            return exe;
        }
        return {};
    }

  private slots:
    void ptcExecutesScript();
    void ptcRejectsInvalidFilename();
};

void XToolPtcTest::ptcExecutesScript()
{
    QTemporaryDir tempDir;
    QVERIFY2(tempDir.isValid(), "Failed to create temporary directory for ptc test");

    const QString workRoot = makeUniqueWorkRoot(tempDir);
    QVERIFY2(QDir().mkpath(workRoot), "Failed to create work root for ptc test");

    auto tool = createTestTool(tempDir.path(), workRoot);
    const QString pythonSpec = resolvePythonSpec();
    if (pythonSpec.isEmpty())
    {
        QSKIP("Python interpreter not available; skipping ptc script execution test");
    }
    tool->pythonExecutable = pythonSpec;

    QSignalSpy pushSpy(tool.get(), &xTool::tool2ui_pushover);
    const QString scriptBody = QStringLiteral(
        "import pathlib\n"
        "print('PTC_OK')\n"
        "print(pathlib.Path('.').resolve())\n");

    tool->Exec(makeToolCall("ptc",
                            mcp::json::object({{"filename", "helper_ptc.py"},
                                               {"workdir", "."},
                                               {"content", scriptBody.toStdString()}})));

    QVERIFY2(pushSpy.count() > 0 || pushSpy.wait(5000), "ptc script test produced no push message");
    const QString message = pushSpy.takeFirst().at(0).toString();
    qInfo() << "ptc output:" << message;
    QVERIFY2(message.contains(QStringLiteral("exit code")), "ptc script output missing exit code");
    QJsonObject ptcEnvelope;
    envelopeFromPush(message, &ptcEnvelope);
    verifyEnvelopeShape(ptcEnvelope);
    QVERIFY2(ptcEnvelope.value(QStringLiteral("ok")).toBool(), "ptc envelope should report ok=true");
    QVERIFY2(ptcEnvelope.value(QStringLiteral("data")).toObject().value(QStringLiteral("legacy_text")).toString().contains(QStringLiteral("PTC_OK")),
             "ptc envelope missing legacy stdout");

    QFile saved(QDir(workRoot).filePath(QStringLiteral("ptc_temp/helper_ptc.py")));
    QVERIFY2(saved.exists(), "ptc script was not persisted under ptc_temp");
}

void XToolPtcTest::ptcRejectsInvalidFilename()
{
    QTemporaryDir tempDir;
    QVERIFY2(tempDir.isValid(), "Failed to create temporary directory for ptc guard test");

    auto tool = createTestTool(tempDir.path(), makeUniqueWorkRoot(tempDir));
    QSignalSpy pushSpy(tool.get(), &xTool::tool2ui_pushover);

    tool->Exec(makeToolCall("ptc",
                            mcp::json::object({{"filename", "../hack.py"},
                                               {"workdir", "."},
                                               {"content", "print('oops')"}})));

    QVERIFY2(pushSpy.count() > 0 || pushSpy.wait(2000), "ptc guard test produced no push message");
    const QString message = pushSpy.takeFirst().at(0).toString();
    QVERIFY2(message.contains(QStringLiteral("filename")), "ptc guard did not report filename validation error");
    QJsonObject guardEnvelope;
    envelopeFromPush(message, &guardEnvelope);
    verifyEnvelopeShape(guardEnvelope);
    QVERIFY2(!guardEnvelope.value(QStringLiteral("ok")).toBool(), "ptc guard envelope should report ok=false");
    QVERIFY2(guardEnvelope.value(QStringLiteral("error")).toObject().value(QStringLiteral("type")).toString() != QStringLiteral(""),
             "ptc guard envelope missing error type");
    QVERIFY2(guardEnvelope.value(QStringLiteral("recovery_hints")).toArray().size() >= 0,
             "ptc guard envelope missing recovery hints array");
}

class XToolMcpFlowTest : public QObject
{
    Q_OBJECT

  private slots:
    void mcpToolCallRoundtrip();
};

void XToolMcpFlowTest::mcpToolCallRoundtrip()
{
    QTemporaryDir tempDir;
    QVERIFY2(tempDir.isValid(), "Failed to create temporary directory for MCP flow test");

    auto tool = createTestTool(tempDir.path(), makeUniqueWorkRoot(tempDir));

    QSignalSpy pushSpy(tool.get(), &xTool::tool2ui_pushover);

    bool callEmitted = false;
    bool resultHandled = false;
    quint64 invocationId = 0;
    QString emittedName;
    QString emittedArgs;

    QObject::connect(tool.get(), &xTool::tool2mcp_toolcall, tool.get(),
                     [&](quint64 id, const QString &name, const QString &args) {
                         callEmitted = true;
                         invocationId = id;
                         emittedName = name;
                         emittedArgs = args;
                         tool->recv_callTool_over(id, QStringLiteral("mcp-test-result"));
                         resultHandled = true;
                     });

    const mcp::json args = mcp::json::object({{"input", "value"}}); // NOLINT
    tool->Exec(makeToolCall("service@tool_name", args));

    QTRY_VERIFY_WITH_TIMEOUT(callEmitted, 2000);
    QTRY_VERIFY_WITH_TIMEOUT(resultHandled, 2000);
    QVERIFY2(invocationId > 0, "MCP invocation id should be positive");
    QCOMPARE(emittedName, QStringLiteral("service@tool_name"));
    QVERIFY2(emittedArgs.contains(QStringLiteral("\"input\"")),
             "MCP call arguments missing expected payload");

    const bool pushOk = pushSpy.count() > 0 || pushSpy.wait(2000);
    QVERIFY2(pushOk, "MCP tool call result did not trigger a push message");

    const QString pushMessage = pushSpy.takeFirst().at(0).toString();
    QVERIFY2(pushMessage.contains(QStringLiteral("mcp-test-result")),
             "MCP result push message missing tool response content");
}

class XToolKnowledgeTest : public QObject
{
    Q_OBJECT

  private slots:
    void knowledgeWithoutEmbedding();
};

void XToolKnowledgeTest::knowledgeWithoutEmbedding()
{
    QTemporaryDir tempDir;
    QVERIFY2(tempDir.isValid(), "Failed to create temporary directory for knowledge test");

    auto tool = createTestTool(tempDir.path(), makeUniqueWorkRoot(tempDir));
    QSignalSpy pushSpy(tool.get(), &xTool::tool2ui_pushover);

    const mcp::json args = mcp::json::object({{"content", "What is EVA?"}});
    tool->Exec(makeToolCall("knowledge", args));

    const bool pushOk = pushSpy.count() > 0 || pushSpy.wait(2000);
    QVERIFY2(pushOk, "knowledge tool did not produce a push notification");

    const QString pushMessage = pushSpy.takeFirst().at(0).toString();
    QVERIFY2(pushMessage.contains(QStringLiteral("embed knowledge into the knowledge base first")),
             "knowledge tool fallback guidance missing");
}

class XToolStableDiffusionTest : public QObject
{
    Q_OBJECT

  private slots:
    void stableDiffusionDeliversResult();
};

void XToolStableDiffusionTest::stableDiffusionDeliversResult()
{
    QTemporaryDir tempDir;
    QVERIFY2(tempDir.isValid(), "Failed to create temporary directory for stablediffusion test");

    auto tool = createTestTool(tempDir.path(), makeUniqueWorkRoot(tempDir));
    QSignalSpy pushSpy(tool.get(), &xTool::tool2ui_pushover);

    bool drawRequested = false;
    QObject::connect(tool.get(), &xTool::tool2expend_draw, tool.get(),
                     [&](quint64 id, const QString &prompt) {
                         Q_UNUSED(prompt);
                         drawRequested = true;
                         tool->recv_drawover(id, QStringLiteral("mock-image.png"), true);
                     });

    const mcp::json args = mcp::json::object({{"prompt", "Unit test mecha concept"}}); // NOLINT
    tool->Exec(makeToolCall("stablediffusion", args));

    QTRY_VERIFY_WITH_TIMEOUT(drawRequested, 2000);
    const bool pushOk = pushSpy.count() > 0 || pushSpy.wait(2000);
    QVERIFY2(pushOk, "stablediffusion did not emit a result");

    const QString pushMessage = pushSpy.takeFirst().at(0).toString();
    QVERIFY2(pushMessage.contains(QStringLiteral("<ylsdamxssjxxdd:showdraw>mock-image.png")),
             "stablediffusion push message missing image marker");
}

class XToolFileToolsTest : public QObject
{
    Q_OBJECT

  private slots:
    void readWriteEditListSearch();
};

void XToolFileToolsTest::readWriteEditListSearch()
{
    QTemporaryDir tempDir;
    QVERIFY2(tempDir.isValid(), "Failed to create temporary directory for file tools test");

    const QString workRoot = makeUniqueWorkRoot(tempDir);
    QVERIFY2(QDir().mkpath(workRoot), "Failed to create work root");

    auto tool = createTestTool(tempDir.path(), workRoot);
    QSignalSpy pushSpy(tool.get(), &xTool::tool2ui_pushover);

    auto nextPush = [&](const QString &context) -> QString {
        if (!(pushSpy.count() > 0 || pushSpy.wait(2000)))
        {
            QTest::qFail(qPrintable(context + QStringLiteral(" did not emit a push message")), __FILE__, __LINE__);
            return QString();
        }
        return pushSpy.takeFirst().at(0).toString();
    };

    // write_file
    tool->Exec(makeToolCall("write_file", mcp::json::object({
                                                   {"path", "notes/test.txt"},
                                                   {"content", "Alpha\nBeta\nGamma\n"}})));
    const QString writeMsg = nextPush("write_file");
    QVERIFY2(writeMsg.contains(QStringLiteral("write over")), "write_file did not confirm completion");
    QJsonObject writeEnvelope;
    envelopeFromPush(writeMsg, &writeEnvelope);
    verifyEnvelopeShape(writeEnvelope);
    QVERIFY2(writeEnvelope.value(QStringLiteral("ok")).toBool(), "write_file envelope should report ok=true");
    QVERIFY2(writeEnvelope.value(QStringLiteral("data")).toObject().value(QStringLiteral("legacy_text")).toString().contains(QStringLiteral("write over")),
             "write_file envelope missing legacy completion text");

    QFile otherFile(QDir(workRoot).filePath(QStringLiteral("notes/other.txt")));
    QVERIFY2(otherFile.open(QIODevice::WriteOnly | QIODevice::Text), "Failed to create secondary file for read_file batch test");
    otherFile.write("One\nTwo\nThree\n");
    otherFile.close();

    // read_file
    tool->Exec(makeToolCall("read_file", mcp::json::object({
                                                  {"path", "notes/test.txt"},
                                                  {"start_line", 2},
                                                  {"end_line", 3}})));
    const QString readMsg = nextPush("read_file");
    QVERIFY2(readMsg.contains(QStringLiteral(">>> notes/test.txt")), "read_file missing header with path");
    QVERIFY2(readMsg.contains(QStringLiteral("2: Beta")), "read_file missing expected line number/content");
    QVERIFY2(readMsg.contains(QStringLiteral("3: Gamma")), "read_file missing expected content");
    QJsonObject readEnvelope;
    envelopeFromPush(readMsg, &readEnvelope);
    verifyEnvelopeShape(readEnvelope);
    QVERIFY2(readEnvelope.value(QStringLiteral("ok")).toBool(), "read_file envelope should report ok=true");
    QVERIFY2(readEnvelope.value(QStringLiteral("data")).toObject().value(QStringLiteral("legacy_text")).toString().contains(QStringLiteral("2: Beta")),
             "read_file envelope missing legacy file content");

    // batched read_file with multiple files and ranges
    tool->Exec(makeToolCall("read_file", mcp::json::object({
                                                  {"files", mcp::json::array({
                                                               mcp::json::object({{"path", "notes/test.txt"}, {"start_line", 1}, {"end_line", 1}}),
                                                               mcp::json::object({{"path", "notes/other.txt"}, {"line_ranges", mcp::json::array({mcp::json::array({2, 3})})}})
                                                           })}})));
    const QString readBatchMsg = nextPush("read_file batch");
    QVERIFY2(readBatchMsg.contains(QStringLiteral(">>> notes/test.txt")), "batched read_file missing first file header");
    QVERIFY2(readBatchMsg.contains(QStringLiteral("1: Alpha")), "batched read_file missing first file content");
    QVERIFY2(readBatchMsg.contains(QStringLiteral(">>> notes/other.txt")), "batched read_file missing second file header");
    QVERIFY2(readBatchMsg.contains(QStringLiteral("2: Two")), "batched read_file missing second file range content");
    QVERIFY2(readMsg.contains(QStringLiteral("Beta")), "read_file missing expected content");
    QVERIFY2(readMsg.contains(QStringLiteral("Gamma")), "read_file missing expected content");

    // replace_in_file
    tool->Exec(makeToolCall("replace_in_file", mcp::json::object({
                                                      {"path", "notes/test.txt"},
                                                      {"old_string", "Beta"},
                                                      {"new_string", "Delta"}})));
    const QString replaceMsg = nextPush("replace_in_file");
    QVERIFY2(replaceMsg.contains(QStringLiteral("replaced 1 occurrence")), "replace_in_file did not report replacement");

    // edit_in_file
    tool->Exec(makeToolCall("edit_in_file", mcp::json::object({
                                                     {"path", "notes/test.txt"},
                                                     {"edits", mcp::json::array({
                                                                   mcp::json::object({
                                                                       {"action", "insert_after"},
                                                                       {"start_line", 2},
                                                                       {"new_content", "BetaPrime"}
                                                                   }),
                                                                   mcp::json::object({
                                                                       {"action", "replace"},
                                                                       {"start_line", 1},
                                                                       {"end_line", 1},
                                                                       {"new_content", "AlphaPrime"}
                                                                   })})},
                                                     {"ensure_newline_at_eof", true}})));
    const QString structuredMsg = nextPush("edit_in_file");
    QVERIFY2(structuredMsg.contains(QStringLiteral("applied 2 edit")), "edit_in_file did not confirm edits");
    QVERIFY2(structuredMsg.contains(QStringLiteral("replace:1")), "edit_in_file summary missing replace count");
    QVERIFY2(structuredMsg.contains(QStringLiteral("insert_after:1")), "edit_in_file summary missing insert count");

    QFile resultFile(QDir(workRoot).filePath(QStringLiteral("notes/test.txt")));
    QVERIFY2(resultFile.open(QIODevice::ReadOnly | QIODevice::Text), "Failed to open edited file for verification");
    const QString finalText = QString::fromUtf8(resultFile.readAll());
    QCOMPARE(finalText, QStringLiteral("AlphaPrime\nDelta\nBetaPrime\nGamma\n"));

    // list_files (default path should point at work root)
    tool->Exec(makeToolCall("list_files", mcp::json::object()));
    const QString listDefaultMsg = nextPush("list_files default");
    QVERIFY2(listDefaultMsg.contains(QStringLiteral("notes/")),
             "list_files default listing should include newly created directory");

    // list_files (explicit directory)
    tool->Exec(makeToolCall("list_files", mcp::json::object({{"path", "notes"}})));
    const QString listMsg = nextPush("list_files");
    QVERIFY2(listMsg.contains(QStringLiteral("notes/test.txt")), "list_files missing expected entry");

    // search_content
    tool->Exec(makeToolCall("search_content", mcp::json::object({{"query", "Delta"}})));
    const QString searchMsg = nextPush("search_content");
    const bool hasSlashPath = searchMsg.contains(QStringLiteral("notes/test.txt")) || searchMsg.contains(QStringLiteral("notes\\test.txt"));
    QVERIFY2(hasSlashPath, "search_content missing file reference");
    QVERIFY2(searchMsg.contains(QStringLiteral("Delta")), "search_content missing match text");
    QVERIFY2(searchMsg.contains(QStringLiteral("Found")), "search_content should include summary");
}

class XToolWorkspaceArtifactTest : public QObject
{
    Q_OBJECT

  private slots:
    void statReportsFileAndDirectoryMetadata();
    void copyFileHandlesSpacesAndChinesePaths();
    void copyFileRejectsDirectoryIntoOwnChild();
    void artifactConfirmReportsMetadataAndHints();
    void pathGuardsRejectTraversalAndSymlinkEscape();
};

void XToolWorkspaceArtifactTest::statReportsFileAndDirectoryMetadata()
{
    QTemporaryDir tempDir;
    QVERIFY2(tempDir.isValid(), "Failed to create temporary directory for stat_file test");

    const QString workRoot = makeUniqueWorkRoot(tempDir);
    QVERIFY2(QDir().mkpath(QDir(workRoot).filePath(QStringLiteral("docs"))), "Failed to create docs directory");
    QFile file(QDir(workRoot).filePath(QStringLiteral("docs/report.md")));
    QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Text), "Failed to create report.md");
    file.write("hello artifact\n");
    file.close();

    auto tool = createTestTool(tempDir.path(), workRoot);
    QSignalSpy pushSpy(tool.get(), &xTool::tool2ui_pushover);

    tool->Exec(makeToolCall("stat_file", mcp::json::object({{"path", "docs/report.md"}})));
    QVERIFY2(pushSpy.count() > 0 || pushSpy.wait(2000), "stat_file file test produced no push message");
    QJsonObject fileEnvelope;
    envelopeFromPush(pushSpy.takeFirst().at(0).toString(), &fileEnvelope);
    verifyEnvelopeShape(fileEnvelope);
    QVERIFY2(fileEnvelope.value(QStringLiteral("ok")).toBool(), "stat_file file envelope should report ok=true");
    const QJsonObject fileData = fileEnvelope.value(QStringLiteral("data")).toObject();
    QCOMPARE(fileData.value(QStringLiteral("type")).toString(), QStringLiteral("file"));
    QCOMPARE(fileData.value(QStringLiteral("extension")).toString(), QStringLiteral("md"));
    QVERIFY2(fileData.value(QStringLiteral("size")).toString().toLongLong() > 0, "stat_file should include file size");
    QVERIFY2(!fileData.value(QStringLiteral("modified_time")).toString().isEmpty(), "stat_file should include modified_time");
    QVERIFY2(fileData.value(QStringLiteral("normalized_path")).toString().contains(QStringLiteral("report.md")), "stat_file should include normalized path");

    tool->Exec(makeToolCall("stat_file", mcp::json::object({{"path", "docs"}})));
    QVERIFY2(pushSpy.count() > 0 || pushSpy.wait(2000), "stat_file directory test produced no push message");
    QJsonObject dirEnvelope;
    envelopeFromPush(pushSpy.takeFirst().at(0).toString(), &dirEnvelope);
    verifyEnvelopeShape(dirEnvelope);
    QVERIFY2(dirEnvelope.value(QStringLiteral("ok")).toBool(), "stat_file dir envelope should report ok=true");
    QCOMPARE(dirEnvelope.value(QStringLiteral("data")).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("directory"));
    QVERIFY2(dirEnvelope.value(QStringLiteral("data")).toObject().value(QStringLiteral("allowed_roots")).toArray().size() >= 3,
             "stat_file should expose allowed roots metadata");
}

void XToolWorkspaceArtifactTest::copyFileHandlesSpacesAndChinesePaths()
{
    QTemporaryDir tempDir;
    QVERIFY2(tempDir.isValid(), "Failed to create temporary directory for copy_file test");

    const QString workRoot = makeUniqueWorkRoot(tempDir);
    QVERIFY2(QDir().mkpath(QDir(workRoot).filePath(QStringLiteral("源 目录"))), "Failed to create source directory");
    QFile source(QDir(workRoot).filePath(QStringLiteral("源 目录/文件 名.txt")));
    QVERIFY2(source.open(QIODevice::WriteOnly | QIODevice::Text), "Failed to create source file");
    source.write("copy-ok");
    source.close();

    auto tool = createTestTool(tempDir.path(), workRoot);
    QSignalSpy pushSpy(tool.get(), &xTool::tool2ui_pushover);

    tool->Exec(makeToolCall("copy_file",
                            mcp::json::object({{"source", "源 目录/文件 名.txt"},
                                               {"destination", "目标 目录/复制 文件.txt"}})));
    QVERIFY2(pushSpy.count() > 0 || pushSpy.wait(2000), "copy_file test produced no push message");
    QJsonObject envelope;
    envelopeFromPush(pushSpy.takeFirst().at(0).toString(), &envelope);
    verifyEnvelopeShape(envelope);
    QVERIFY2(envelope.value(QStringLiteral("ok")).toBool(), "copy_file envelope should report ok=true");
    QVERIFY2(envelope.value(QStringLiteral("artifacts")).toArray().size() == 1, "copy_file should report destination artifact metadata");
    const QJsonObject destination = envelope.value(QStringLiteral("data")).toObject().value(QStringLiteral("destination")).toObject();
    QVERIFY2(destination.value(QStringLiteral("normalized_path")).toString().contains(QStringLiteral("复制 文件.txt")),
             "copy_file should preserve spaces and Chinese path metadata");

    QFile copied(QDir(workRoot).filePath(QStringLiteral("目标 目录/复制 文件.txt")));
    QVERIFY2(copied.open(QIODevice::ReadOnly | QIODevice::Text), "Copied file was not created");
    QCOMPARE(QString::fromUtf8(copied.readAll()), QStringLiteral("copy-ok"));
}

void XToolWorkspaceArtifactTest::copyFileRejectsDirectoryIntoOwnChild()
{
    QTemporaryDir tempDir;
    QVERIFY2(tempDir.isValid(), "Failed to create temporary directory for copy recursion guard test");

    const QString workRoot = makeUniqueWorkRoot(tempDir);
    QVERIFY2(QDir().mkpath(QDir(workRoot).filePath(QStringLiteral("source/subdir"))), "Failed to create source tree");
    QFile sourceFile(QDir(workRoot).filePath(QStringLiteral("source/subdir/file.txt")));
    QVERIFY2(sourceFile.open(QIODevice::WriteOnly | QIODevice::Text), "Failed to create source file");
    sourceFile.write("copy recursion guard");
    sourceFile.close();

    auto tool = createTestTool(tempDir.path(), workRoot);
    QSignalSpy pushSpy(tool.get(), &xTool::tool2ui_pushover);
    tool->Exec(makeToolCall("copy_file",
                            mcp::json::object({{"source", "source"},
                                               {"destination", "source/backup"}})));

    QVERIFY2(pushSpy.count() > 0 || pushSpy.wait(2000), "copy recursion guard test produced no push message");
    QJsonObject envelope;
    envelopeFromPush(pushSpy.takeFirst().at(0).toString(), &envelope);
    verifyEnvelopeShape(envelope);
    QVERIFY2(!envelope.value(QStringLiteral("ok")).toBool(), "copy_file should reject copying a directory into its own child");
    QVERIFY2(envelope.value(QStringLiteral("data")).toObject().value(QStringLiteral("legacy_text")).toString().contains(QStringLiteral("itself")),
             "copy_file should explain the self-copy rejection");
}
void XToolWorkspaceArtifactTest::artifactConfirmReportsMetadataAndHints()
{
    QTemporaryDir tempDir;

    const QString workRoot = makeUniqueWorkRoot(tempDir);
    QVERIFY2(QDir().mkpath(QDir(workRoot).filePath(QStringLiteral("artifacts"))), "Failed to create artifacts directory");
    QFile artifact(QDir(workRoot).filePath(QStringLiteral("artifacts/report.pptx")));
    QVERIFY2(artifact.open(QIODevice::WriteOnly), "Failed to create pptx artifact");
    artifact.write("pptx-bytes");
    artifact.close();

    auto tool = createTestTool(tempDir.path(), workRoot);
    QSignalSpy pushSpy(tool.get(), &xTool::tool2ui_pushover);

    tool->Exec(makeToolCall("artifact_confirm",
                            mcp::json::object({{"path", "artifacts/report.pptx"},
                                               {"label", "Quarterly deck"},
                                               {"source_tool", "ptc"}})));
    QVERIFY2(pushSpy.count() > 0 || pushSpy.wait(2000), "artifact_confirm test produced no push message");
    QJsonObject envelope;
    envelopeFromPush(pushSpy.takeFirst().at(0).toString(), &envelope);
    verifyEnvelopeShape(envelope);
    QVERIFY2(envelope.value(QStringLiteral("ok")).toBool(), "artifact_confirm envelope should report ok=true");
    QVERIFY2(envelope.value(QStringLiteral("artifacts")).toArray().size() == 1, "artifact_confirm should include artifact collection");
    const QJsonObject data = envelope.value(QStringLiteral("data")).toObject();
    QCOMPARE(data.value(QStringLiteral("type")).toString(), QStringLiteral("file"));
    QCOMPARE(data.value(QStringLiteral("extension")).toString(), QStringLiteral("pptx"));
    QCOMPARE(data.value(QStringLiteral("label")).toString(), QStringLiteral("Quarterly deck"));
    QCOMPARE(data.value(QStringLiteral("source_tool")).toString(), QStringLiteral("ptc"));
    QVERIFY2(data.value(QStringLiteral("size")).toString().toLongLong() > 0, "artifact_confirm should report size");
    QVERIFY2(!data.value(QStringLiteral("modified_time")).toString().isEmpty(), "artifact_confirm should report modified_time");
    const QJsonObject hints = data.value(QStringLiteral("hints")).toObject();
    QVERIFY2(hints.contains(QStringLiteral("open")), "artifact_confirm should include open hint");
    QVERIFY2(hints.contains(QStringLiteral("download")), "artifact_confirm should include download hint");
    QVERIFY2(hints.contains(QStringLiteral("inspect")), "artifact_confirm should include inspect hint");
    QVERIFY2(data.value(QStringLiteral("retention_policy")).toString().contains(QStringLiteral("retained")),
             "artifact_confirm should document retention policy");
}

void XToolWorkspaceArtifactTest::pathGuardsRejectTraversalAndSymlinkEscape()
{
    QTemporaryDir tempDir;
    QVERIFY2(tempDir.isValid(), "Failed to create temporary directory for path guard test");

    const QString workRoot = makeUniqueWorkRoot(tempDir);
    const QString outsideRoot = QDir(tempDir.path()).filePath(QStringLiteral("outside"));
    QVERIFY2(QDir().mkpath(workRoot), "Failed to create work root");
    QVERIFY2(QDir().mkpath(outsideRoot), "Failed to create outside root");

    auto tool = createTestTool(tempDir.path(), workRoot);
    QSignalSpy pushSpy(tool.get(), &xTool::tool2ui_pushover);

    tool->Exec(makeToolCall("stat_file", mcp::json::object({{"path", "../outside"}})));
    QVERIFY2(pushSpy.count() > 0 || pushSpy.wait(2000), "stat_file traversal test produced no push message");
    QJsonObject traversalEnvelope;
    envelopeFromPush(pushSpy.takeFirst().at(0).toString(), &traversalEnvelope);
    verifyEnvelopeShape(traversalEnvelope);
    QVERIFY2(!traversalEnvelope.value(QStringLiteral("ok")).toBool(), "traversal stat_file should report ok=false");
    QCOMPARE(traversalEnvelope.value(QStringLiteral("error")).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("path"));

#ifndef Q_OS_WIN
    QFile outsideFile(QDir(outsideRoot).filePath(QStringLiteral("secret.txt")));
    QVERIFY2(outsideFile.open(QIODevice::WriteOnly | QIODevice::Text), "Failed to create outside file");
    outsideFile.write("secret");
    outsideFile.close();
    QVERIFY2(QFile::link(QDir(outsideRoot).filePath(QStringLiteral("secret.txt")),
                         QDir(workRoot).filePath(QStringLiteral("escape_link.txt"))),
             "Failed to create symlink escape");

    tool->Exec(makeToolCall("copy_file",
                            mcp::json::object({{"source", "escape_link.txt"},
                                               {"destination", "copied-secret.txt"}})));
    QVERIFY2(pushSpy.count() > 0 || pushSpy.wait(2000), "copy_file symlink test produced no push message");
    QJsonObject symlinkEnvelope;
    envelopeFromPush(pushSpy.takeFirst().at(0).toString(), &symlinkEnvelope);
    verifyEnvelopeShape(symlinkEnvelope);
    QVERIFY2(!symlinkEnvelope.value(QStringLiteral("ok")).toBool(), "symlink copy should report ok=false");
    QCOMPARE(symlinkEnvelope.value(QStringLiteral("error")).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("path"));
#else
    QSKIP("Symlink escape portion is Unix-only.");
#endif
}


class XToolSkillRuntimeTest : public QObject
{
    Q_OBJECT

  private slots:
    void skillCallReturnsMetadataForChineseLegacySkill();
    void skillRunStagesAssetsAndReturnsArtifacts();
    void skillRunFailsWhenExpectedOutputIsMissing();
    void skillRunReportsMissingEntrypoint();
};

void XToolSkillRuntimeTest::skillCallReturnsMetadataForChineseLegacySkill()
{
    QTemporaryDir tempDir;
    QVERIFY2(tempDir.isValid(), "Failed to create temporary directory for skill_call test");
    const QString appRoot = tempDir.path();
    const QString workRoot = makeUniqueWorkRoot(tempDir);
    const QString skillRoot = QDir(appRoot).filePath(QStringLiteral("EVA_SKILLS/中文 技能"));
    QVERIFY2(QDir().mkpath(skillRoot), "Failed to create Chinese skill root");

    QFile skillMd(QDir(skillRoot).filePath(QStringLiteral("SKILL.md")));
    QVERIFY2(skillMd.open(QIODevice::WriteOnly | QIODevice::Text), "Failed to write SKILL.md");
    skillMd.write("---\nname: 中文 技能\ndescription: legacy skill\ndependencies: [python, node]\noutputs: [result.md]\n---\nUse this skill.\n");
    skillMd.close();
    QFile script(QDir(skillRoot).filePath(QStringLiteral("helper.py")));
    QVERIFY2(script.open(QIODevice::WriteOnly | QIODevice::Text), "Failed to write helper.py");
    script.write("print('hello')\n");
    script.close();

    auto tool = createTestTool(appRoot, workRoot);
    QSignalSpy pushSpy(tool.get(), &xTool::tool2ui_pushover);
    tool->Exec(makeToolCall("skill_call", mcp::json::object({{"name", "中文 技能"}})));

    QVERIFY2(pushSpy.count() > 0 || pushSpy.wait(2000), "skill_call did not emit a push message");
    QJsonObject envelope;
    envelopeFromPush(pushSpy.takeFirst().at(0).toString(), &envelope);
    verifyEnvelopeShape(envelope);
    QVERIFY2(envelope.value(QStringLiteral("ok")).toBool(), "skill_call envelope should report ok=true");
    const QString legacy = envelope.value(QStringLiteral("data")).toObject().value(QStringLiteral("legacy_text")).toString();
    QVERIFY2(legacy.contains(QStringLiteral("中文 技能")), "skill_call should preserve Chinese skill name");
    QVERIFY2(legacy.contains(QStringLiteral("entrypoint_hints")), "skill_call should expose inferred entrypoint hints");
    QVERIFY2(legacy.contains(QStringLiteral("dependency_hints")), "skill_call should expose dependency hints");
    QVERIFY2(legacy.contains(QStringLiteral("output_hints")), "skill_call should expose output hints");
}

void XToolSkillRuntimeTest::skillRunStagesAssetsAndReturnsArtifacts()
{
    QTemporaryDir tempDir;
    QVERIFY2(tempDir.isValid(), "Failed to create temporary directory for skill_run test");
    const QString appRoot = tempDir.path();
    const QString workRoot = makeUniqueWorkRoot(tempDir);
    const QString skillRoot = QDir(appRoot).filePath(QStringLiteral("EVA_SKILLS/deck-skill"));
    QVERIFY2(QDir().mkpath(skillRoot), "Failed to create skill root");

    QFile skillMd(QDir(skillRoot).filePath(QStringLiteral("SKILL.md")));
    QVERIFY2(skillMd.open(QIODevice::WriteOnly | QIODevice::Text), "Failed to write SKILL.md");
    skillMd.write("---\nname: deck-skill\ndescription: script skill\ncommand: sh run.sh\nassets: [template.txt]\noutputs: [deck.pptx]\n---\nRun the script.\n");
    skillMd.close();
    QFile script(QDir(skillRoot).filePath(QStringLiteral("run.sh")));
    QVERIFY2(script.open(QIODevice::WriteOnly | QIODevice::Text), "Failed to write run.sh");
    script.write("printf 'pptx bytes from skill' > deck.pptx\nprintf 'mutated' > template.txt\n");
    script.close();
    QFile asset(QDir(skillRoot).filePath(QStringLiteral("template.txt")));
    QVERIFY2(asset.open(QIODevice::WriteOnly | QIODevice::Text), "Failed to write template asset");
    asset.write("original");
    asset.close();

    auto tool = createTestTool(appRoot, workRoot);
    QSignalSpy pushSpy(tool.get(), &xTool::tool2ui_pushover);
    QSignalSpy stateSpy(tool.get(), &xTool::tool2ui_state);
    tool->Exec(makeToolCall("skill_run", mcp::json::object({{"name", "deck-skill"}, {"expected_outputs", mcp::json::array({"deck.pptx"})}, {"timeout_ms", 5000}})));

    QVERIFY2(pushSpy.count() > 0 || pushSpy.wait(6000), "skill_run did not emit a push message");
    QJsonObject envelope;
    envelopeFromPush(pushSpy.takeFirst().at(0).toString(), &envelope);
    verifyEnvelopeShape(envelope);
    QVERIFY2(envelope.value(QStringLiteral("ok")).toBool(), "skill_run envelope should report ok=true");
    QVERIFY2(envelope.value(QStringLiteral("artifacts")).toArray().size() >= 1, "skill_run should confirm generated artifact");
    QVERIFY2(envelope.value(QStringLiteral("data")).toObject().value(QStringLiteral("run_dir")).toString().contains(QStringLiteral(".eva_runs")),
             "skill_run should expose isolated run directory");

    bool sawLoading = false;
    bool sawRunning = false;
    bool sawArtifact = false;
    for (const auto &argsVariant : stateSpy)
    {
        const QString line = argsVariant.at(0).toString();
        sawLoading = sawLoading || line.contains(QStringLiteral("progress:skill_loading"));
        sawRunning = sawRunning || line.contains(QStringLiteral("progress:skill_running"));
        sawArtifact = sawArtifact || line.contains(QStringLiteral("progress:artifact_ready"));
    }
    QVERIFY2(sawLoading, "skill_run should emit skill_loading progress");
    QVERIFY2(sawRunning, "skill_run should emit skill_running progress");
    QVERIFY2(sawArtifact, "skill_run should emit artifact_ready progress");

    QFile originalAsset(QDir(skillRoot).filePath(QStringLiteral("template.txt")));
    QVERIFY2(originalAsset.open(QIODevice::ReadOnly | QIODevice::Text), "Failed to reopen original asset");
    QCOMPARE(QString::fromUtf8(originalAsset.readAll()), QStringLiteral("original"));
}

void XToolSkillRuntimeTest::skillRunFailsWhenExpectedOutputIsMissing()
{
    QTemporaryDir tempDir;
    QVERIFY2(tempDir.isValid(), "Failed to create temporary directory for missing Skill output test");
    const QString appRoot = tempDir.path();
    const QString workRoot = makeUniqueWorkRoot(tempDir);
    const QString skillRoot = QDir(appRoot).filePath(QStringLiteral("EVA_SKILLS/missing-output-skill"));
    QVERIFY2(QDir().mkpath(skillRoot), "Failed to create skill root");

    QFile skillMd(QDir(skillRoot).filePath(QStringLiteral("SKILL.md")));
    QVERIFY2(skillMd.open(QIODevice::WriteOnly | QIODevice::Text), "Failed to write SKILL.md");
    skillMd.write("---\nname: missing-output-skill\ndescription: missing output skill\ncommand: sh run.sh\noutputs: [deck.pptx]\n---\nRun the script.\n");
    skillMd.close();
    QFile script(QDir(skillRoot).filePath(QStringLiteral("run.sh")));
    QVERIFY2(script.open(QIODevice::WriteOnly | QIODevice::Text), "Failed to write run.sh");
    script.write("printf 'done without deck'\n");
    script.close();

    auto tool = createTestTool(appRoot, workRoot);
    QSignalSpy pushSpy(tool.get(), &xTool::tool2ui_pushover);
    tool->Exec(makeToolCall("skill_run", mcp::json::object({{"name", "missing-output-skill"}, {"expected_outputs", mcp::json::array({"deck.pptx"})}, {"timeout_ms", 5000}})));

    QVERIFY2(pushSpy.count() > 0 || pushSpy.wait(6000), "missing-output skill_run did not emit a push message");
    QJsonObject envelope;
    envelopeFromPush(pushSpy.takeFirst().at(0).toString(), &envelope);
    verifyEnvelopeShape(envelope);
    QVERIFY2(!envelope.value(QStringLiteral("ok")).toBool(), "skill_run should fail when expected output is missing");
    QCOMPARE(envelope.value(QStringLiteral("error")).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("artifact_missing"));
    QVERIFY2(envelope.value(QStringLiteral("data")).toObject().value(QStringLiteral("missing_expected_outputs")).toArray().contains(QStringLiteral("deck.pptx")),
             "skill_run should report the missing expected output");
}
void XToolSkillRuntimeTest::skillRunReportsMissingEntrypoint()
{
    QTemporaryDir tempDir;
    QVERIFY2(tempDir.isValid(), "Failed to create temporary directory for missing entrypoint test");
    const QString appRoot = tempDir.path();
    const QString workRoot = makeUniqueWorkRoot(tempDir);
    const QString skillRoot = QDir(appRoot).filePath(QStringLiteral("EVA_SKILLS/doc-only"));
    QVERIFY2(QDir().mkpath(skillRoot), "Failed to create doc-only skill root");

    QFile skillMd(QDir(skillRoot).filePath(QStringLiteral("SKILL.md")));
    QVERIFY2(skillMd.open(QIODevice::WriteOnly | QIODevice::Text), "Failed to write SKILL.md");
    skillMd.write("---\nname: doc-only\ndescription: no script\n---\nOnly instructions.\n");
    skillMd.close();

    auto tool = createTestTool(appRoot, workRoot);
    QSignalSpy pushSpy(tool.get(), &xTool::tool2ui_pushover);
    tool->Exec(makeToolCall("skill_run", mcp::json::object({{"name", "doc-only"}})));

    QVERIFY2(pushSpy.count() > 0 || pushSpy.wait(2000), "missing entrypoint skill_run did not emit a push message");
    QJsonObject envelope;
    envelopeFromPush(pushSpy.takeFirst().at(0).toString(), &envelope);
    verifyEnvelopeShape(envelope);
    QVERIFY2(!envelope.value(QStringLiteral("ok")).toBool(), "missing entrypoint skill_run should report ok=false");
    QVERIFY2(envelope.value(QStringLiteral("recovery_hints")).toArray().size() >= 1, "missing entrypoint should include recovery hints");
}

class XToolWinPathCompatibilityTest : public QObject
{
    Q_OBJECT

  private slots:
    void structuredPathToolsHandleBackslashesSpacesChineseAndLongPaths();
};

void XToolWinPathCompatibilityTest::structuredPathToolsHandleBackslashesSpacesChineseAndLongPaths()
{
    QTemporaryDir tempDir;
    QVERIFY2(tempDir.isValid(), "Failed to create temporary directory for Windows path compatibility test");
    const QString workRoot = makeUniqueWorkRoot(tempDir);
    const QString longDir = QStringLiteral("space dir/中文 子目录/long-path-segment-for-win7-fallback-check");
    QVERIFY2(QDir().mkpath(QDir(workRoot).filePath(longDir)), "Failed to create compatibility path");
    QFile file(QDir(workRoot).filePath(longDir + QStringLiteral("/report file.txt")));
    QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Text), "Failed to create compatibility file");
    file.write("win-path-ok");
    file.close();

    auto tool = createTestTool(tempDir.path(), workRoot);
    QSignalSpy pushSpy(tool.get(), &xTool::tool2ui_pushover);
    tool->Exec(makeToolCall("stat_file", mcp::json::object({{"path", "space dir\\中文 子目录\\long-path-segment-for-win7-fallback-check\\report file.txt"}})));

    QVERIFY2(pushSpy.count() > 0 || pushSpy.wait(2000), "stat_file backslash compatibility test produced no push message");
    QJsonObject envelope;
    envelopeFromPush(pushSpy.takeFirst().at(0).toString(), &envelope);
    verifyEnvelopeShape(envelope);
    QVERIFY2(envelope.value(QStringLiteral("ok")).toBool(), "stat_file should handle backslashes, spaces, and Chinese path segments");
    const QJsonObject data = envelope.value(QStringLiteral("data")).toObject();
    QVERIFY2(data.value(QStringLiteral("normalized_path")).toString().contains(QStringLiteral("report file.txt")), "normalized path should preserve file name");
}

class XToolFileGuardsTest : public QObject
{
    Q_OBJECT

  private slots:
    void replaceInFileEnforcesExpectedCount();
    void replaceInFileShowsSnippetWhenMissing();
};

void XToolFileGuardsTest::replaceInFileEnforcesExpectedCount()
{
    QTemporaryDir tempDir;
    QVERIFY2(tempDir.isValid(), "Failed to create temporary directory for replace_in_file guard test");

    const QString workRoot = makeUniqueWorkRoot(tempDir);
    QVERIFY2(QDir().mkpath(workRoot), "Failed to create work root for guard test");
    QDir rootDir(workRoot);
    QVERIFY2(rootDir.mkpath(QStringLiteral("notes")), "Failed to create notes directory for guard test");

    QFile file(rootDir.filePath(QStringLiteral("notes/sample.txt")));
    QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Text), "Failed to prime sample file for guard test");
    file.write("Alpha\nBeta\nGamma\n");
    file.close();

    auto tool = createTestTool(tempDir.path(), workRoot);
    QSignalSpy pushSpy(tool.get(), &xTool::tool2ui_pushover);

    tool->Exec(makeToolCall("replace_in_file",
                            mcp::json::object({{"path", "notes/sample.txt"},
                                               {"old_string", "Beta"},
                                               {"new_string", "BetaPrime"},
                                               {"expected_replacements", 2}})));

    QVERIFY2(pushSpy.count() > 0 || pushSpy.wait(2000), "replace_in_file guard test did not emit push message");
    const QString message = pushSpy.takeFirst().at(0).toString();
    QVERIFY2(message.contains(QStringLiteral("Expected 2 replacement(s) but found 1")),
             "replace_in_file guard did not report expected replacement mismatch");

    QFile verify(rootDir.filePath(QStringLiteral("notes/sample.txt")));
    QVERIFY2(verify.open(QIODevice::ReadOnly | QIODevice::Text), "Failed to reopen file after guard execution");
    const QString persisted = QString::fromUtf8(verify.readAll());
    QCOMPARE(persisted, QStringLiteral("Alpha\nBeta\nGamma\n"));
}

void XToolFileGuardsTest::replaceInFileShowsSnippetWhenMissing()
{
    QTemporaryDir tempDir;
    QVERIFY2(tempDir.isValid(), "Failed to create temporary directory for snippet guard test");

    const QString workRoot = makeUniqueWorkRoot(tempDir);
    QVERIFY2(QDir().mkpath(workRoot), "Failed to create work root for snippet guard test");
    QDir rootDir(workRoot);
    QVERIFY2(rootDir.mkpath(QStringLiteral("notes")), "Failed to create notes directory for snippet guard test");

    QFile file(rootDir.filePath(QStringLiteral("notes/sample.txt")));
    QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Text), "Failed to prime sample file for snippet guard test");
    file.write("Alpha\nBeta\nGamma\n");
    file.close();

    auto tool = createTestTool(tempDir.path(), workRoot);
    QSignalSpy pushSpy(tool.get(), &xTool::tool2ui_pushover);

    tool->Exec(makeToolCall("replace_in_file",
                            mcp::json::object({{"path", "notes/sample.txt"},
                                               {"old_string", "BetaPrime block"},
                                               {"new_string", "BetaPrime"}})));

    QVERIFY2(pushSpy.count() > 0 || pushSpy.wait(2000), "replace_in_file missing-match test did not emit push message");
    const QString message = pushSpy.takeFirst().at(0).toString();
    QVERIFY2(message.contains(QStringLiteral("old_string NOT found.")),
             "replace_in_file missing-match flow did not report the failure");
    QVERIFY2(message.contains(QStringLiteral("Snippet: BetaPrime block")),
             "replace_in_file missing-match flow should include snippet preview");
    QVERIFY2(message.contains(QStringLiteral("Hint: provide more surrounding context")),
             "replace_in_file missing-match flow should include hint text");
}

class XToolSearchContentTest : public QObject
{
    Q_OBJECT

  private slots:
    void searchContentHandlesEmptyQueryAndNoMatches();
};

void XToolSearchContentTest::searchContentHandlesEmptyQueryAndNoMatches()
{
    QTemporaryDir tempDir;
    QVERIFY2(tempDir.isValid(), "Failed to create temporary directory for search_content guard test");

    const QString workRoot = makeUniqueWorkRoot(tempDir);
    QVERIFY2(QDir().mkpath(workRoot), "Failed to create work root for search_content guard test");
    QDir rootDir(workRoot);
    QVERIFY2(rootDir.mkpath(QStringLiteral("notes")), "Failed to create notes directory for search_content guard test");

    QFile file(rootDir.filePath(QStringLiteral("notes/log.txt")));
    QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Text), "Failed to prime log file for search_content");
    file.write("Alpha bravo charlie");
    file.close();

    auto tool = createTestTool(tempDir.path(), workRoot);
    QSignalSpy pushSpy(tool.get(), &xTool::tool2ui_pushover);

    tool->Exec(makeToolCall("search_content", mcp::json::object({{"query", "   "}})));
    QVERIFY2(pushSpy.count() > 0 || pushSpy.wait(2000), "search_content empty-query test produced no message");
    QString message = pushSpy.takeFirst().at(0).toString();
    QVERIFY2(message.contains(QStringLiteral("Empty query.")),
             "search_content empty-query flow should report validation error");

    pushSpy.clear();
    tool->Exec(makeToolCall("search_content", mcp::json::object({{"query", "delta"}})));
    QVERIFY2(pushSpy.count() > 0 || pushSpy.wait(2000), "search_content no-match test produced no message");
    message = pushSpy.takeFirst().at(0).toString();
    QVERIFY2(message.contains(QStringLiteral("No matches.")),
             "search_content no-match flow should mention the empty result");
}

class XToolMcpListTest : public QObject
{
    Q_OBJECT

  private slots:
    void mcpToolListRoundtrip();
};

void XToolMcpListTest::mcpToolListRoundtrip()
{
    QTemporaryDir tempDir;
    QVERIFY2(tempDir.isValid(), "Failed to create temporary directory for MCP list test");

    auto tool = createTestTool(tempDir.path(), makeUniqueWorkRoot(tempDir));
    QSignalSpy pushSpy(tool.get(), &xTool::tool2ui_pushover);

    bool listRequested = false;
    QObject::connect(tool.get(), &xTool::tool2mcp_toollist, tool.get(),
                     [&](quint64 id) {
                         listRequested = true;
                         tool->recv_calllist_over(id);
                     });

    tool->Exec(makeToolCall("mcp_tools_list", mcp::json::object()));

    QTRY_VERIFY_WITH_TIMEOUT(listRequested, 2000);

    const bool pushOk = pushSpy.count() > 0 || pushSpy.wait(2000);
    QVERIFY2(pushOk, "mcp_tools_list did not produce a push message");

    const QString pushMessage = pushSpy.takeFirst().at(0).toString();
    QVERIFY2(pushMessage.contains(QStringLiteral("mcp_tool_list")),
             "mcp_tools_list push message missing identifier");
}

class XToolWorkdirTest : public QObject
{
    Q_OBJECT

  private slots:
    void recvWorkdirUpdatesRoot();
    void createTempDirectoryHandlesExistingPaths();
};

void XToolWorkdirTest::recvWorkdirUpdatesRoot()
{
    QTemporaryDir tempDir;
    QVERIFY2(tempDir.isValid(), "Failed to create temporary directory for workdir test");

    auto tool = createTestTool(tempDir.path(), makeUniqueWorkRoot(tempDir));
    QSignalSpy stateSpy(tool.get(), &xTool::tool2ui_state);

    const QString newRoot = tempDir.filePath(QStringLiteral("custom_root"));
    tool->recv_workdir(newRoot);
    QCOMPARE(tool->workDirRoot, QDir::cleanPath(newRoot));

    const bool stateOk = stateSpy.count() > 0 || stateSpy.wait(1000);
    QVERIFY2(stateOk, "recv_workdir did not emit a state notification");

    const QString message = stateSpy.takeFirst().at(0).toString();
    QVERIFY2(message.contains(QDir::cleanPath(newRoot)), "State notification missing updated path");
}

void XToolWorkdirTest::createTempDirectoryHandlesExistingPaths()
{
    QTemporaryDir tempDir;
    QVERIFY2(tempDir.isValid(), "Failed to create temporary directory for temp dir test");

    auto tool = createTestTool(tempDir.path(), makeUniqueWorkRoot(tempDir));
    const QString tempPath = tempDir.filePath(QStringLiteral("EVA_TEMP/work-subdir"));
    QVERIFY2(tool->createTempDirectory(tempPath), "Expected createTempDirectory to create new path");
    QVERIFY2(QDir(tempPath).exists(), "Expected new temporary directory to exist on disk");
    QVERIFY2(!tool->createTempDirectory(tempPath), "Existing directories should not be recreated");
}

class XToolClampTest : public QObject
{
    Q_OBJECT

  private slots:
    void readFileOutputIsClamped();
};

void XToolClampTest::readFileOutputIsClamped()
{
    QTemporaryDir tempDir;
    QVERIFY2(tempDir.isValid(), "Failed to create temporary directory for clamp test");

    const QString workRoot = makeUniqueWorkRoot(tempDir);
    QVERIFY2(QDir().mkpath(workRoot), "Failed to create work root for clamp test");
    QDir workDir(workRoot);
    QVERIFY2(workDir.mkpath(QStringLiteral("notes")), "Failed to create notes directory");

    QFile bigFile(workDir.filePath(QStringLiteral("notes/big.txt")));
    QVERIFY2(bigFile.open(QIODevice::WriteOnly | QIODevice::Text), "Failed to open big.txt for writing");
    QByteArray payload("HEAD-");
    payload += QByteArray(60000, 'B');
    payload += "-TAIL";
    QVERIFY2(bigFile.write(payload) == payload.size(), "Failed to write payload for clamp test");
    bigFile.close();

    auto tool = createTestTool(tempDir.path(), workRoot);
    QSignalSpy pushSpy(tool.get(), &xTool::tool2ui_pushover);

    tool->Exec(makeToolCall("read_file", mcp::json::object({{"path", "notes/big.txt"}})));

    if (!(pushSpy.count() > 0 || pushSpy.wait(2000)))
    {
        QFAIL("Expected push message for clamp test");
        return;
    }

    const QString message = pushSpy.takeFirst().at(0).toString();
    QVERIFY2(message.contains(QStringLiteral("[tool output truncated")), "Expected clamp indicator missing");
    QVERIFY2(message.contains(QStringLiteral("...")), "Expected ellipsis marker in clamped output");
    QVERIFY2(message.contains(QStringLiteral("HEAD-")), "Clamped output should keep leading context");
    QVERIFY2(message.contains(QStringLiteral("-TAIL")), "Clamped output should keep trailing context");
}

int main(int argc, char **argv)
{
#ifdef Q_OS_LINUX
    qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
#endif
    QApplication app(argc, argv);

    int status = 0;
    {
        XToolCalculatorTest tc;
        status |= QTest::qExec(&tc, argc, argv);
    }
    {
        XToolExecuteCommandTest tc;
        status |= QTest::qExec(&tc, argc, argv);
    }
    {
        XToolPtcTest tc;
        status |= QTest::qExec(&tc, argc, argv);
    }
    {
        XToolMcpFlowTest tc;
        status |= QTest::qExec(&tc, argc, argv);
    }
    {
        XToolKnowledgeTest tc;
        status |= QTest::qExec(&tc, argc, argv);
    }
    {
        XToolStableDiffusionTest tc;
        status |= QTest::qExec(&tc, argc, argv);
    }
    {
        XToolFileToolsTest tc;
        status |= QTest::qExec(&tc, argc, argv);
    }
    {
        XToolWorkspaceArtifactTest tc;
        status |= QTest::qExec(&tc, argc, argv);
    }
    {
        XToolSkillRuntimeTest tc;
        status |= QTest::qExec(&tc, argc, argv);
    }
    {
        XToolWinPathCompatibilityTest tc;
        status |= QTest::qExec(&tc, argc, argv);
    }
    {
        XToolFileGuardsTest tc;
        status |= QTest::qExec(&tc, argc, argv);
    }
    {
        XToolSearchContentTest tc;
        status |= QTest::qExec(&tc, argc, argv);
    }
    {
        XToolMcpListTest tc;
        status |= QTest::qExec(&tc, argc, argv);
    }
    {
        XToolWorkdirTest tc;
        status |= QTest::qExec(&tc, argc, argv);
    }
    {
        XToolClampTest tc;
        status |= QTest::qExec(&tc, argc, argv);
    }

    return status;
}

#include "xtool_tests.moc"
