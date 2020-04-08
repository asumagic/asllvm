set(TESTS
    "test-integer-arithmetic"
)

foreach(name ${TESTS})
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/${name})
    add_test(
        NAME ctest-${name}
        COMMAND
            ${CMAKE_COMMAND}
            -E env LD_LIBRARY_PATH=${ANGELSCRIPT_SDK_DIRECTORY}/angelscript/lib:$ENV{LD_LIBRARY_PATH}
            "${CMAKE_CURRENT_BINARY_DIR}/${name}/${name}"
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${name}/workdir/
    )
endforeach()

# Force tests to occur after compilation
add_custom_target(run_unit_test ALL
	COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
	DEPENDS ${TESTS} ${PROJECT_NAME}
)
