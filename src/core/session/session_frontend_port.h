#pragma once

#include <QString>
#include <QStringList>

#include "xconfig.h"

// SessionController 面向前端的窄端口。
// 运行层会话逻辑只通过该端口采集输入和报告前端相关警告，避免直接绑定 Widget 控件。
class SessionFrontendPort
{
public:
    virtual ~SessionFrontendPort() = default;

    virtual QString takeDraftText(bool allowText) = 0;
    virtual QStringList draftImageFilePaths() const = 0;
    virtual QStringList draftDocumentFilePaths() const = 0;
    virtual QStringList draftAudioFilePaths() const = 0;
    virtual void clearDraftAttachments() = 0;

    virtual bool shouldAttachControllerFrame() const = 0;
    virtual QString captureControllerFrameImagePath() = 0;
    virtual void rememberControllerFrameForModel(const QString &imagePath) = 0;

    virtual void presentUserMessageRecord(const QString &text, int messageIndex) = 0;
    virtual void presentToolMessageRecord(const QString &toolName, const QString &text, int messageIndex) = 0;
    virtual bool outputDocumentEmpty() const = 0;
    virtual void showSessionWarning(const QString &message, SIGNAL_STATE state) = 0;
};
