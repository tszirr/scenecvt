project(scenecvt)

include(add/make/everything.cmake OPTIONAL)

#--------------------------------------------------------------------
# Data
#--------------------------------------------------------------------

#--------------------------------------------------------------------
# Create target
#--------------------------------------------------------------------
set(TOOL_SRC
  main.cpp
  scene.cpp
  pch.cpp
  pch.h
  )
source_group(
  "Source Files" FILES ${TOOL_SRC}
  )

add_executable(${PROJECT_NAME} ${TOOL_SRC})
target_precompiled_header(${PROJECT_NAME} pch.h pch.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE lighter assimp)

#--------------------------------------------------------------------
# Install files other than the application
#--------------------------------------------------------------------
if (LIGHTER_INSTALL)
    install (TARGETS ${PROJECT_NAME} DESTINATION bin)
	
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD COMMAND ${LIGHTER_INSTALL_COMMAND})
endif()
