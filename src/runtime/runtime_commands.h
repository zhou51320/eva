#pragma once

#include <QJsonArray>
#include <QString>
#include <QStringList>

#include "xconfig.h"

// 本地装载命令。后续 BackendCoordinator 去 Widget 化后会直接消费该结构。
struct RuntimeLoadLocalCommand
{
    QString modelPath;
    QString mmprojPath;
    QString loraPath;
    QString backendChoice = QStringLiteral("auto");
    QString port = QStringLiteral(DEFAULT_SERVER_PORT);
    SETTINGS settings;
    bool forceReload = false;
};

// 链接模式命令。endpoint 是 OpenAI 兼容 base endpoint，model 是目标模型名。
struct RuntimeConnectRemoteCommand
{
    QString endpoint;
    QString apiKey;
    QString model;
    SETTINGS sampling;
};

// 发送命令。第一阶段先覆盖文本；附件字段保留给后续图片/文档/音频迁移。
struct RuntimeSendMessageCommand
{
    QString text;
    QStringList imagePaths;
    QStringList documentPaths;
    QStringList audioPaths;
    QJsonArray frontendMessages;
    bool stream = true;
};

struct RuntimeResetCommand
{
    bool clearHistory = true;
};

Q_DECLARE_METATYPE(RuntimeLoadLocalCommand)
Q_DECLARE_METATYPE(RuntimeConnectRemoteCommand)
Q_DECLARE_METATYPE(RuntimeSendMessageCommand)
Q_DECLARE_METATYPE(RuntimeResetCommand)
