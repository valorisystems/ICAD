set(ICAD_UNIT_TEST_TARGETS)
function(icad_add_unit_test component source)
    set(target "icad_${component}_test")
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE ICAD::compiler icad_project_options)
    add_test(NAME "unit.${component}" COMMAND ${target})
    set_tests_properties("unit.${component}" PROPERTIES LABELS "unit;${component}")
    list(APPEND ICAD_UNIT_TEST_TARGETS ${target})
    set(ICAD_UNIT_TEST_TARGETS "${ICAD_UNIT_TEST_TARGETS}" PARENT_SCOPE)
endfunction()

icad_add_unit_test(lexer unit/lexer_test.cpp)
icad_add_unit_test(language unit/language_test.cpp)
icad_add_unit_test(expression unit/expression_test.cpp)
icad_add_unit_test(parser unit/parser_test.cpp)
icad_add_unit_test(units unit/units_test.cpp)
icad_add_unit_test(resolver unit/resolver_test.cpp)
icad_add_unit_test(semantic unit/semantic_test.cpp)
icad_add_unit_test(geometry unit/geometry_test.cpp)
icad_add_unit_test(geometry_fidelity unit/geometry_fidelity_test.cpp)
icad_add_unit_test(boolean unit/boolean_test.cpp)
icad_add_unit_test(modeling_tools unit/modeling_tools_test.cpp)
icad_add_unit_test(topology_query unit/topology_query_test.cpp)
icad_add_unit_test(surfaces unit/surfaces_test.cpp)
icad_add_unit_test(sketch_solver unit/sketch_solver_test.cpp)
icad_add_unit_test(feature_history unit/feature_history_test.cpp)
icad_add_unit_test(multi_shape_sketch unit/multi_shape_sketch_test.cpp)
add_executable(icad_engine_api_test unit/engine_api_test.cpp)
target_link_libraries(icad_engine_api_test PRIVATE ICAD::engine icad_project_options)
add_test(NAME unit.engine_api COMMAND icad_engine_api_test)
set_tests_properties(unit.engine_api PROPERTIES LABELS "unit;engine")
list(APPEND ICAD_UNIT_TEST_TARGETS icad_engine_api_test)
icad_add_unit_test(incremental unit/incremental_test.cpp)
icad_add_unit_test(importer unit/importer_test.cpp)
icad_add_unit_test(queries unit/queries_test.cpp)
icad_add_unit_test(instances unit/instances_test.cpp)
icad_add_unit_test(assembly_semantics unit/assembly_semantics_test.cpp)
icad_add_unit_test(assembly_interfaces unit/assembly_interfaces_test.cpp)
add_test(NAME benchmark.incremental COMMAND icad_incremental_test)
set_tests_properties(benchmark.incremental PROPERTIES LABELS "benchmark" TIMEOUT 60)

add_executable(icad_turbojet_live_refresh_benchmark
    benchmark/turbojet_live_refresh.cpp)
target_link_libraries(icad_turbojet_live_refresh_benchmark PRIVATE
    ICAD::compiler icad_project_options)
if(ICAD_ENABLE_SANITIZERS)
    # Instrumentation makes geometry timing non-representative. Keep the complete
    # model and incremental correctness checks active without treating sanitizer
    # overhead as a production performance regression.
    target_compile_definitions(icad_turbojet_live_refresh_benchmark PRIVATE
        ICAD_TURBOJET_TIMING_GATES=0)
elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
    # The hosted Ubuntu Debug baseline is about 24.2 s for the full 358-body
    # preview; keep a measured 30 s regression ceiling while Release stays at
    # the stricter defaults declared by the benchmark.
    target_compile_definitions(icad_turbojet_live_refresh_benchmark PRIVATE
        ICAD_TURBOJET_INITIAL_BUDGET_MS=30000
        ICAD_TURBOJET_EDIT_BUDGET_MS=5000)
endif()
add_test(
    NAME benchmark.large_turbojet_refresh
    COMMAND icad_turbojet_live_refresh_benchmark
            ${PROJECT_SOURCE_DIR}/examples/bench/turbojet_engine.icad)
set_tests_properties(benchmark.large_turbojet_refresh PROPERTIES
    LABELS "benchmark;viewer;incremental;large"
    TIMEOUT 60)
if(ICAD_ENABLE_SANITIZERS)
    set_tests_properties(benchmark.large_turbojet_refresh PROPERTIES TIMEOUT 120)
endif()
icad_add_unit_test(intersection unit/intersection_test.cpp)
icad_add_unit_test(topology unit/topology_test.cpp)
icad_add_unit_test(scene unit/scene_test.cpp)
icad_add_unit_test(materials unit/materials_test.cpp)
icad_add_unit_test(live_viewer unit/live_viewer_test.cpp)
icad_add_unit_test(profile unit/profile_test.cpp)
icad_add_unit_test(constraints unit/constraints_test.cpp)
icad_add_unit_test(mechanism unit/mechanism_test.cpp)
icad_add_unit_test(engineering unit/engineering_test.cpp)
icad_add_unit_test(agent unit/agent_test.cpp)
icad_add_unit_test(json unit/json_test.cpp)
icad_add_unit_test(evidence unit/evidence_test.cpp)
icad_add_unit_test(mcp unit/mcp_test.cpp)
icad_add_unit_test(source_document unit/source_document_test.cpp)
icad_add_unit_test(fuzz_smoke unit/fuzz_smoke_test.cpp)
icad_add_unit_test(agent_workflow unit/agent_workflow_test.cpp)
set_tests_properties(unit.fuzz_smoke PROPERTIES LABELS "unit;fuzz" TIMEOUT 30)

add_test(
    NAME integration.cli_check_valid
    COMMAND ${CMAKE_COMMAND}
        -DICAD_EXECUTABLE=$<TARGET_FILE:icad>
        -DICAD_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/valid/minimal.icad
        -DEXPECT_SUCCESS=ON
        -P ${CMAKE_CURRENT_SOURCE_DIR}/integration/cli_check.cmake
)
set_tests_properties(integration.cli_check_valid PROPERTIES LABELS "integration")

add_test(
    NAME integration.cli_check_invalid
    COMMAND ${CMAKE_COMMAND}
        -DICAD_EXECUTABLE=$<TARGET_FILE:icad>
        -DICAD_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/invalid/unexpected_character.icad
        -DEXPECT_SUCCESS=OFF
        -DEXPECT_CODE=ICAD-L0001
        -P ${CMAKE_CURRENT_SOURCE_DIR}/integration/cli_check.cmake
)
set_tests_properties(integration.cli_check_invalid PROPERTIES LABELS "integration")

add_test(
    NAME integration.cli_check_semantic_error
    COMMAND ${CMAKE_COMMAND}
        -DICAD_EXECUTABLE=$<TARGET_FILE:icad>
        -DICAD_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/invalid/missing_height.icad
        -DEXPECT_SUCCESS=OFF
        -DEXPECT_CODE=ICAD-S0010
        -P ${CMAKE_CURRENT_SOURCE_DIR}/integration/cli_check.cmake
)
set_tests_properties(integration.cli_check_semantic_error PROPERTIES LABELS "integration")

add_test(
    NAME integration.cli_check_unsupported_language
    COMMAND ${CMAKE_COMMAND}
        -DICAD_EXECUTABLE=$<TARGET_FILE:icad>
        -DICAD_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/invalid/unsupported_language.icad
        -DEXPECT_SUCCESS=OFF
        -DEXPECT_CODE=ICAD-C0002
        -P ${CMAKE_CURRENT_SOURCE_DIR}/integration/cli_check.cmake
)
set_tests_properties(integration.cli_check_unsupported_language
                     PROPERTIES LABELS "integration;language")

add_test(
    NAME integration.cli_language
    COMMAND ${CMAKE_COMMAND}
        -DICAD_EXECUTABLE=$<TARGET_FILE:icad>
        -P ${CMAKE_CURRENT_SOURCE_DIR}/integration/cli_language.cmake
)
set_tests_properties(integration.cli_language PROPERTIES LABELS "integration;language")

add_test(
    NAME integration.cli_evidence
    COMMAND ${CMAKE_COMMAND}
        -DICAD_EXECUTABLE=$<TARGET_FILE:icad>
        -DICAD_SOURCE=${PROJECT_SOURCE_DIR}/examples/bench/turbojet_engine.icad
        -DICAD_MANIFEST=${PROJECT_SOURCE_DIR}/examples/bench/turbojet_engine.evidence.json
        -DOUTPUT_ROOT=${CMAKE_CURRENT_BINARY_DIR}/evidence
        -P ${CMAKE_CURRENT_SOURCE_DIR}/integration/cli_evidence.cmake
)
set_tests_properties(integration.cli_evidence PROPERTIES
    LABELS "integration;evidence;turbojet"
    TIMEOUT 60)

add_test(
    NAME integration.turbojet_baseline
    COMMAND ${CMAKE_COMMAND}
        -DICAD_SOURCE=${PROJECT_SOURCE_DIR}/examples/bench/turbojet_engine.icad
        -DBASELINE_PATCH=${PROJECT_SOURCE_DIR}/examples/bench/baseline/R3_SOURCE_FROM_DEVELOPMENT.patch
        -DOUTPUT_ROOT=${CMAKE_CURRENT_BINARY_DIR}/turbojet-baseline
        -P ${CMAKE_CURRENT_SOURCE_DIR}/integration/turbojet_baseline.cmake
)
set_tests_properties(integration.turbojet_baseline PROPERTIES
    LABELS "integration;evidence;turbojet;baseline")

add_test(
    NAME integration.hall_clinker_bom
    COMMAND ${CMAKE_COMMAND}
        -DICAD_EXECUTABLE=$<TARGET_FILE:icad>
        -DICAD_SOURCE=${PROJECT_SOURCE_DIR}/examples/bench/hall_clinker/hall_clinker.icad
        -P ${CMAKE_CURRENT_SOURCE_DIR}/integration/hall_clinker_bom.cmake
)
set_tests_properties(integration.hall_clinker_bom PROPERTIES
    LABELS "integration;hangar;bom;manufacturing;materials"
    TIMEOUT 60)

add_test(
    NAME integration.cli_check_advanced_sketch_constraints
    COMMAND ${CMAKE_COMMAND}
        -DICAD_EXECUTABLE=$<TARGET_FILE:icad>
        -DICAD_SOURCE=${PROJECT_SOURCE_DIR}/examples/advanced_sketch_constraints.icad
        -DEXPECT_SUCCESS=ON
        -P ${CMAKE_CURRENT_SOURCE_DIR}/integration/cli_check.cmake
)
set_tests_properties(integration.cli_check_advanced_sketch_constraints
                     PROPERTIES LABELS "integration;sketch_solver;region")

add_test(
    NAME integration.cli_compare
    COMMAND ${CMAKE_COMMAND}
        -DICAD_EXECUTABLE=$<TARGET_FILE:icad>
        -DFIRST_SOURCE=${PROJECT_SOURCE_DIR}/examples/minimal.icad
        -DSECOND_SOURCE=${PROJECT_SOURCE_DIR}/examples/robotic_arm.icad
        -P ${CMAKE_CURRENT_SOURCE_DIR}/integration/cli_compare.cmake
)
set_tests_properties(integration.cli_compare PROPERTIES LABELS "integration;agent")

add_test(
    NAME integration.release_metadata
    COMMAND ${CMAKE_COMMAND}
        -DPROJECT_ROOT=${PROJECT_SOURCE_DIR}
        -DICAD_EXECUTABLE=$<TARGET_FILE:icad>
        -DEXPECTED_PROJECT_VERSION=${PROJECT_VERSION}
        -DEXPECTED_RELEASE_VERSION=${ICAD_RELEASE_VERSION}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/integration/release_metadata.cmake
)
set_tests_properties(integration.release_metadata PROPERTIES LABELS "integration;release")

add_test(
    NAME sandbox.clean_working_directory
    COMMAND ${CMAKE_COMMAND}
        -DICAD_EXECUTABLE=$<TARGET_FILE:icad>
        -DICAD_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/valid/minimal.icad
        -DSANDBOX_ROOT=${CMAKE_CURRENT_BINARY_DIR}/sandbox-runtime
        -P ${CMAKE_CURRENT_SOURCE_DIR}/sandbox/clean_working_directory.cmake
)
set_tests_properties(sandbox.clean_working_directory PROPERTIES LABELS "sandbox")

add_custom_target(test-unit
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -L unit
    DEPENDS ${ICAD_UNIT_TEST_TARGETS}
    USES_TERMINAL
)

add_test(
    NAME benchmark.advanced_bridge
    COMMAND ${CMAKE_COMMAND}
        -DICAD_EXECUTABLE=$<TARGET_FILE:icad>
        -DICAD_SOURCE=${PROJECT_SOURCE_DIR}/examples/advanced.icad
        -DOUTPUT_ROOT=${CMAKE_CURRENT_BINARY_DIR}/benchmark-advanced
        -P ${CMAKE_CURRENT_SOURCE_DIR}/benchmark/advanced_bridge.cmake
)
set_tests_properties(benchmark.advanced_bridge PROPERTIES LABELS "benchmark" TIMEOUT 60)
if(TARGET icad-viewer)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets)
    add_executable(icad_viewer_geometry_test
        unit/viewer_geometry_test.cpp
        ${PROJECT_SOURCE_DIR}/src/desktop_viewer/scene_model.cpp)
    target_include_directories(icad_viewer_geometry_test PRIVATE
        ${PROJECT_SOURCE_DIR}/src/desktop_viewer)
    target_link_libraries(icad_viewer_geometry_test PRIVATE
        ICAD::compiler Qt6::Core Qt6::Gui icad_project_options)
    add_test(NAME unit.viewer_geometry COMMAND icad_viewer_geometry_test)
    set_tests_properties(unit.viewer_geometry PROPERTIES LABELS "unit;viewer;geometry")
    list(APPEND ICAD_UNIT_TEST_TARGETS icad_viewer_geometry_test)

    add_executable(icad_viewer_editor_test
        unit/viewer_editor_test.cpp
        ${PROJECT_SOURCE_DIR}/src/desktop_viewer/icad_editor.cpp)
    target_include_directories(icad_viewer_editor_test PRIVATE
        ${PROJECT_SOURCE_DIR}/src/desktop_viewer)
    target_link_libraries(icad_viewer_editor_test PRIVATE
        Qt6::Core Qt6::Gui Qt6::Widgets icad_project_options)
    add_test(NAME unit.viewer_editor COMMAND icad_viewer_editor_test)
    set_tests_properties(unit.viewer_editor PROPERTIES
        LABELS "unit;viewer;editor"
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    list(APPEND ICAD_UNIT_TEST_TARGETS icad_viewer_editor_test)

    add_test(
        NAME viewer.qt_native_smoke
        COMMAND $<TARGET_FILE:icad-viewer> --self-test ${PROJECT_SOURCE_DIR}/examples/advanced.icad
    )
    set_tests_properties(viewer.qt_native_smoke PROPERTIES
        LABELS "viewer;qt;native"
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 60)
    add_test(
        NAME viewer.qt_window_construction
        COMMAND $<TARGET_FILE:icad-viewer> --window-test --view front
                ${PROJECT_SOURCE_DIR}/examples/selective_round_vessel.icad
                ${PROJECT_SOURCE_DIR}/examples/assembly_semantics.icad
    )
    set_tests_properties(viewer.qt_window_construction PROPERTIES
        LABELS "viewer;qt;native;window"
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 60)
    if(APPLE)
        add_test(
            NAME viewer.macos_signature
            COMMAND /usr/bin/codesign --verify --deep --strict
                    $<TARGET_BUNDLE_DIR:icad-viewer>)
        set_tests_properties(viewer.macos_signature PROPERTIES
            LABELS "viewer;release;macos")
    endif()
endif()
add_test(
    NAME benchmark.robotic_arm
    COMMAND ${CMAKE_COMMAND}
        -DICAD_EXECUTABLE=$<TARGET_FILE:icad>
        -DICAD_SOURCE=${PROJECT_SOURCE_DIR}/examples/robotic_arm.icad
        -DREFERENCE_ROOT=${PROJECT_SOURCE_DIR}/examples/Robotic_Arm_3D_Model
        -DPROJECT_ROOT=${PROJECT_SOURCE_DIR}
        -DOUTPUT_ROOT=${CMAKE_CURRENT_BINARY_DIR}/benchmark-robotic-arm
        -P ${CMAKE_CURRENT_SOURCE_DIR}/benchmark/robotic_arm.cmake
)
set_tests_properties(benchmark.robotic_arm PROPERTIES LABELS "benchmark" TIMEOUT 60)
add_executable(icad_live_viewer_refresh_test benchmark/live_viewer_refresh.cpp)
target_link_libraries(icad_live_viewer_refresh_test PRIVATE ICAD::compiler icad_project_options)
add_test(
    NAME benchmark.live_viewer_refresh
    COMMAND icad_live_viewer_refresh_test ${PROJECT_SOURCE_DIR}/examples/robotic_arm.icad
)
set_tests_properties(benchmark.live_viewer_refresh
                     PROPERTIES LABELS "benchmark;viewer;incremental" TIMEOUT 60)
add_test(
    NAME benchmark.boolean_showcase
    COMMAND ${CMAKE_COMMAND}
        -DICAD_EXECUTABLE=$<TARGET_FILE:icad>
        -DICAD_SOURCE=${PROJECT_SOURCE_DIR}/examples/boolean_showcase.icad
        -DOUTPUT_ROOT=${CMAKE_CURRENT_BINARY_DIR}/benchmark-boolean
        -P ${CMAKE_CURRENT_SOURCE_DIR}/benchmark/boolean_showcase.cmake
)
set_tests_properties(benchmark.boolean_showcase PROPERTIES LABELS "benchmark" TIMEOUT 60)
add_test(
    NAME benchmark.modeling_tools
    COMMAND ${CMAKE_COMMAND}
        -DICAD_EXECUTABLE=$<TARGET_FILE:icad>
        -DICAD_SOURCE=${PROJECT_SOURCE_DIR}/examples/modeling_tools.icad
        -DOUTPUT_ROOT=${CMAKE_CURRENT_BINARY_DIR}/benchmark-modeling-tools
        -P ${CMAKE_CURRENT_SOURCE_DIR}/benchmark/modeling_tools.cmake
)
set_tests_properties(benchmark.modeling_tools PROPERTIES LABELS "benchmark" TIMEOUT 60)
add_test(
    NAME benchmark.advanced_surfaces
    COMMAND ${CMAKE_COMMAND}
        -DICAD_EXECUTABLE=$<TARGET_FILE:icad>
        -DICAD_SOURCE=${PROJECT_SOURCE_DIR}/examples/advanced_surfaces.icad
        -DOUTPUT_ROOT=${CMAKE_CURRENT_BINARY_DIR}/benchmark-advanced-surfaces
        -P ${CMAKE_CURRENT_SOURCE_DIR}/benchmark/advanced_surfaces.cmake
)
set_tests_properties(benchmark.advanced_surfaces PROPERTIES LABELS "benchmark" TIMEOUT 60)
add_test(
    NAME benchmark.constrained_sketch
    COMMAND ${CMAKE_COMMAND}
        -DICAD_EXECUTABLE=$<TARGET_FILE:icad>
        -DICAD_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/valid/constrained_sketch.icad
        -DOUTPUT_ROOT=${CMAKE_CURRENT_BINARY_DIR}/benchmark-constrained-sketch
        -P ${CMAKE_CURRENT_SOURCE_DIR}/benchmark/constrained_sketch.cmake
)
set_tests_properties(benchmark.constrained_sketch PROPERTIES LABELS "benchmark" TIMEOUT 60)
add_test(
    NAME benchmark.sketch_history
    COMMAND ${CMAKE_COMMAND}
        -DICAD_EXECUTABLE=$<TARGET_FILE:icad>
        -DICAD_SOURCE=${PROJECT_SOURCE_DIR}/examples/sketch_history.icad
        -DOUTPUT_ROOT=${CMAKE_CURRENT_BINARY_DIR}/benchmark-sketch-history
        -P ${CMAKE_CURRENT_SOURCE_DIR}/benchmark/sketch_history.cmake
)
set_tests_properties(benchmark.sketch_history
                     PROPERTIES LABELS "benchmark;feature_history" TIMEOUT 60)
add_test(
    NAME benchmark.multi_shape_plate
    COMMAND ${CMAKE_COMMAND}
        -DICAD_EXECUTABLE=$<TARGET_FILE:icad>
        -DICAD_SOURCE=${PROJECT_SOURCE_DIR}/examples/multi_shape_plate.icad
        -DOUTPUT_ROOT=${CMAKE_CURRENT_BINARY_DIR}/benchmark-multi-shape-plate
        -P ${CMAKE_CURRENT_SOURCE_DIR}/benchmark/multi_shape_plate.cmake
)
set_tests_properties(benchmark.multi_shape_plate
                     PROPERTIES LABELS "benchmark;feature_history;multi_shape_sketch" TIMEOUT 60)
add_test(
    NAME benchmark.region_plate
    COMMAND ${CMAKE_COMMAND}
        -DICAD_EXECUTABLE=$<TARGET_FILE:icad>
        -DICAD_SOURCE=${PROJECT_SOURCE_DIR}/examples/region_plate.icad
        -DOUTPUT_ROOT=${CMAKE_CURRENT_BINARY_DIR}/benchmark-region-plate
        -P ${CMAKE_CURRENT_SOURCE_DIR}/benchmark/region_plate.cmake
)
set_tests_properties(benchmark.region_plate
                     PROPERTIES LABELS "benchmark;feature_history;multi_shape_sketch;region" TIMEOUT 60)
add_test(
    NAME benchmark.persistent_face_plate
    COMMAND ${CMAKE_COMMAND}
        -DICAD_EXECUTABLE=$<TARGET_FILE:icad>
        -DICAD_SOURCE=${PROJECT_SOURCE_DIR}/examples/persistent_face_plate.icad
        -DOUTPUT_ROOT=${CMAKE_CURRENT_BINARY_DIR}/benchmark-persistent-face-plate
        -P ${CMAKE_CURRENT_SOURCE_DIR}/benchmark/persistent_face_plate.cmake
)
set_tests_properties(benchmark.persistent_face_plate
                     PROPERTIES LABELS "benchmark;feature_history;persistent_topology" TIMEOUT 60)
add_test(
    NAME benchmark.rounded_tangent_plate
    COMMAND ${CMAKE_COMMAND}
        -DICAD_EXECUTABLE=$<TARGET_FILE:icad>
        -DICAD_SOURCE=${PROJECT_SOURCE_DIR}/examples/rounded_tangent_plate.icad
        -DOUTPUT_ROOT=${CMAKE_CURRENT_BINARY_DIR}/benchmark-rounded-tangent-plate
        -P ${CMAKE_CURRENT_SOURCE_DIR}/benchmark/rounded_tangent_plate.cmake
)
set_tests_properties(benchmark.rounded_tangent_plate
                     PROPERTIES LABELS "benchmark;sketch_solver;tangency" TIMEOUT 60)
add_test(
    NAME benchmark.selective_round_vessel
    COMMAND ${CMAKE_COMMAND}
        -DICAD_EXECUTABLE=$<TARGET_FILE:icad>
        -DICAD_SOURCE=${PROJECT_SOURCE_DIR}/examples/selective_round_vessel.icad
        -DOUTPUT_ROOT=${CMAKE_CURRENT_BINARY_DIR}/benchmark-selective-round-vessel
        -P ${CMAKE_CURRENT_SOURCE_DIR}/benchmark/selective_round_vessel.cmake
)
set_tests_properties(benchmark.selective_round_vessel
                     PROPERTIES LABELS "benchmark;modeling_tools;selection;fillet" TIMEOUT 60)
add_test(
    NAME benchmark.topology_query_vessel
    COMMAND ${CMAKE_COMMAND}
        -DICAD_EXECUTABLE=$<TARGET_FILE:icad>
        -DICAD_SOURCE=${PROJECT_SOURCE_DIR}/examples/topology_query_vessel.icad
        -DOUTPUT_ROOT=${CMAKE_CURRENT_BINARY_DIR}/benchmark-topology-query-vessel
        -P ${CMAKE_CURRENT_SOURCE_DIR}/benchmark/topology_query_vessel.cmake
)
set_tests_properties(benchmark.topology_query_vessel
                     PROPERTIES LABELS "benchmark;topology_query;selection;fillet" TIMEOUT 60)
add_test(
    NAME benchmark.geometric_queries
    COMMAND ${CMAKE_COMMAND}
        -DICAD_EXECUTABLE=$<TARGET_FILE:icad>
        -DICAD_SOURCE=${PROJECT_SOURCE_DIR}/examples/geometric_queries.icad
        -P ${CMAKE_CURRENT_SOURCE_DIR}/benchmark/geometric_queries.cmake
)
set_tests_properties(benchmark.geometric_queries PROPERTIES LABELS "benchmark" TIMEOUT 60)
add_test(
    NAME benchmark.assembly_instances
    COMMAND ${CMAKE_COMMAND}
        -DICAD_EXECUTABLE=$<TARGET_FILE:icad>
        -DICAD_SOURCE=${PROJECT_SOURCE_DIR}/examples/assembly_instances.icad
        -DOUTPUT_ROOT=${CMAKE_CURRENT_BINARY_DIR}/benchmark-assembly-instances
        -P ${CMAKE_CURRENT_SOURCE_DIR}/benchmark/assembly_instances.cmake
)
set_tests_properties(benchmark.assembly_instances PROPERTIES LABELS "benchmark" TIMEOUT 60)
add_test(
    NAME benchmark.assembly_semantics
    COMMAND ${CMAKE_COMMAND}
        -DICAD_EXECUTABLE=$<TARGET_FILE:icad>
        -DICAD_SOURCE=${PROJECT_SOURCE_DIR}/examples/assembly_semantics.icad
        -DOUTPUT_ROOT=${CMAKE_CURRENT_BINARY_DIR}/benchmark-assembly-semantics
        -P ${CMAKE_CURRENT_SOURCE_DIR}/benchmark/assembly_semantics.cmake
)
set_tests_properties(benchmark.assembly_semantics PROPERTIES LABELS "benchmark" TIMEOUT 60)
add_test(
    NAME benchmark.agentic_robot_prompt
    COMMAND ${CMAKE_COMMAND}
        -DICAD_EXECUTABLE=$<TARGET_FILE:icad>
        -DREFERENCE_ROOT=${PROJECT_SOURCE_DIR}/examples/Robotic_Arm_3D_Model
        -DPROJECT_ROOT=${PROJECT_SOURCE_DIR}
        -DOUTPUT_ROOT=${CMAKE_CURRENT_BINARY_DIR}/benchmark-agentic-robot-prompt
        -P ${CMAKE_CURRENT_SOURCE_DIR}/benchmark/agentic_robot_prompt.cmake
)
set_tests_properties(benchmark.agentic_robot_prompt PROPERTIES LABELS "benchmark;agent" TIMEOUT 60)
add_custom_target(benchmark
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -L benchmark
    DEPENDS icad icad_incremental_test
    USES_TERMINAL
)
add_custom_target(test-integration
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -L integration
    DEPENDS icad
    USES_TERMINAL
)
add_custom_target(test-sandbox
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -L sandbox
    DEPENDS icad
    USES_TERMINAL
)
