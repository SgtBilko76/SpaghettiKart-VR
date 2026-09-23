# The NDK ships neither libogg nor libvorbis, so build them from source.
include(FetchContent)

set(BUILD_SHARED_LIBS OFF)
set(BUILD_TESTING OFF)
set(INSTALL_DOCS OFF)

FetchContent_Declare(
  ogg
  GIT_REPOSITORY https://github.com/xiph/ogg.git
  GIT_TAG v1.3.5
  OVERRIDE_FIND_PACKAGE)
FetchContent_MakeAvailable(ogg)

FetchContent_Declare(
  vorbis
  GIT_REPOSITORY https://github.com/xiph/vorbis.git
  GIT_TAG v1.3.7
  OVERRIDE_FIND_PACKAGE)
FetchContent_MakeAvailable(vorbis)

# vorbis 1.3.7 doesn't export namespaced targets when used as a subproject
foreach(VORBIS_LIB vorbis vorbisenc vorbisfile)
  if(NOT TARGET Vorbis::${VORBIS_LIB})
    add_library(Vorbis::${VORBIS_LIB} ALIAS ${VORBIS_LIB})
  endif()
endforeach()

if(ENABLE_OPENXR)
  # The stereo rendering hooks live in a patch on the libultraship submodule; apply it if a fresh
  # checkout doesn't have it yet
  set(LUS_VR_PATCH ${CMAKE_SOURCE_DIR}/android/patches/libultraship-vr-stereo.patch)
  execute_process(
    COMMAND git apply --check --reverse ${LUS_VR_PATCH}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/libultraship
    RESULT_VARIABLE LUS_VR_PATCH_APPLIED
    OUTPUT_QUIET ERROR_QUIET)
  if(NOT LUS_VR_PATCH_APPLIED EQUAL 0)
    execute_process(
      COMMAND git apply ${LUS_VR_PATCH}
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/libultraship
      RESULT_VARIABLE LUS_VR_PATCH_RESULT)
    if(NOT LUS_VR_PATCH_RESULT EQUAL 0)
      message(FATAL_ERROR "Could not apply ${LUS_VR_PATCH} to libultraship")
    endif()
  endif()

  # Provided by the org.khronos.openxr:openxr_loader_for_android prefab package in Gradle
  find_package(OpenXR REQUIRED CONFIG)
  find_library(EGL_LIBRARY EGL REQUIRED)
  target_link_libraries(${PROJECT_NAME} PRIVATE OpenXR::openxr_loader ${EGL_LIBRARY})
  target_compile_definitions(${PROJECT_NAME} PRIVATE SPAGHETTIKART_OPENXR)
endif()
