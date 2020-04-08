function(test_target_common name)
    target_link_libraries(${name} PRIVATE angelscript-llvm angelscript-addons Catch2::Catch2)
    target_include_directories(${name} PRIVATE ../../include ../../3rd ../common/include/)
endfunction()
