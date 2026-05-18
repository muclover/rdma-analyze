cmake_minimum_required(VERSION 3.10)

project(OBMM_SO C)

# Define version file path
set(VERSION_FILE_PATH ${CMAKE_CURRENT_LIST_DIR}/../../VERSION)

# Read version from VERSION file
file(READ ${VERSION_FILE_PATH} OBMM_VERSION)
string(STRIP ${OBMM_VERSION} OBMM_VERSION)

# Parse version into major, minor, patch
string(REGEX REPLACE "([0-9]+)\.([0-9]+)\.([0-9]+)" "\\1" OBMM_VERSION_MAJOR ${OBMM_VERSION})
string(REGEX REPLACE "([0-9]+)\.([0-9]+)\.([0-9]+)" "\\2" OBMM_VERSION_MINOR ${OBMM_VERSION})
string(REGEX REPLACE "([0-9]+)\.([0-9]+)\.([0-9]+)" "\\3" OBMM_VERSION_PATCH ${OBMM_VERSION})

# Set SOVERSION to major version (common practice for library versioning)
set(OBMM_SOVERSION ${OBMM_VERSION_MAJOR})

aux_source_directory(${CMAKE_CURRENT_LIST_DIR} LIBOBMM_DIR_SRCS)
add_library(OBMM_SO SHARED ${LIBOBMM_DIR_SRCS})
target_link_libraries(OBMM_SO PRIVATE)

target_include_directories(OBMM_SO PRIVATE ${CMAKE_CURRENT_LIST_DIR})

set(OBMM_NAME obmm)
set_target_properties(OBMM_SO PROPERTIES OUTPUT_NAME ${OBMM_NAME}
	VERSION ${OBMM_VERSION}
	SOVERSION ${OBMM_SOVERSION})

target_compile_options(OBMM_SO PRIVATE -Wall -Wextra -Wfloat-equal -fno-common -std=gnu99 -fvisibility=hidden
	-Wuninitialized -Wno-error -Wno-error=format -Wundef -Wunused -Wdate-time -Wshadow -Wvla
	-Wdisabled-optimization -Wempty-body -Wignored-qualifiers -Wimplicit-fallthrough=3
	-Wtype-limits -Wshift-negative-value -Wswitch-default -Wframe-larger-than=8192
	-Wshift-overflow=2 -Wwrite-strings -Wmissing-format-attribute -Wformat-nonliteral
	-Wduplicated-cond -Wtrampolines -Wlogical-op -Wsuggest-attribute=format
	-Wduplicated-branches -Wmissing-include-dirs -Wformat-signedness -Wmissing-declarations
	-Wreturn-local-addr -Wredundant-decls -Wfloat-conversion -Wmissing-prototypes
	-Wstrict-prototypes)
if(GCOV)
	# coverage test version
	target_compile_options(OBMM_SO PRIVATE -O0 -fprofile-arcs -ftest-coverage
		-fkeep-inline-functions -fkeep-static-functions)
	target_link_libraries(OBMM_SO PRIVATE gcov)
else()
	# release version
	target_link_options(OBMM_SO PRIVATE -s)
endif()

install(TARGETS OBMM_SO LIBRARY DESTINATION lib64 OPTIONAL)
install(FILES libobmm.h DESTINATION include)