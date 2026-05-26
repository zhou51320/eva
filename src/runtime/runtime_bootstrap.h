#pragma once

#include "app/app_context.h"

// RuntimeBootstrap 只处理无 UI 运行层也需要的启动准备：
// 环境变量、路径上下文、EVA_TEMP 和首次默认配置。
// Widget/Expend/托盘/主题/字体等仍属于前端启动层。
class RuntimeBootstrap
{
  public:
    struct Options
    {
        bool ensureTempDir = true;
        bool ensureDefaultConfig = true;
    };

    // 必须在 QApplication/QCoreApplication 创建前调用。
    static void applyProcessEnvironment();

    // 构建运行层上下文，并按选项准备持久化目录和默认配置。
    static AppContext prepareContext();
    static AppContext prepareContext(const Options &options);
};
