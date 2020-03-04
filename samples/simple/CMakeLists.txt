add_executable(simple main.cpp)

target_link_libraries(simple PRIVATE angelscript-llvm angelscript-addons)
target_include_directories(simple PRIVATE ../../include ../../3rd)
