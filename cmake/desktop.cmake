# Silicon LogiX / SLX SeaBalance
# Copyright (c) 2026 Marco Pezzullo (Silicon LogiX).
# Author: Marco Pezzullo
# License: Silicon LogiX Evaluation License 1.0. See LICENSE.

if(WIN32)
  set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
  set(USE_MSVC_RUNTIME_LIBRARY_DLL OFF CACHE BOOL "" FORCE)
  add_subdirectory(third_party/glfw EXCLUDE_FROM_ALL)
else()
  find_package(glfw3 3.3 REQUIRED)
endif()
find_package(OpenGL REQUIRED)
set(imgui third_party/imgui)
set(implot third_party/implot)
add_library(slx-ui STATIC
  ${imgui}/imgui.cpp ${imgui}/imgui_draw.cpp ${imgui}/imgui_tables.cpp ${imgui}/imgui_widgets.cpp
  ${imgui}/backends/imgui_impl_glfw.cpp ${imgui}/backends/imgui_impl_opengl3.cpp
  ${implot}/implot.cpp ${implot}/implot_items.cpp)
target_include_directories(slx-ui SYSTEM PUBLIC
  ${imgui} ${imgui}/backends ${implot} third_party/include third_party/cgltf third_party/stb)
target_link_libraries(slx-ui PUBLIC glfw OpenGL::GL)
add_executable(SLX-SeaBalance WIN32 desktop/app.cpp desktop/renderer.cpp)
target_link_libraries(SLX-SeaBalance PRIVATE slx-session slx-ui)
if(WIN32)
  enable_language(RC)
  set(SLX_PC_ICON "${CMAKE_CURRENT_SOURCE_DIR}/assets/branding/seabalance.ico")
  configure_file(desktop/windows.rc.in "${CMAKE_CURRENT_BINARY_DIR}/seabalance.rc" @ONLY)
  target_sources(SLX-SeaBalance PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/seabalance.rc")
  target_link_libraries(SLX-SeaBalance PRIVATE shell32)
endif()
slx_warnings(SLX-SeaBalance)
add_custom_target(seabalance-assets ALL
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/assets"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/docs" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/docs"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/licenses" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/licenses"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/LICENSE"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/THIRD_PARTY_NOTICES.md"
  VERBATIM)
add_dependencies(SLX-SeaBalance seabalance-assets)
install(TARGETS SLX-SeaBalance RUNTIME DESTINATION bin)
