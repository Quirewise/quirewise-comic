cmake_minimum_required(VERSION 3.21)
project(QuirewiseComic VERSION 0.1.0 LANGUAGES C)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
if(NOT TARGET Quirewise::Archive)
    find_package(QuirewiseArchive CONFIG REQUIRED)
endif()
if(NOT TARGET Quirewise::Image)
    find_package(QuirewiseImage CONFIG REQUIRED)
endif()
file(GLOB_RECURSE QW_SOURCES CONFIGURE_DEPENDS src/*.c src/*.cpp)
add_library(quirewise_comic STATIC ${QW_SOURCES})
add_library(Quirewise::Comic ALIAS quirewise_comic)
set_target_properties(quirewise_comic PROPERTIES EXPORT_NAME Comic POSITION_INDEPENDENT_CODE ON)
target_compile_features(quirewise_comic PUBLIC c_std_11)
target_include_directories(quirewise_comic PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/src/core ${CMAKE_CURRENT_SOURCE_DIR}/src/formats ${CMAKE_CURRENT_SOURCE_DIR}/src/backends)
target_link_libraries(quirewise_comic PRIVATE Quirewise::Archive Quirewise::Image)
install(TARGETS quirewise_comic EXPORT QuirewiseComicTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(EXPORT QuirewiseComicTargets FILE QuirewiseComicTargets.cmake NAMESPACE Quirewise:: DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/QuirewiseComic")
configure_package_config_file(cmake/QuirewiseComicConfig.cmake.in "${CMAKE_CURRENT_BINARY_DIR}/QuirewiseComicConfig.cmake" INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/QuirewiseComic")
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/QuirewiseComicConfigVersion.cmake" VERSION "${PROJECT_VERSION}" COMPATIBILITY SameMajorVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/QuirewiseComicConfig.cmake" "${CMAKE_CURRENT_BINARY_DIR}/QuirewiseComicConfigVersion.cmake" DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/QuirewiseComic")
