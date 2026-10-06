get_filename_component(CACHE_DIRECTORY "${DESTINATION}" DIRECTORY)
file(MAKE_DIRECTORY "${CACHE_DIRECTORY}")
# Consumers see only complete archives; another editor can publish concurrently.
file(LOCK "${DESTINATION}.lock" GUARD PROCESS TIMEOUT 300)
if(NOT EXISTS "${DESTINATION}")
    configure_file("${SOURCE_FILE}" "${DESTINATION}.tmp" COPYONLY)
    file(RENAME "${DESTINATION}.tmp" "${DESTINATION}")
endif()
