find_package(Qt5 COMPONENTS Core Gui Widgets REQUIRED)

set_source_files_properties(
    ui_theme_tokens_tests.cpp
    PROPERTIES SKIP_AUTOUIC ON
)

add_executable(ui_theme_tokens_tests
    ui_theme_tokens_tests.cpp
)
target_link_libraries(ui_theme_tokens_tests PRIVATE
    Qt5::Core
    Qt5::Gui
    eva_doctest
)

add_executable(app_shell_tests
    app_shell_tests.cpp
    ${CMAKE_SOURCE_DIR}/src/widget/app_shell.cpp
    ${CMAKE_SOURCE_DIR}/src/widget/app_shell.h
    ${CMAKE_SOURCE_DIR}/src/widget/context_drawer.cpp
    ${CMAKE_SOURCE_DIR}/src/widget/context_drawer.h
    ${CMAKE_SOURCE_DIR}/src/widget/chat_workspace_page.cpp
    ${CMAKE_SOURCE_DIR}/src/widget/chat_workspace_page.h
    ${CMAKE_SOURCE_DIR}/src/widget/engineer_workspace_page.cpp
    ${CMAKE_SOURCE_DIR}/src/widget/engineer_workspace_page.h
    ${CMAKE_SOURCE_DIR}/src/widget/knowledge_workspace_page.cpp
    ${CMAKE_SOURCE_DIR}/src/widget/knowledge_workspace_page.h
    ${CMAKE_SOURCE_DIR}/src/widget/media_workspace_page.cpp
    ${CMAKE_SOURCE_DIR}/src/widget/media_workspace_page.h
    ${CMAKE_SOURCE_DIR}/src/widget/settings_workspace_page.cpp
    ${CMAKE_SOURCE_DIR}/src/widget/settings_workspace_page.h
)
target_link_libraries(app_shell_tests PRIVATE
    Qt5::Core
    Qt5::Widgets
    eva_doctest
)
target_include_directories(ui_theme_tokens_tests PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)
target_compile_features(ui_theme_tokens_tests PRIVATE cxx_std_17)

target_include_directories(app_shell_tests PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)
target_compile_features(app_shell_tests PRIVATE cxx_std_17)

if (MINGW)
    if (DEFINED EVA_COMPILE_OPTIONS)
        target_compile_options(ui_theme_tokens_tests PRIVATE ${EVA_COMPILE_OPTIONS})
        target_compile_options(app_shell_tests PRIVATE ${EVA_COMPILE_OPTIONS})
    endif()
    if (DEFINED EVA_LINK_OPTIONS)
        target_link_options(ui_theme_tokens_tests PRIVATE ${EVA_LINK_OPTIONS})
        target_link_options(app_shell_tests PRIVATE ${EVA_LINK_OPTIONS})
    endif()
endif()

add_test(NAME ui_theme_tokens_tests COMMAND ui_theme_tokens_tests)
set_tests_properties(ui_theme_tokens_tests PROPERTIES LABELS unit)

add_test(NAME app_shell_tests COMMAND app_shell_tests)
set_tests_properties(app_shell_tests PROPERTIES LABELS unit)
