add_executable(tests
	booleans.cpp
	branching.cpp
	classmanip.cpp
	common.cpp
	enums.cpp
	floatmath.cpp
	funcdefs.cpp
	functions.cpp
	globals.cpp
	integermath.cpp
	main.cpp
	recursion.cpp
	typedefs.cpp
)

target_link_libraries(tests PRIVATE angelscript-llvm angelscript-addons Catch2::Catch2)
target_include_directories(tests PRIVATE ../include ../3rd)

include(Catch)
catch_discover_tests(tests
	WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
