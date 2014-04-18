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

#--------------------------------------------------------------------
# Install files other than the application
#--------------------------------------------------------------------
if (LIGHTER_INSTALL)
    install (TARGETS ${TOOL_PROJECT} DESTINATION bin)
	
    add_custom_command(TARGET ${TOOL_PROJECT} POST_BUILD COMMAND ${LIGHTER_INSTALL_COMMAND})
endif()
