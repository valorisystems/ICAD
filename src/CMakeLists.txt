add_library(icad_compiler
    agent/workflow.cpp
    json/value.cpp
    evidence/compliance.cpp
    project/builder.cpp
    mcp/server.cpp
    compiler/compiler.cpp
    compiler/dependency_graph.cpp
    compiler/expression.cpp
    compiler/incremental.cpp
    compiler/importer.cpp
    compiler/language.cpp
    compiler/lexer/lexer.cpp
    compiler/parser/parser.cpp
    compiler/resolver/resolver.cpp
    compiler/semantic/semantic.cpp
    compiler/types/types.cpp
    compiler/units/units.cpp
    materials/library.cpp
    materials/catalog.cpp
    cad/model.cpp
    cad/boolean.cpp
    cad/topology.cpp
    cad/analysis.cpp
    cad/intersection.cpp
    cad/queries.cpp
    constraints/validator.cpp
    constraints/sketch_solver.cpp
    ai/inspector.cpp
    lsp/server.cpp
    document/revision.cpp
    document/source.cpp
    document/exporter.cpp
    manufacturing/validator.cpp
    drawings/exporter.cpp
    scene/exporter.cpp
    scene/evaluator.cpp
    viewer/live_session.cpp
    exchange/exporter.cpp
    exchange/obj_exporter.cpp
    exchange/gltf_exporter.cpp
    exchange/step_exporter.cpp
    exchange/stl_exporter.cpp
    exchange/three_mf_exporter.cpp
)
add_library(ICAD::compiler ALIAS icad_compiler)
set_target_properties(icad_compiler PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(icad_compiler
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_include_directories(icad_compiler PRIVATE "${PROJECT_BINARY_DIR}/generated")
target_compile_definitions(icad_compiler PRIVATE ICAD_VERSION="${ICAD_RELEASE_VERSION}")
target_link_libraries(icad_compiler PUBLIC icad_project_options)

add_library(icad_engine SHARED engine/session.cpp engine/c_api.cpp)
add_library(ICAD::engine ALIAS icad_engine)
target_compile_definitions(icad_engine PRIVATE ICAD_ENGINE_BUILD)
target_include_directories(icad_engine
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(icad_engine PRIVATE ICAD::compiler icad_project_options)
set_target_properties(icad_engine PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

add_executable(icad cli/main.cpp)
target_compile_definitions(icad PRIVATE ICAD_VERSION="${ICAD_RELEASE_VERSION}")
target_link_libraries(icad PRIVATE ICAD::compiler icad_project_options)
install(TARGETS icad icad_engine
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/icad DESTINATION include)

if(ICAD_BUILD_DESKTOP_VIEWER)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets Concurrent)
    qt_standard_project_setup()
    qt_add_executable(icad-viewer
        MANUAL_FINALIZATION
        desktop_viewer/main.cpp
        desktop_viewer/main_window.cpp
        desktop_viewer/main_window.hpp
        desktop_viewer/cad_viewport.cpp
        desktop_viewer/cad_viewport.hpp
        desktop_viewer/scene_model.cpp
        desktop_viewer/scene_model.hpp
        desktop_viewer/icad_highlighter.cpp
        desktop_viewer/icad_highlighter.hpp
        desktop_viewer/icad_editor.cpp
        desktop_viewer/icad_editor.hpp
        desktop_viewer/resources.qrc
        engine/session.cpp
    )
    target_compile_definitions(icad-viewer PRIVATE
        ICAD_ENGINE_BUILD
        ICAD_VERSION="${ICAD_RELEASE_VERSION}")
    target_link_libraries(icad-viewer PRIVATE
        ICAD::compiler
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::OpenGL
        Qt6::OpenGLWidgets
        Qt6::Concurrent
        icad_project_options
    )
    set_target_properties(icad-viewer PROPERTIES
        MACOSX_BUNDLE TRUE
        WIN32_EXECUTABLE TRUE
        OUTPUT_NAME "icad-viewer"
    )
    if(APPLE)
        configure_file(
            ${PROJECT_SOURCE_DIR}/cmake/ICADViewerInfo.plist.in
            ${PROJECT_BINARY_DIR}/generated/ICADViewerInfo.plist
            @ONLY)
        set_source_files_properties(${PROJECT_SOURCE_DIR}/assets/icons/icad.icns
                                    PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
        target_sources(icad-viewer PRIVATE ${PROJECT_SOURCE_DIR}/assets/icons/icad.icns)
        set_target_properties(icad-viewer PROPERTIES
            MACOSX_BUNDLE_INFO_PLIST ${PROJECT_BINARY_DIR}/generated/ICADViewerInfo.plist
            MACOSX_BUNDLE_BUNDLE_NAME "ICAD Studio"
            MACOSX_BUNDLE_GUI_IDENTIFIER "com.icad.studio"
            MACOSX_BUNDLE_ICON_FILE "icad.icns"
            MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
            MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        )
        set(ICAD_MACOS_CODESIGN_IDENTITY "-" CACHE STRING
            "macOS signing identity for ICAD Studio; '-' creates an ad-hoc local signature")
    elseif(WIN32)
        enable_language(RC)
        configure_file(
            ${PROJECT_SOURCE_DIR}/cmake/icad-viewer.rc.in
            ${PROJECT_BINARY_DIR}/generated/icad-viewer.rc
            @ONLY)
        target_sources(icad-viewer PRIVATE ${PROJECT_BINARY_DIR}/generated/icad-viewer.rc)
    endif()
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ICAD_VIEWER_IPO_SUPPORTED OUTPUT ICAD_VIEWER_IPO_ERROR)
    if(ICAD_VIEWER_IPO_SUPPORTED)
        set_property(TARGET icad-viewer PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
        set_property(TARGET icad-viewer PROPERTY INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL TRUE)
    endif()
    qt_finalize_executable(icad-viewer)
    if(APPLE)
        if(ICAD_MACOS_CODESIGN_IDENTITY STREQUAL "-")
            add_custom_command(TARGET icad-viewer POST_BUILD
                COMMAND /usr/bin/codesign --force --deep --options runtime
                        --entitlements ${PROJECT_SOURCE_DIR}/cmake/icad-viewer-adhoc.entitlements.plist
                        --sign - $<TARGET_BUNDLE_DIR:icad-viewer>
                COMMENT "Ad-hoc signing the build-tree ICAD Studio bundle")
        else()
            add_custom_command(TARGET icad-viewer POST_BUILD
                COMMAND /usr/bin/codesign --force --deep --options runtime --timestamp
                        --entitlements ${PROJECT_SOURCE_DIR}/cmake/icad-viewer-adhoc.entitlements.plist
                        --sign ${ICAD_MACOS_CODESIGN_IDENTITY} $<TARGET_BUNDLE_DIR:icad-viewer>
                COMMENT "Signing the build-tree ICAD Studio bundle")
        endif()
    endif()
    install(TARGETS icad-viewer
            BUNDLE DESTINATION . COMPONENT Viewer
            RUNTIME DESTINATION bin COMPONENT Viewer)
    install(FILES ${PROJECT_SOURCE_DIR}/README.md ${PROJECT_SOURCE_DIR}/LICENSE
            DESTINATION share/icad
            COMPONENT Viewer)
    install(FILES ${PROJECT_SOURCE_DIR}/assets/icons/icad-256.png
            DESTINATION share/icons/hicolor/256x256/apps
            RENAME icad.png
            COMPONENT Viewer)
    if(UNIX AND NOT APPLE)
        install(FILES ${PROJECT_SOURCE_DIR}/assets/icad-studio.desktop
                DESTINATION share/applications
                COMPONENT Viewer)
    endif()
    if(APPLE)
        install(IMPORTED_RUNTIME_ARTIFACTS Qt6::QCocoaIntegrationPlugin
                LIBRARY DESTINATION icad-viewer.app/Contents/PlugIns/platforms
                COMPONENT Viewer)
        qt_generate_deploy_script(
            TARGET icad-viewer
            OUTPUT_SCRIPT icad_viewer_deploy_script
            CONTENT [[
qt_deploy_runtime_dependencies(
    EXECUTABLE "$<TARGET_FILE_NAME:icad-viewer>.app"
    ADDITIONAL_LIBRARIES
        "icad-viewer.app/Contents/PlugIns/platforms/$<TARGET_FILE_NAME:Qt6::QCocoaIntegrationPlugin>"
    NO_PLUGINS
    NO_TRANSLATIONS
)
]])
    else()
        qt_generate_deploy_app_script(
            TARGET icad-viewer
            OUTPUT_SCRIPT icad_viewer_deploy_script
            NO_TRANSLATIONS
            NO_UNSUPPORTED_PLATFORM_ERROR
        )
    endif()
    install(SCRIPT ${icad_viewer_deploy_script} COMPONENT Viewer)
    if(APPLE)
        install(CODE "
            set(icad_viewer_app \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/icad-viewer.app\")
            if(\"${ICAD_MACOS_CODESIGN_IDENTITY}\" STREQUAL \"-\")
                execute_process(
                    COMMAND /usr/bin/codesign --force --deep --options runtime
                            --entitlements \"${PROJECT_SOURCE_DIR}/cmake/icad-viewer-adhoc.entitlements.plist\"
                            --sign - \"\${icad_viewer_app}\"
                    RESULT_VARIABLE icad_sign_result
                    OUTPUT_VARIABLE icad_sign_output
                    ERROR_VARIABLE icad_sign_error)
            else()
                execute_process(
                    COMMAND /usr/bin/codesign --force --deep --options runtime --timestamp
                            --entitlements \"${PROJECT_SOURCE_DIR}/cmake/icad-viewer-adhoc.entitlements.plist\"
                            --sign \"${ICAD_MACOS_CODESIGN_IDENTITY}\" \"\${icad_viewer_app}\"
                    RESULT_VARIABLE icad_sign_result
                    OUTPUT_VARIABLE icad_sign_output
                    ERROR_VARIABLE icad_sign_error)
            endif()
            if(NOT icad_sign_result EQUAL 0)
                message(FATAL_ERROR \"ICAD Studio signing failed: \${icad_sign_error}\${icad_sign_output}\")
            endif()
            message(STATUS \"Signed ICAD Studio with identity '${ICAD_MACOS_CODESIGN_IDENTITY}'\")
        " COMPONENT Viewer)
    endif()
endif()
