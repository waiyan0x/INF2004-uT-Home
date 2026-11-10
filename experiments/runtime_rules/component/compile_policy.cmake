# Apply source-specific policy after ESP-IDF has created the application target.
# This changes only our component; vendor source and options remain untouched.
function(runtime_rules_policy)
    if(TARGET __idf_src)
        set(rule_dir "${CMAKE_SOURCE_DIR}/src/esphome/components/runtime_rules")
        set_source_files_properties("${rule_dir}/txrule.c" "${rule_dir}/txwire.c"
            TARGET_DIRECTORY __idf_src
            PROPERTIES COMPILE_OPTIONS "-std=c99;-Wall;-Wextra;-Werror;-Werror=extra;-Werror=unused-function;-Werror=unused-variable;-Werror=unused-but-set-variable;-Werror=deprecated-declarations;-pedantic")
        set_source_files_properties("${rule_dir}/rulebridge.cpp"
            TARGET_DIRECTORY __idf_src
            PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror;-Werror=extra;-Werror=unused-function;-Werror=unused-variable;-Werror=unused-but-set-variable;-Werror=deprecated-declarations")
    endif()
endfunction()
cmake_language(DEFER CALL runtime_rules_policy)
