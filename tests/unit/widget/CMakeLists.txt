find_package(Qt5 COMPONENTS Core Gui REQUIRED)

add_executable(ui_theme_tokens_tests
    ui_theme_tokens_tests.cpp
)
target_link_libraries(ui_theme_tokens_tests PRIVATE
    Qt5::Core
    Qt5::Gui
    eva_doctest
)
target_include_directories(ui_theme_tokens_tests PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)
target_compile_features(ui_theme_tokens_tests PRIVATE cxx_std_17)

if (MINGW)
    if (DEFINED EVA_COMPILE_OPTIONS)
        target_compile_options(ui_theme_tokens_tests PRIVATE ${EVA_COMPILE_OPTIONS})
    endif()
    if (DEFINED EVA_LINK_OPTIONS)
        target_link_options(ui_theme_tokens_tests PRIVATE ${EVA_LINK_OPTIONS})
    endif()
endif()

add_test(NAME ui_theme_tokens_tests COMMAND ui_theme_tokens_tests)
set_tests_properties(ui_theme_tokens_tests PROPERTIES LABELS unit)
