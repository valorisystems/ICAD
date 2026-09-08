if(NOT DEFINED ICAD_EXECUTABLE OR NOT DEFINED ICAD_SOURCE OR NOT DEFINED OUTPUT_ROOT)
    message(FATAL_ERROR "assembly-semantics benchmark is missing required paths")
endif()
file(REMOVE_RECURSE "${OUTPUT_ROOT}")
file(MAKE_DIRECTORY "${OUTPUT_ROOT}")

execute_process(
    COMMAND "${ICAD_EXECUTABLE}" validate "${ICAD_SOURCE}"
    RESULT_VARIABLE validate_result OUTPUT_VARIABLE validate_output ERROR_VARIABLE validate_error
)
if(NOT validate_result EQUAL 0 OR NOT validate_output MATCHES "CONSTRAINT seated PASS" OR
   NOT validate_output MATCHES "CONSTRAINT aligned PASS")
    message(FATAL_ERROR "assembly mate validation mismatch: ${validate_error}${validate_output}")
endif()

execute_process(
    COMMAND "${ICAD_EXECUTABLE}" inspect-json "${ICAD_SOURCE}"
    RESULT_VARIABLE inspect_result OUTPUT_VARIABLE inspect_output ERROR_VARIABLE inspect_error
)
if(NOT inspect_result EQUAL 0 OR NOT inspect_output MATCHES "\"mates\":2" OR
   NOT inspect_output MATCHES "\"targetKind\":\"JOINT\"" OR
   NOT inspect_output MATCHES "\"surfaceContactOnlyPartPairs\":1")
    message(FATAL_ERROR "assembly semantics inspection mismatch: ${inspect_error}${inspect_output}")
endif()

execute_process(
    COMMAND "${ICAD_EXECUTABLE}" build "${ICAD_SOURCE}" --output-dir "${OUTPUT_ROOT}"
    RESULT_VARIABLE build_result OUTPUT_VARIABLE build_output ERROR_VARIABLE build_error
)
if(NOT build_result EQUAL 0 OR NOT build_output MATCHES "BUILD components=2 solids=2")
    message(FATAL_ERROR "assembly semantics build mismatch: ${build_error}${build_output}")
endif()

file(READ "${OUTPUT_ROOT}/assembly_semantics.scene.json" scene_output)
string(FIND "${scene_output}" "\"targetKind\":\"JOINT\"" joint_track_position)
string(FIND "${scene_output}" "\"pivotMm\":[0,0,100]" pivot_position)
if(joint_track_position EQUAL -1 OR pivot_position EQUAL -1)
    message(FATAL_ERROR "joint-driven scene metadata mismatch")
endif()
file(READ "${OUTPUT_ROOT}/assembly_semantics.bom.json" bom_output)
string(FIND "${bom_output}" "\"occurrences\":[\"link_definition\",\"upper_link\"]" occurrence_position)
string(FIND "${bom_output}" "\"definition\":\"link_definition\"" definition_position)
if(occurrence_position EQUAL -1 OR definition_position EQUAL -1)
    message(FATAL_ERROR "assembly BOM omitted occurrence-to-definition ownership")
endif()
file(READ "${OUTPUT_ROOT}/assembly_semantics.manufacturing.json" manufacturing_output)
string(FIND "${manufacturing_output}" "\"issues\":[]" issues_position)
if(issues_position EQUAL -1)
    message(FATAL_ERROR "assembly instance did not inherit its manufacturing material")
endif()

execute_process(
    COMMAND "${ICAD_EXECUTABLE}" inspect-step "${OUTPUT_ROOT}/assembly_semantics.step"
    RESULT_VARIABLE step_result OUTPUT_VARIABLE step_output ERROR_VARIABLE step_error
)
if(NOT step_result EQUAL 0 OR NOT step_output MATCHES "STEP_SOLIDS 2")
    message(FATAL_ERROR "assembly semantics STEP mismatch: ${step_error}${step_output}")
endif()

message(STATUS "assembly semantics passed: face/edge mates, solid contact classification, joint animation")
