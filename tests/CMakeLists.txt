add_executable(animation_blending_test animation_blending_test.c)
target_link_libraries(animation_blending_test PRIVATE raylib)

add_test(NAME animation_blending COMMAND animation_blending_test)

option(RAYLIB_RENDER_TESTS "Build animation tests that require a graphics context" OFF)

if (RAYLIB_RENDER_TESTS)
    add_executable(animation_render_check animation_render_check.c)
    target_link_libraries(animation_render_check PRIVATE raylib)
    target_compile_definitions(animation_render_check PRIVATE
        ANIMATION_TEST_RESOURCE_DIR="${PROJECT_SOURCE_DIR}/examples/models/resources")

    add_test(NAME animation_render_check COMMAND animation_render_check)
    set_tests_properties(animation_render_check PROPERTIES SKIP_RETURN_CODE 77)
endif()
