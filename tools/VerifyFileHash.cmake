if(NOT DEFINED VALCRAFT_FILE OR NOT DEFINED VALCRAFT_EXPECTED_SHA256)
    message(FATAL_ERROR
        "VALCRAFT_FILE and VALCRAFT_EXPECTED_SHA256 are required"
    )
endif()

if(NOT EXISTS "${VALCRAFT_FILE}")
    message(FATAL_ERROR "Generated file is missing: ${VALCRAFT_FILE}")
endif()

file(SHA256 "${VALCRAFT_FILE}" actual_sha256)
string(TOLOWER "${VALCRAFT_EXPECTED_SHA256}" expected_sha256)
if(NOT actual_sha256 STREQUAL expected_sha256)
    message(FATAL_ERROR
        "Generated file '${VALCRAFT_FILE}' has SHA-256 '${actual_sha256}', "
        "expected '${expected_sha256}'."
    )
endif()
