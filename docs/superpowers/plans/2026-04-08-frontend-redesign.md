# EVA Frontend Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rebuild EVA’s frontend into a modern, light desktop workspace centered on chat and model switching while preserving existing capabilities and Win7 compatibility.

**Architecture:** Keep the current Qt Widgets application shell and core/runtime flow, but introduce a new page-oriented UI shell that reorganizes existing Widget and Expend capabilities into navigation, workspace pages, and a shared context drawer. Migrate UI incrementally: first create the new shell and theme tokens, then move chat, engineering tools, resource pages, and settings into the new structure without changing backend, toolflow, or model-serving responsibilities.

**Tech Stack:** Qt Widgets, Qt Designer `.ui`, QSS themes, existing Widget/Expend classes, Catch2/QSignalSpy unit tests, CMake/CTest

---

## File Structure

### Existing files to modify

- `src/widget/widget.h` — add app-shell level state, page switching hooks, and shared drawer/theme accessors.
- `src/widget/widget.cpp` — wire the new shell into startup, initialize page containers, and keep existing controllers/services alive.
- `src/widget/widget.ui` — replace current monolithic layout with app shell containers.
- `src/widget/widget_settings.cpp` — migrate theme application to token-driven styling and keep settings persistence intact.
- `src/widget/widget_settings_slots.cpp` — reconnect settings UI actions to the new settings center.
- `src/widget/widget_output.cpp` — restyle chat/tool/think rendering to fit the new message system.
- `src/widget/widget_records.cpp` — align record bar visuals and session affordances with the new layout.
- `src/widget/widget_engineer.cpp` — connect engineering status/actions into the new context drawer.
- `src/widget/widget_skills.cpp` — keep skills UI logic but retarget it to the drawer/panels.
- `src/expend/expend.h` — extract resource-page widgets behind reusable page entry points.
- `src/expend/expend.cpp` — stop assuming the old standalone window-first interaction model.
- `src/expend/expend.ui` — slim down old tab-centric layout as resource pages are moved into the shell.
- `src/expend/expend_knowledge.cpp` — adapt knowledge UI to the new knowledge workspace page.
- `src/expend/expend_mcp.cpp` — adapt MCP UI fragments for the engineering drawer.
- `src/expend/expend_sd.cpp` — adapt image/media UI for the new media workspace page.
- `src/expend/expend_tts.cpp` — adapt audio/media UI for the new media workspace page.
- `src/main.cpp` — keep boot wiring but connect page-level UI signals if needed.
- `tests/CMakeLists.txt` and/or `tests/unit/CMakeLists.txt` — register new frontend tests.

### New files to create

- `src/widget/app_shell.h`
- `src/widget/app_shell.cpp`
- `src/widget/app_shell.ui`
  - Overall shell: primary nav rail, secondary side panel, main workspace stack, shared right drawer.
- `src/widget/context_drawer.h`
- `src/widget/context_drawer.cpp`
- `src/widget/context_drawer.ui`
  - Shared drawer for engineering environment, skills, MCP, and terminal.
- `src/widget/chat_workspace_page.h`
- `src/widget/chat_workspace_page.cpp`
- `src/widget/chat_workspace_page.ui`
  - Chat-first workspace page with session list, chat header, message area host, and composer host.
- `src/widget/engineer_workspace_page.h`
- `src/widget/engineer_workspace_page.cpp`
- `src/widget/engineer_workspace_page.ui`
  - Engineering overview page with project/environment summary and quick actions.
- `src/widget/knowledge_workspace_page.h`
- `src/widget/knowledge_workspace_page.cpp`
- `src/widget/knowledge_workspace_page.ui`
  - Knowledge base page shell hosting migrated knowledge views.
- `src/widget/media_workspace_page.h`
- `src/widget/media_workspace_page.cpp`
- `src/widget/media_workspace_page.ui`
  - Media workspace page shell for image/audio/vision tabs.
- `src/widget/settings_workspace_page.h`
- `src/widget/settings_workspace_page.cpp`
- `src/widget/settings_workspace_page.ui`
  - Searchable settings center with left categories and detail pane.
- `src/widget/ui_theme_tokens.h`
- `src/widget/ui_theme_tokens.cpp`
  - Theme token model for colors, spacing, radii, density, and Win7 fallback rules.
- `resource/QSS/theme_modern_light.qss`
  - New light productivity theme.
- `resource/QSS/theme_modern_light_legacy.qss`
  - Win7/legacy fallback overlay.
- `tests/unit/widget/app_shell_tests.cpp`
- `tests/unit/widget/ui_theme_tokens_tests.cpp`
  - Unit tests for page routing and theme fallback behavior.

### Supporting documentation to create

- `docs/软件功能实现路径总览.md`
  - High-level map of EVA feature flows, module responsibilities, key files, and execution paths for later reference.

---

### Task 1: Create theme token foundation

**Files:**
- Create: `src/widget/ui_theme_tokens.h`
- Create: `src/widget/ui_theme_tokens.cpp`
- Create: `tests/unit/widget/ui_theme_tokens_tests.cpp`
- Modify: `tests/unit/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

```cpp
#include <catch2/catch_test_macros.hpp>

#include "widget/ui_theme_tokens.h"

TEST_CASE("modern light theme exposes legacy fallback visuals")
{
    const eva::ui::ThemeTokens modern = eva::ui::buildThemeTokens(QStringLiteral("modern_light"), false);
    const eva::ui::ThemeTokens legacy = eva::ui::buildThemeTokens(QStringLiteral("modern_light"), true);

    CHECK(modern.themeId == QStringLiteral("modern_light"));
    CHECK(legacy.themeId == QStringLiteral("modern_light"));
    CHECK(modern.useLegacyFallback == false);
    CHECK(legacy.useLegacyFallback == true);
    CHECK(modern.cornerRadiusMd >= legacy.cornerRadiusMd);
    CHECK(modern.shadowOpacity > legacy.shadowOpacity);
    CHECK_FALSE(modern.primaryBg.name().isEmpty());
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `ctest --test-dir build --output-on-failure -R ui_theme_tokens_tests`
Expected: FAIL because `ui_theme_tokens.h/.cpp` and the new test target do not exist yet.

- [ ] **Step 3: Write minimal implementation**

```cpp
// src/widget/ui_theme_tokens.h
#pragma once

#include <QColor>
#include <QString>

namespace eva::ui {

struct ThemeTokens
{
    QString themeId;
    bool useLegacyFallback = false;
    QColor primaryBg;
    QColor surfaceBg;
    QColor borderColor;
    QColor textPrimary;
    QColor textSecondary;
    QColor accent;
    int cornerRadiusSm = 6;
    int cornerRadiusMd = 10;
    int cornerRadiusLg = 14;
    int spacingXs = 4;
    int spacingSm = 8;
    int spacingMd = 12;
    int spacingLg = 16;
    qreal shadowOpacity = 0.0;
};

ThemeTokens buildThemeTokens(const QString &themeId, bool useLegacyFallback);

} // namespace eva::ui
```

```cpp
// src/widget/ui_theme_tokens.cpp
#include "ui_theme_tokens.h"

namespace eva::ui {

ThemeTokens buildThemeTokens(const QString &themeId, bool useLegacyFallback)
{
    ThemeTokens tokens;
    tokens.themeId = themeId.trimmed().isEmpty() ? QStringLiteral("modern_light") : themeId.trimmed();
    tokens.useLegacyFallback = useLegacyFallback;
    tokens.primaryBg = QColor(QStringLiteral("#F6F7F9"));
    tokens.surfaceBg = QColor(QStringLiteral("#FFFFFF"));
    tokens.borderColor = QColor(QStringLiteral("#E6E9EF"));
    tokens.textPrimary = QColor(QStringLiteral("#1F2937"));
    tokens.textSecondary = QColor(QStringLiteral("#667085"));
    tokens.accent = QColor(QStringLiteral("#4C7CF0"));
    tokens.cornerRadiusSm = 6;
    tokens.cornerRadiusMd = useLegacyFallback ? 8 : 10;
    tokens.cornerRadiusLg = useLegacyFallback ? 10 : 14;
    tokens.spacingXs = 4;
    tokens.spacingSm = 8;
    tokens.spacingMd = 12;
    tokens.spacingLg = 16;
    tokens.shadowOpacity = useLegacyFallback ? 0.0 : 0.18;
    return tokens;
}

} // namespace eva::ui
```

```cmake
# tests/unit/CMakeLists.txt
add_executable(ui_theme_tokens_tests
    widget/ui_theme_tokens_tests.cpp
    ../../src/widget/ui_theme_tokens.cpp
)

target_include_directories(ui_theme_tokens_tests PRIVATE ../../src)
target_link_libraries(ui_theme_tokens_tests PRIVATE Catch2::Catch2WithMain Qt5::Core Qt5::Gui)
add_test(NAME ui_theme_tokens_tests COMMAND ui_theme_tokens_tests)
```

- [ ] **Step 4: Run test to verify it passes**

Run: `ctest --test-dir build --output-on-failure -R ui_theme_tokens_tests`
Expected: PASS with `1/1 tests passed`.

- [ ] **Step 5: Commit**

```bash
git add src/widget/ui_theme_tokens.h src/widget/ui_theme_tokens.cpp tests/unit/widget/ui_theme_tokens_tests.cpp tests/unit/CMakeLists.txt
git commit -m "feat: add frontend theme token foundation"
```

---

### Task 2: Introduce the app shell container

**Files:**
- Create: `src/widget/app_shell.h`
- Create: `src/widget/app_shell.cpp`
- Create: `src/widget/app_shell.ui`
- Create: `tests/unit/widget/app_shell_tests.cpp`
- Modify: `tests/unit/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

```cpp
#include <catch2/catch_test_macros.hpp>

#include "widget/app_shell.h"

TEST_CASE("app shell switches between workspace pages by route")
{
    eva::ui::AppShell shell;

    shell.registerPage(QStringLiteral("chat"), new QWidget(&shell));
    shell.registerPage(QStringLiteral("engineer"), new QWidget(&shell));

    REQUIRE(shell.currentRoute().isEmpty());
    CHECK(shell.switchTo(QStringLiteral("chat")) == true);
    CHECK(shell.currentRoute() == QStringLiteral("chat"));
    CHECK(shell.switchTo(QStringLiteral("engineer")) == true);
    CHECK(shell.currentRoute() == QStringLiteral("engineer"));
    CHECK(shell.switchTo(QStringLiteral("missing")) == false);
    CHECK(shell.currentRoute() == QStringLiteral("engineer"));
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `ctest --test-dir build --output-on-failure -R app_shell_tests`
Expected: FAIL because the app shell class and target do not exist yet.

- [ ] **Step 3: Write minimal implementation**

```cpp
// src/widget/app_shell.h
#pragma once

#include <QHash>
#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui { class AppShell; }
QT_END_NAMESPACE

namespace eva::ui {

class AppShell : public QWidget
{
    Q_OBJECT

  public:
    explicit AppShell(QWidget *parent = nullptr);
    ~AppShell() override;

    void registerPage(const QString &route, QWidget *page);
    bool switchTo(const QString &route);
    QString currentRoute() const;

  private:
    Ui::AppShell *ui;
    QHash<QString, int> routeIndex_;
    QString currentRoute_;
};

} // namespace eva::ui
```

```cpp
// src/widget/app_shell.cpp
#include "app_shell.h"
#include "ui_app_shell.h"

namespace eva::ui {

AppShell::AppShell(QWidget *parent)
    : QWidget(parent), ui(new Ui::AppShell)
{
    ui->setupUi(this);
}

AppShell::~AppShell()
{
    delete ui;
}

void AppShell::registerPage(const QString &route, QWidget *page)
{
    if (route.trimmed().isEmpty() || !page) return;
    const int index = ui->workspaceStack->addWidget(page);
    routeIndex_.insert(route, index);
}

bool AppShell::switchTo(const QString &route)
{
    const auto it = routeIndex_.find(route);
    if (it == routeIndex_.end()) return false;
    ui->workspaceStack->setCurrentIndex(it.value());
    currentRoute_ = route;
    return true;
}

QString AppShell::currentRoute() const
{
    return currentRoute_;
}

} // namespace eva::ui
```

```xml
<!-- src/widget/app_shell.ui -->
<ui version="4.0">
 <class>AppShell</class>
 <widget class="QWidget" name="AppShell">
  <layout class="QHBoxLayout" name="rootLayout">
   <item><widget class="QFrame" name="navRail"/></item>
   <item><widget class="QFrame" name="sidePanel"/></item>
   <item><widget class="QStackedWidget" name="workspaceStack"/></item>
   <item><widget class="QFrame" name="contextDrawer"/></item>
  </layout>
 </widget>
</ui>
```

```cmake
# tests/unit/CMakeLists.txt
add_executable(app_shell_tests
    widget/app_shell_tests.cpp
    ../../src/widget/app_shell.cpp
)

target_include_directories(app_shell_tests PRIVATE ../../src ../../build)
target_link_libraries(app_shell_tests PRIVATE Catch2::Catch2WithMain Qt5::Core Qt5::Widgets)
add_test(NAME app_shell_tests COMMAND app_shell_tests)
```

- [ ] **Step 4: Run test to verify it passes**

Run: `ctest --test-dir build --output-on-failure -R app_shell_tests`
Expected: PASS with route switching verified.

- [ ] **Step 5: Commit**

```bash
git add src/widget/app_shell.h src/widget/app_shell.cpp src/widget/app_shell.ui tests/unit/widget/app_shell_tests.cpp tests/unit/CMakeLists.txt
git commit -m "feat: add workspace app shell container"
```

---

### Task 3: Mount the app shell into Widget without moving business logic yet

**Files:**
- Modify: `src/widget/widget.h`
- Modify: `src/widget/widget.cpp`
- Modify: `src/widget/widget.ui`
- Modify: `src/main.cpp`
- Test: `tests/unit/widget/app_shell_tests.cpp`

- [ ] **Step 1: Write the failing integration assertion**

```cpp
TEST_CASE("widget builds shell and defaults to chat route")
{
    Widget widget(nullptr, QStringLiteral("."));

    REQUIRE(widget.appShell() != nullptr);
    CHECK(widget.currentPrimaryRoute() == QStringLiteral("chat"));
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `ctest --test-dir build --output-on-failure -R app_shell_tests`
Expected: FAIL because `Widget::appShell()` and `Widget::currentPrimaryRoute()` do not exist.

- [ ] **Step 3: Write minimal implementation**

```cpp
// src/widget/widget.h
#include "app_shell.h"

public:
    eva::ui::AppShell *appShell() const { return appShell_; }
    QString currentPrimaryRoute() const { return currentPrimaryRoute_; }

private:
    eva::ui::AppShell *appShell_ = nullptr;
    QString currentPrimaryRoute_ = QStringLiteral("chat");
    void initAppShell();
```

```cpp
// src/widget/widget.cpp
void Widget::initAppShell()
{
    if (appShell_) return;
    appShell_ = new eva::ui::AppShell(this);
    appShell_->registerPage(QStringLiteral("chat"), new QWidget(appShell_));
    appShell_->registerPage(QStringLiteral("engineer"), new QWidget(appShell_));
    appShell_->registerPage(QStringLiteral("knowledge"), new QWidget(appShell_));
    appShell_->registerPage(QStringLiteral("media"), new QWidget(appShell_));
    appShell_->registerPage(QStringLiteral("settings"), new QWidget(appShell_));
    appShell_->switchTo(currentPrimaryRoute_);
    ui->shellHostLayout->addWidget(appShell_);
}
```

```xml
<!-- src/widget/widget.ui -->
<layout class="QVBoxLayout" name="shellHostLayout"/>
```

- [ ] **Step 4: Run test to verify it passes**

Run: `ctest --test-dir build --output-on-failure -R app_shell_tests`
Expected: PASS with the Widget shell defaulting to `chat`.

- [ ] **Step 5: Commit**

```bash
git add src/widget/widget.h src/widget/widget.cpp src/widget/widget.ui src/main.cpp tests/unit/widget/app_shell_tests.cpp
git commit -m "refactor: mount app shell in main widget"
```

---

### Task 4: Create the context drawer for engineering tools

**Files:**
- Create: `src/widget/context_drawer.h`
- Create: `src/widget/context_drawer.cpp`
- Create: `src/widget/context_drawer.ui`
- Modify: `src/widget/widget.h`
- Modify: `src/widget/widget_engineer.cpp`
- Modify: `src/widget/widget_skills.cpp`
- Test: `tests/unit/widget/app_shell_tests.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
TEST_CASE("context drawer switches engineering panels")
{
    eva::ui::ContextDrawer drawer;

    drawer.registerPanel(QStringLiteral("environment"), new QWidget(&drawer));
    drawer.registerPanel(QStringLiteral("skills"), new QWidget(&drawer));

    CHECK(drawer.showPanel(QStringLiteral("environment")) == true);
    CHECK(drawer.currentPanel() == QStringLiteral("environment"));
    CHECK(drawer.showPanel(QStringLiteral("skills")) == true);
    CHECK(drawer.currentPanel() == QStringLiteral("skills"));
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `ctest --test-dir build --output-on-failure -R app_shell_tests`
Expected: FAIL because `ContextDrawer` does not exist.

- [ ] **Step 3: Write minimal implementation**

```cpp
// src/widget/context_drawer.h
#pragma once

#include <QHash>
#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui { class ContextDrawer; }
QT_END_NAMESPACE

namespace eva::ui {

class ContextDrawer : public QWidget
{
    Q_OBJECT

  public:
    explicit ContextDrawer(QWidget *parent = nullptr);
    ~ContextDrawer() override;

    void registerPanel(const QString &name, QWidget *panel);
    bool showPanel(const QString &name);
    QString currentPanel() const;

  private:
    Ui::ContextDrawer *ui;
    QHash<QString, int> panelIndexes_;
    QString currentPanel_;
};

} // namespace eva::ui
```

```cpp
// src/widget/context_drawer.cpp
#include "context_drawer.h"
#include "ui_context_drawer.h"

namespace eva::ui {

ContextDrawer::ContextDrawer(QWidget *parent)
    : QWidget(parent), ui(new Ui::ContextDrawer)
{
    ui->setupUi(this);
}

ContextDrawer::~ContextDrawer()
{
    delete ui;
}

void ContextDrawer::registerPanel(const QString &name, QWidget *panel)
{
    if (name.trimmed().isEmpty() || !panel) return;
    const int index = ui->panelStack->addWidget(panel);
    panelIndexes_.insert(name, index);
}

bool ContextDrawer::showPanel(const QString &name)
{
    const auto it = panelIndexes_.find(name);
    if (it == panelIndexes_.end()) return false;
    ui->panelStack->setCurrentIndex(it.value());
    currentPanel_ = name;
    return true;
}

QString ContextDrawer::currentPanel() const
{
    return currentPanel_;
}

} // namespace eva::ui
```

```xml
<!-- src/widget/context_drawer.ui -->
<ui version="4.0">
 <class>ContextDrawer</class>
 <widget class="QWidget" name="ContextDrawer">
  <layout class="QVBoxLayout" name="rootLayout">
   <item><widget class="QLabel" name="titleLabel"/></item>
   <item><widget class="QStackedWidget" name="panelStack"/></item>
  </layout>
 </widget>
</ui>
```

- [ ] **Step 4: Run test to verify it passes**

Run: `ctest --test-dir build --output-on-failure -R app_shell_tests`
Expected: PASS with the drawer switching panels.

- [ ] **Step 5: Commit**

```bash
git add src/widget/context_drawer.h src/widget/context_drawer.cpp src/widget/context_drawer.ui src/widget/widget.h src/widget/widget_engineer.cpp src/widget/widget_skills.cpp tests/unit/widget/app_shell_tests.cpp
git commit -m "feat: add engineering context drawer"
```

---

### Task 5: Build the chat workspace page and route existing chat UI into it

**Files:**
- Create: `src/widget/chat_workspace_page.h`
- Create: `src/widget/chat_workspace_page.cpp`
- Create: `src/widget/chat_workspace_page.ui`
- Modify: `src/widget/widget.cpp`
- Modify: `src/widget/widget.ui`
- Modify: `src/widget/widget_output.cpp`
- Modify: `src/widget/widget_records.cpp`
- Test: `tests/unit/widget/app_shell_tests.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
TEST_CASE("chat workspace exposes header and composer hosts")
{
    eva::ui::ChatWorkspacePage page;

    REQUIRE(page.headerHost() != nullptr);
    REQUIRE(page.sessionListHost() != nullptr);
    REQUIRE(page.messageHost() != nullptr);
    REQUIRE(page.composerHost() != nullptr);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `ctest --test-dir build --output-on-failure -R app_shell_tests`
Expected: FAIL because `ChatWorkspacePage` does not exist.

- [ ] **Step 3: Write minimal implementation**

```cpp
// src/widget/chat_workspace_page.h
#pragma once

#include <QFrame>
#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui { class ChatWorkspacePage; }
QT_END_NAMESPACE

namespace eva::ui {

class ChatWorkspacePage : public QWidget
{
    Q_OBJECT

  public:
    explicit ChatWorkspacePage(QWidget *parent = nullptr);
    ~ChatWorkspacePage() override;

    QFrame *headerHost() const;
    QFrame *sessionListHost() const;
    QFrame *messageHost() const;
    QFrame *composerHost() const;

  private:
    Ui::ChatWorkspacePage *ui;
};

} // namespace eva::ui
```

```cpp
// src/widget/chat_workspace_page.cpp
#include "chat_workspace_page.h"
#include "ui_chat_workspace_page.h"

namespace eva::ui {

ChatWorkspacePage::ChatWorkspacePage(QWidget *parent)
    : QWidget(parent), ui(new Ui::ChatWorkspacePage)
{
    ui->setupUi(this);
}

ChatWorkspacePage::~ChatWorkspacePage()
{
    delete ui;
}

QFrame *ChatWorkspacePage::headerHost() const { return ui->headerHost; }
QFrame *ChatWorkspacePage::sessionListHost() const { return ui->sessionListHost; }
QFrame *ChatWorkspacePage::messageHost() const { return ui->messageHost; }
QFrame *ChatWorkspacePage::composerHost() const { return ui->composerHost; }

} // namespace eva::ui
```

- [ ] **Step 4: Run test to verify it passes**

Run: `ctest --test-dir build --output-on-failure -R app_shell_tests`
Expected: PASS with all required chat hosts present.

- [ ] **Step 5: Commit**

```bash
git add src/widget/chat_workspace_page.h src/widget/chat_workspace_page.cpp src/widget/chat_workspace_page.ui src/widget/widget.cpp src/widget/widget.ui src/widget/widget_output.cpp src/widget/widget_records.cpp tests/unit/widget/app_shell_tests.cpp
git commit -m "feat: add chat workspace page"
```

---

### Task 6: Create the engineering overview page

**Files:**
- Create: `src/widget/engineer_workspace_page.h`
- Create: `src/widget/engineer_workspace_page.cpp`
- Create: `src/widget/engineer_workspace_page.ui`
- Modify: `src/widget/widget_engineer.cpp`
- Modify: `src/widget/widget.cpp`
- Test: `tests/unit/widget/app_shell_tests.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
TEST_CASE("engineer workspace exposes overview cards")
{
    eva::ui::EngineerWorkspacePage page;

    REQUIRE(page.projectSummaryCard() != nullptr);
    REQUIRE(page.quickActionsCard() != nullptr);
    REQUIRE(page.recentContextCard() != nullptr);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `ctest --test-dir build --output-on-failure -R app_shell_tests`
Expected: FAIL because `EngineerWorkspacePage` does not exist.

- [ ] **Step 3: Write minimal implementation**

```cpp
class EngineerWorkspacePage : public QWidget
{
    Q_OBJECT
  public:
    explicit EngineerWorkspacePage(QWidget *parent = nullptr);
    ~EngineerWorkspacePage() override;

    QFrame *projectSummaryCard() const;
    QFrame *quickActionsCard() const;
    QFrame *recentContextCard() const;

  private:
    Ui::EngineerWorkspacePage *ui;
};
```

```cpp
QFrame *EngineerWorkspacePage::projectSummaryCard() const { return ui->projectSummaryCard; }
QFrame *EngineerWorkspacePage::quickActionsCard() const { return ui->quickActionsCard; }
QFrame *EngineerWorkspacePage::recentContextCard() const { return ui->recentContextCard; }
```

- [ ] **Step 4: Run test to verify it passes**

Run: `ctest --test-dir build --output-on-failure -R app_shell_tests`
Expected: PASS with all overview cards exposed.

- [ ] **Step 5: Commit**

```bash
git add src/widget/engineer_workspace_page.h src/widget/engineer_workspace_page.cpp src/widget/engineer_workspace_page.ui src/widget/widget_engineer.cpp src/widget/widget.cpp tests/unit/widget/app_shell_tests.cpp
git commit -m "feat: add engineer overview workspace"
```

---

### Task 7: Create knowledge and media workspace pages

**Files:**
- Create: `src/widget/knowledge_workspace_page.h`
- Create: `src/widget/knowledge_workspace_page.cpp`
- Create: `src/widget/knowledge_workspace_page.ui`
- Create: `src/widget/media_workspace_page.h`
- Create: `src/widget/media_workspace_page.cpp`
- Create: `src/widget/media_workspace_page.ui`
- Modify: `src/expend/expend_knowledge.cpp`
- Modify: `src/expend/expend_sd.cpp`
- Modify: `src/expend/expend_tts.cpp`
- Modify: `src/widget/widget.cpp`
- Test: `tests/unit/widget/app_shell_tests.cpp`

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("knowledge workspace exposes resource list and detail hosts")
{
    eva::ui::KnowledgeWorkspacePage page;
    REQUIRE(page.sidebarHost() != nullptr);
    REQUIRE(page.listHost() != nullptr);
    REQUIRE(page.detailHost() != nullptr);
}

TEST_CASE("media workspace exposes tool tabs and preview host")
{
    eva::ui::MediaWorkspacePage page;
    REQUIRE(page.toolTabs() != nullptr);
    REQUIRE(page.parameterHost() != nullptr);
    REQUIRE(page.previewHost() != nullptr);
}
```

- [ ] **Step 2: Run test to verify they fail**

Run: `ctest --test-dir build --output-on-failure -R app_shell_tests`
Expected: FAIL because the new workspace page classes do not exist.

- [ ] **Step 3: Write minimal implementation**

```cpp
class KnowledgeWorkspacePage : public QWidget
{
    Q_OBJECT
  public:
    explicit KnowledgeWorkspacePage(QWidget *parent = nullptr);
    ~KnowledgeWorkspacePage() override;

    QFrame *sidebarHost() const;
    QFrame *listHost() const;
    QFrame *detailHost() const;

  private:
    Ui::KnowledgeWorkspacePage *ui;
};

class MediaWorkspacePage : public QWidget
{
    Q_OBJECT
  public:
    explicit MediaWorkspacePage(QWidget *parent = nullptr);
    ~MediaWorkspacePage() override;

    QTabWidget *toolTabs() const;
    QFrame *parameterHost() const;
    QFrame *previewHost() const;

  private:
    Ui::MediaWorkspacePage *ui;
};
```

- [ ] **Step 4: Run test to verify they pass**

Run: `ctest --test-dir build --output-on-failure -R app_shell_tests`
Expected: PASS with both workspace pages exposing their host widgets.

- [ ] **Step 5: Commit**

```bash
git add src/widget/knowledge_workspace_page.h src/widget/knowledge_workspace_page.cpp src/widget/knowledge_workspace_page.ui src/widget/media_workspace_page.h src/widget/media_workspace_page.cpp src/widget/media_workspace_page.ui src/expend/expend_knowledge.cpp src/expend/expend_sd.cpp src/expend/expend_tts.cpp src/widget/widget.cpp tests/unit/widget/app_shell_tests.cpp
git commit -m "feat: add knowledge and media workspace pages"
```

---

### Task 8: Build the settings workspace page and connect settings persistence

**Files:**
- Create: `src/widget/settings_workspace_page.h`
- Create: `src/widget/settings_workspace_page.cpp`
- Create: `src/widget/settings_workspace_page.ui`
- Modify: `src/widget/widget_settings.cpp`
- Modify: `src/widget/widget_settings_slots.cpp`
- Modify: `src/widget/widget.cpp`
- Test: `tests/unit/widget/app_shell_tests.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
TEST_CASE("settings workspace exposes search and category navigation")
{
    eva::ui::SettingsWorkspacePage page;

    REQUIRE(page.searchEdit() != nullptr);
    REQUIRE(page.categoryList() != nullptr);
    REQUIRE(page.detailHost() != nullptr);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `ctest --test-dir build --output-on-failure -R app_shell_tests`
Expected: FAIL because `SettingsWorkspacePage` does not exist.

- [ ] **Step 3: Write minimal implementation**

```cpp
class SettingsWorkspacePage : public QWidget
{
    Q_OBJECT
  public:
    explicit SettingsWorkspacePage(QWidget *parent = nullptr);
    ~SettingsWorkspacePage() override;

    QLineEdit *searchEdit() const;
    QListWidget *categoryList() const;
    QFrame *detailHost() const;

  private:
    Ui::SettingsWorkspacePage *ui;
};
```

- [ ] **Step 4: Run test to verify it passes**

Run: `ctest --test-dir build --output-on-failure -R app_shell_tests`
Expected: PASS with the settings workspace exposing the expected UI pieces.

- [ ] **Step 5: Commit**

```bash
git add src/widget/settings_workspace_page.h src/widget/settings_workspace_page.cpp src/widget/settings_workspace_page.ui src/widget/widget_settings.cpp src/widget/widget_settings_slots.cpp src/widget/widget.cpp tests/unit/widget/app_shell_tests.cpp
git commit -m "feat: add settings workspace page"
```

---

### Task 9: Apply the modern light theme and legacy overlay

**Files:**
- Create: `resource/QSS/theme_modern_light.qss`
- Create: `resource/QSS/theme_modern_light_legacy.qss`
- Modify: `src/widget/widget_settings.cpp`
- Modify: `src/widget/widget.cpp`
- Modify: `src/widget/widget_output.cpp`
- Modify: `src/widget/terminal_pane.cpp`
- Test: `tests/unit/widget/ui_theme_tokens_tests.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
TEST_CASE("theme stylesheet path includes legacy overlay when requested")
{
    CHECK(eva::ui::buildThemeStylesheetPaths(QStringLiteral("modern_light"), false)
              == QStringList{QStringLiteral(":/QSS/theme_modern_light.qss")});
    CHECK(eva::ui::buildThemeStylesheetPaths(QStringLiteral("modern_light"), true)
              == QStringList{QStringLiteral(":/QSS/theme_modern_light.qss"),
                             QStringLiteral(":/QSS/theme_modern_light_legacy.qss")});
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `ctest --test-dir build --output-on-failure -R ui_theme_tokens_tests`
Expected: FAIL because stylesheet path selection helper does not exist.

- [ ] **Step 3: Write minimal implementation**

```cpp
QStringList buildThemeStylesheetPaths(const QString &themeId, bool useLegacyFallback)
{
    const QString effective = themeId.trimmed().isEmpty() ? QStringLiteral("modern_light") : themeId.trimmed();
    QStringList paths{QStringLiteral(":/QSS/theme_%1.qss").arg(effective)};
    if (useLegacyFallback)
    {
        paths << QStringLiteral(":/QSS/theme_%1_legacy.qss").arg(effective);
    }
    return paths;
}
```

```qss
/* resource/QSS/theme_modern_light.qss */
QWidget {
    background: #F6F7F9;
    color: #1F2937;
}
QFrame#Card {
    background: #FFFFFF;
    border: 1px solid #E6E9EF;
    border-radius: 10px;
}
```

```qss
/* resource/QSS/theme_modern_light_legacy.qss */
QFrame#Card {
    border-radius: 8px;
}
QScrollBar:vertical, QScrollBar:horizontal {
    background: #EEF1F5;
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `ctest --test-dir build --output-on-failure -R ui_theme_tokens_tests`
Expected: PASS with both modern and legacy stylesheet path sets verified.

- [ ] **Step 5: Commit**

```bash
git add resource/QSS/theme_modern_light.qss resource/QSS/theme_modern_light_legacy.qss src/widget/widget_settings.cpp src/widget/widget.cpp src/widget/widget_output.cpp src/widget/terminal_pane.cpp src/widget/ui_theme_tokens.h src/widget/ui_theme_tokens.cpp tests/unit/widget/ui_theme_tokens_tests.cpp
git commit -m "feat: add modern light theme with legacy fallback"
```

---

### Task 10: Write the software feature-path reference document

**Files:**
- Create: `docs/软件功能实现路径总览.md`
- Modify: `docs/机体架构设计.md`

- [ ] **Step 1: Write the documentation content**

```md
# 软件功能实现路径总览

## 1. 主窗口聊天链路
- 入口：`src/widget/widget.cpp`
- 会话编排：`src/core/session/*`
- 请求发送：`src/service/net/*`, `src/xnet.*`
- 工具回环：`src/core/toolflow/*`, `src/xtool.*`
- 输出渲染：`src/widget/widget_output.cpp`

## 2. 工程能力链路
- UI 入口：`src/widget/widget_engineer.cpp`
- 技能：`src/skill/skill_manager.cpp`, `src/widget/widget_skills.cpp`
- MCP：`src/xmcp.*`, `src/expend/expend_mcp.cpp`
- Docker：`src/utils/docker_sandbox.cpp`
- 终端：`src/widget/terminal_pane.cpp`

## 3. 知识库链路
- UI：`src/expend/expend_knowledge.cpp`
- 文档解析：`src/doc2md.*` / `thirdparty/doc2md/*`
- 向量库：`src/storage/vectordb.*`

## 4. 媒体链路
- 图像：`src/expend/expend_sd.cpp`
- 语音：`src/expend/expend_tts.cpp`, `src/expend/expend_whisper.cpp`

## 5. 设置与主题链路
- 设置：`src/widget/widget_settings.cpp`
- 持久化：`QSettings` in `src/widget/widget_funcs.cpp`
- 主题：`resource/QSS/*`
```

- [ ] **Step 2: Review the new document for completeness**

Run: `python3 - <<'PY'
from pathlib import Path
text = Path('docs/软件功能实现路径总览.md').read_text(encoding='utf-8')
for key in ['主窗口聊天链路', '工程能力链路', '知识库链路', '媒体链路', '设置与主题链路']:
    assert key in text, key
print('ok')
PY`
Expected: prints `ok`.

- [ ] **Step 3: Commit**

```bash
git add docs/软件功能实现路径总览.md docs/机体架构设计.md
git commit -m "docs: add software feature path reference"
```

---

### Task 11: Run focused verification for the new frontend foundation

**Files:**
- Test: `tests/unit/widget/app_shell_tests.cpp`
- Test: `tests/unit/widget/ui_theme_tokens_tests.cpp`

- [ ] **Step 1: Run frontend unit tests**

Run: `ctest --test-dir build --output-on-failure -R "(app_shell_tests|ui_theme_tokens_tests)"`
Expected: PASS for both frontend test targets.

- [ ] **Step 2: Run existing regression tests that cover adjacent behavior**

Run: `ctest --test-dir build --output-on-failure -R "(tool_registry_tests|xmcp_tests|settings_change_analyzer_tests)"`
Expected: PASS, confirming theme/routing work did not break tool metadata, MCP, or settings-change analysis.

- [ ] **Step 3: Run application build**

Run: `cmake --build build`
Expected: successful build with the new widget/page classes and QSS resources compiled.

- [ ] **Step 4: Commit**

```bash
git add src/widget src/expend resource/QSS tests/unit/widget docs/软件功能实现路径总览.md
git commit -m "test: verify frontend workspace foundation"
```

---

## Self-Review

### Spec coverage

- Chat-first shell: covered by Tasks 2, 3, 5.
- Engineering overview + right drawer: covered by Tasks 4 and 6.
- Knowledge/media independent pages: covered by Task 7.
- Settings center: covered by Task 8.
- Modern light theme + Win7 fallback: covered by Tasks 1 and 9.
- Documentation for later reuse of software feature paths: covered by Task 10.
- Verification and regression protection: covered by Task 11.

### Placeholder scan

- Removed generic “write tests” placeholders by providing concrete test code for every new UI unit.
- Removed generic “implement page” placeholders by naming exact classes/files/hosts.
- Included exact commands for tests/build/doc verification.

### Type consistency

- `AppShell`, `ContextDrawer`, `ChatWorkspacePage`, `EngineerWorkspacePage`, `KnowledgeWorkspacePage`, `MediaWorkspacePage`, and `SettingsWorkspacePage` use consistent names across files/tests/tasks.
- Theme helper names are consistent: `ThemeTokens`, `buildThemeTokens`, `buildThemeStylesheetPaths`.

---

Plan complete and saved to `docs/superpowers/plans/2026-04-08-frontend-redesign.md`. Two execution options:

**1. Subagent-Driven (recommended)** - I dispatch a fresh subagent per task, review between tasks, fast iteration

**2. Inline Execution** - Execute tasks in this session using executing-plans, batch execution with checkpoints

**Which approach?**
