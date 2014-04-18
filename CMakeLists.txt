set(TOOL_PROJECT scenecvt)
project(${TOOL_PROJECT})

#--------------------------------------------------------------------
# Data
#--------------------------------------------------------------------

#--------------------------------------------------------------------
# Create target
#--------------------------------------------------------------------
set(TOOL_SRC
  main.cpp
  pch.cpp
  pch.h
  )
source_group(
  "Source Files" FILES ${TOOL_SRC}
  )

add_executable(${TOOL_PROJECT} ${TOOL_SRC})
add_lighter_only(${TOOL_PROJECT})
target_precompiled_header(${TOOL_PROJECT} pch.h pch.cpp)

set(ASSIMP_BUILD_STATIC_LIB ON)
set(ASSIMP_NO_EXPORT ON)
add_subdirectory(../assimp ${PROJECT_BINARY_DIR}/assimp EXCLUDE_FROM_ALL)
target_include_directories(${TOOL_PROJECT} PRIVATE ../assimp/include)
target_link_libraries(${TOOL_PROJECT} PRIVATE assimp)

#--------------------------------------------------------------------
# Install files other than the application
#--------------------------------------------------------------------
if (LIGHTER_INSTALL)
    install (TARGETS ${TOOL_PROJECT} DESTINATION bin)
	
    add_custom_command(TARGET ${TOOL_PROJECT} POST_BUILD COMMAND ${LIGHTER_INSTALL_COMMAND})
endif()
