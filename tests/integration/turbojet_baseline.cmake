if(NOT DEFINED ICAD_SOURCE OR NOT DEFINED BASELINE_PATCH OR NOT DEFINED OUTPUT_ROOT)
    message(FATAL_ERROR "ICAD_SOURCE, BASELINE_PATCH, and OUTPUT_ROOT are required")
endif()

set(anchor_sha "ecd9cc86f13f35eccdb8fbbe9d49d04256b4de9c266e673fbb0ba7f9c113bc61")
set(patch_sha "b1f10b2fcc313c5b1f91801f0f113378d25644866a0a88651ccfd7da71581adf")
set(r3_sha "d4050632062a7a3a9e1ff019c15d24aca59bf05adb77ba2df2990645a62dc2da")

file(SHA256 "${ICAD_SOURCE}" actual_anchor_sha)
file(SHA256 "${BASELINE_PATCH}" actual_patch_sha)
if(NOT actual_anchor_sha STREQUAL anchor_sha OR NOT actual_patch_sha STREQUAL patch_sha)
    message(FATAL_ERROR "the immutable R3 reconstruction inputs changed")
endif()

file(READ "${ICAD_SOURCE}" development_source)
set(development_header "# Single-spool turbojet ground-demonstrator DEVELOPMENT model.\n# Current geometry maturity is the preserved R3 packaging benchmark: it is not\n# released for rotating-hardware fabrication, fueled operation, flight, or a\n# certification claim. Accepted partner evidence controls every future change.\n# Design authority: this ICAD source, its adjacent evidence manifest, and\n# DESIGN_PREPARATION.md.")
set(r3_header "# Complete non-flight-certified single-spool turbojet mechanical benchmark.\n# Design authority: this ICAD source plus DESIGN_PREPARATION.md.")
string(REPLACE "${development_header}" "${r3_header}" reconstructed "${development_source}")
string(REPLACE
    "# Editable system parameters and derived design handles. TGD-REQ-ENV-001 fixes\n# the preferred candidate envelope. Running clearances remain Gate-1 open."
    "# Editable system parameters and derived design handles."
    reconstructed "${reconstructed}")
string(REGEX REPLACE "(FASTENER M[68]) QUANTITY [0-9]+" "\\1" reconstructed "${reconstructed}")
string(REPLACE "REQUIRES CAPABILITY MATERIAL_PROFILE_V1\n" "" reconstructed "${reconstructed}")
string(REPLACE "REQUIRES CAPABILITY CALCULATED_BILINGUAL_BOM_V1\n" "" reconstructed "${reconstructed}")
foreach(profile_line IN ITEMS
        "PROFILE KAISER_ALUMINUM_6061_T6_T651_SHEET_PLATE\n"
        "PROFILE ATI_TI_6AL_4V_GRADE5_BAR\n"
        "PROFILE SPECIAL_METALS_INCONEL_718\n"
        "PROFILE OUTOKUMPU_PRODEC_316L_4404\n")
    string(REPLACE "${profile_line}" "" reconstructed "${reconstructed}")
endforeach()
string(REPLACE
    "MATERIAL bearing_steel\nPROFILE OVAKO_100CR6_803D\nPRESET CHROME\nEND\n"
    "MATERIAL bearing_steel CHROME\n"
    reconstructed "${reconstructed}")
string(REPLACE
    "MATERIAL bracket_steel\nPROFILE SSAB_DOMEX_355MC\nPRESET STRUCTURAL_STEEL\nEND\n"
    "MATERIAL bracket_steel STRUCTURAL_STEEL\n"
    reconstructed "${reconstructed}")
file(MAKE_DIRECTORY "${OUTPUT_ROOT}")
file(WRITE "${OUTPUT_ROOT}/turbojet_engine_r3.icad" "${reconstructed}")
file(READ "${OUTPUT_ROOT}/turbojet_engine_r3.icad" reconstructed_bytes)
# CMake may materialize generated text with native Windows newlines. R3 is a
# frozen source definition, so compare its canonical LF byte stream while the
# separately tracked anchor and reconstruction-input files remain exact hashes.
string(REPLACE "\r\n" "\n" canonical_r3 "${reconstructed_bytes}")
string(REPLACE "\r" "\n" canonical_r3 "${canonical_r3}")
string(SHA256 actual_r3_sha "${canonical_r3}")
if(NOT actual_r3_sha STREQUAL r3_sha)
    message(FATAL_ERROR
        "R3 source reconstruction SHA-256 does not match the frozen baseline: ${actual_r3_sha}")
endif()
