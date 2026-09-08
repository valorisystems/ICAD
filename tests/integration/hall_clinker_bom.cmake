if(NOT DEFINED ICAD_EXECUTABLE OR NOT DEFINED ICAD_SOURCE)
    message(FATAL_ERROR "ICAD_EXECUTABLE and ICAD_SOURCE are required")
endif()

function(run_icad output_variable)
    execute_process(
        COMMAND "${ICAD_EXECUTABLE}" ${ARGN} "${ICAD_SOURCE}"
        RESULT_VARIABLE command_result
        OUTPUT_VARIABLE command_output
        ERROR_VARIABLE command_error)
    if(NOT command_result EQUAL 0)
        message(FATAL_ERROR "ICAD command failed: ${ARGN}\n${command_output}\n${command_error}")
    endif()
    set(${output_variable} "${command_output}" PARENT_SCOPE)
endfunction()

run_icad(check_output check)
if(NOT check_output MATCHES "compile check passed")
    message(FATAL_ERROR "Hall source did not pass the compiler check")
endif()

run_icad(manufacturing_output manufacturing)
if(NOT manufacturing_output MATCHES "MANUFACTURING PASS")
    message(FATAL_ERROR "Hall source did not pass manufacturing checks")
endif()

run_icad(interference_output interference-json)
if(NOT interference_output MATCHES "\"unintendedPenetratingPartPairs\":0")
    message(FATAL_ERROR "Hall source contains an unintended penetration")
endif()

run_icad(bom_output bom-json)
foreach(required_text IN ITEMS
        "\"schema\":\"icad.bom.v2\""
        "\"languages\":[\"en\",\"fr\"]"
        "\"fr\":\"Nomenclature calculée\""
        "\"componentOccurrences\":690"
        "\"componentLineItems\":29"
        "\"fastenerQuantity\":682"
        "\"weldRodQuantity\":1994"
        "\"weldFillerMassKg\":70.807"
        "\"weldPackageQuantity\":16"
        "\"model\":\"M36_CLASS_8_8\""
        "\"propertyClass\":\"8.8\""
        "\"model\":\"M27_CLASS_6_8\""
        "\"model\":\"M20_CLASS_6_8\""
        "\"model\":\"LINCOLN_LNT_26_ER70S_6_2_4X1000\""
        "\"body\":\"roof_panel_left_lower_full\""
        "\"body\":\"wall_cladding_left_full\""
        "\"supplierMassPerCoveredAreaKgM2\":6.98"
        "\"procurementReady\":false"
        "L'approvisionnement des fixations")
    string(FIND "${bom_output}" "${required_text}" found_at)
    if(found_at EQUAL -1)
        message(FATAL_ERROR "Hall BOM is missing required evidence: ${required_text}")
    endif()
endforeach()

execute_process(
    COMMAND "${ICAD_EXECUTABLE}" materials-json
    RESULT_VARIABLE material_result
    OUTPUT_VARIABLE material_output
    ERROR_VARIABLE material_error)
if(NOT material_result EQUAL 0 OR
   NOT material_output MATCHES "ARCELORMITTAL_HACIERCO_85_280_S350GD_Z275_HAIRPLUS25_075" OR
   NOT material_output MATCHES "ARCELORMITTAL_TRAPEZA_8_125_25_S350GD_Z275_HAIRPLUS25_075" OR
   NOT material_output MATCHES "LINCOLN_LNT_26_ER70S_6_2_4X1000")
    message(FATAL_ERROR "Hall supplier material profiles are missing\n${material_error}")
endif()
