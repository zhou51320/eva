#pragma once

#include <QJsonArray>
#include <QJsonObject>
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

// 发送命令。允许前端直接提交结构化 endpoint 数据，由 runtime 在内部构造 RequestSnapshot。
struct RuntimeSendMessageCommand
{
    QString text;
    QStringList imagePaths;
    QStringList documentPaths;
    QStringList audioPaths;
    QJsonArray frontendMessages;
    APIS apis;
    ENDPOINT_DATA endpoint;
    QJsonObject wordsObj;
    int languageFlag = EVA_LANG_ZH;
    quint64 turnId = 0;
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
