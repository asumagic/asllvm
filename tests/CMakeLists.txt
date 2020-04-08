add_executable(tests
	main.cpp
	common.cpp
	integermath.cpp
)

target_link_libraries(tests PRIVATE angelscript-llvm angelscript-addons Catch2::Catch2)
target_include_directories(tests PRIVATE ../include ../3rd)

include(Catch)
catch_discover_tests(tests
	WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
