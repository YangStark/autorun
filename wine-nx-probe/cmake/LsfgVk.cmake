set(WINE_NX_LSFG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/lsfg-vk")
if(NOT EXISTS "${WINE_NX_LSFG_DIR}/lsfg-vk-backend/src/lsfgvk.cpp")
    message(FATAL_ERROR "Run wine-nx-probe/tools/bootstrap-lsfg-vk.sh before enabling LSFG-VK")
endif()
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/lsfg/revision.txt" LSFG_REVISION LIMIT_COUNT 1)
find_package(Git REQUIRED)
execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${WINE_NX_LSFG_DIR}" rev-parse HEAD
    OUTPUT_VARIABLE LSFG_CHECKOUT OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE LSFG_GIT_RESULT)
if(LSFG_GIT_RESULT OR NOT LSFG_CHECKOUT STREQUAL LSFG_REVISION)
    message(FATAL_ERROR "LSFG-VK must use the pinned GPL archive revision ${LSFG_REVISION}")
endif()
execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${WINE_NX_LSFG_DIR}" apply --reverse --check
    "${CMAKE_CURRENT_SOURCE_DIR}/lsfg/horizon.patch"
    RESULT_VARIABLE LSFG_PATCH_RESULT ERROR_QUIET)
if(LSFG_PATCH_RESULT)
    message(FATAL_ERROR "Run wine-nx-probe/tools/bootstrap-lsfg-vk.sh to apply the Horizon patch")
endif()

find_program(LSFG_GLSLANG_VALIDATOR glslangValidator HINTS /ucrt64/bin REQUIRED)
set(LSFG_GENERATED "${CMAKE_CURRENT_BINARY_DIR}/lsfg")
file(MAKE_DIRECTORY "${LSFG_GENERATED}")
set(LSFG_SHADER_NAMES nvk_mipmap_luma nvk_mipmap_downsample
    nvk_beta1_finalize_quality nvk_beta1_finalize_performance)
set(LSFG_SHADER_SYMBOLS kNvkMipmapLumaSpv kNvkMipmapDownsampleSpv
    kNvkBeta1FinalizeQualitySpv kNvkBeta1FinalizePerformanceSpv)
foreach(index RANGE 0 3)
    list(GET LSFG_SHADER_NAMES ${index} name)
    list(GET LSFG_SHADER_SYMBOLS ${index} symbol)
    set(source "${CMAKE_CURRENT_SOURCE_DIR}/lsfg/shaders/${name}.comp")
    set(header "${LSFG_GENERATED}/${name}.h")
    add_custom_command(OUTPUT "${header}"
        COMMAND "${LSFG_GLSLANG_VALIDATOR}" -V --target-env vulkan1.1
            --vn "${symbol}" -o "${header}" "${source}"
        DEPENDS "${source}" VERBATIM)
    list(APPEND LSFG_HEADERS "${header}")
endforeach()
file(GLOB LSFG_COMMON CONFIGURE_DEPENDS "${WINE_NX_LSFG_DIR}/lsfg-vk-common/src/vulkan/*.cpp")
file(GLOB_RECURSE LSFG_BACKEND CONFIGURE_DEPENDS "${WINE_NX_LSFG_DIR}/lsfg-vk-backend/src/*.cpp")
add_library(wine-nx-lsfg STATIC ${LSFG_COMMON} ${LSFG_BACKEND} ${LSFG_HEADERS}
    "${WINE_NX_LSFG_DIR}/lsfg-vk-common/src/helpers/errors.cpp" source/lsfg.cpp)
set_target_properties(wine-nx-lsfg PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_include_directories(wine-nx-lsfg PRIVATE
    "${WINE_NX_MESA_SWITCH_DIR}/../include"
    "${WINE_NX_LSFG_DIR}/lsfg-vk-common/include"
    "${WINE_NX_LSFG_DIR}/lsfg-vk-backend/include"
    "${WINE_NX_LSFG_DIR}/lsfg-vk-backend/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/lsfg" "${LSFG_GENERATED}")
target_compile_options(wine-nx-lsfg PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
target_compile_definitions(wine-nx-runtime PRIVATE WINE_NX_LSFG)
target_compile_definitions(wine-win32u-real PRIVATE WINE_NX_LSFG)
target_link_libraries(wine-nx-runtime PRIVATE wine-nx-lsfg)
