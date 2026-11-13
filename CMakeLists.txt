cmake_minimum_required(VERSION 3.20)
project(Axiom VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler flags
if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8 /Z7)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Core library sources
set(AXIOM_SOURCES
    src/types/value.cpp
    src/common/record_batch.cpp
    src/common/query_engine.cpp
    src/storage/csv_reader.cpp
    src/parser/lexer.cpp
    src/parser/ast.cpp
    src/parser/parser.cpp
    src/binder/bound_expression.cpp
    src/binder/binder.cpp
    src/planner/logical_plan.cpp
    src/planner/logical_planner.cpp
    src/execution/expression_evaluator.cpp
    src/execution/physical_hash_join.cpp
    src/execution/physical_hash_aggregate.cpp
    src/execution/physical_sort.cpp
    src/execution/physical_planner.cpp
)

add_library(axiom_core STATIC ${AXIOM_SOURCES})
target_include_directories(axiom_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# CLI executable
add_executable(axiom src/cli/main.cpp)
target_link_libraries(axiom PRIVATE axiom_core)

# Tests
enable_testing()

add_executable(axiom_unit_tests
    tests/unit/test_main.cpp
    tests/unit/test_types.cpp
    tests/unit/test_lexer.cpp
    tests/unit/test_parser.cpp
    tests/unit/test_csv_reader.cpp
    tests/unit/test_binder.cpp
    tests/unit/test_operators.cpp
)
target_include_directories(axiom_unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(axiom_unit_tests PRIVATE axiom_core)
add_test(NAME unit_tests COMMAND axiom_unit_tests)

add_executable(axiom_integration_tests
    tests/integration/test_end_to_end.cpp
)
target_include_directories(axiom_integration_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(axiom_integration_tests PRIVATE axiom_core)
add_test(NAME integration_tests COMMAND axiom_integration_tests)
