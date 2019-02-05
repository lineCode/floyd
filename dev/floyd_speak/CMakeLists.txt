# Generated on 2018-9-21 20:42:18 by version CURRENT_PROJECT_VERSION of mulle-xcode-to-cmake
# Command line:
#    mulle-xcode-to-cmake -b export floyd_speak.xcodeproj

# For emscripten, fix CMAKE_CXX_FLAGS, then try
# cmake -DCMAKE_TOOLCHAIN_FILE=/usr/lib/emscripten/cmake/Modules/Platform/Emscripten.cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -G  "Unix Makefiles" ..

project( floyd_speak)

cmake_minimum_required (VERSION 3.4)

#SET (CMAKE_BUILD_TYPE Release)
#SET (CMAKE_BUILD_TYPE Debug)

include_directories(parts floyd_ast bytecode_interpreter parts/immer-master floyd_parser parts/sha1 .)

# for newer cmake 
#add_compile_definitions(DEBUG)
add_definitions(-DDEBUG)

set(CMAKE_CXX_FLAGS "-std=c++17 -g")

#Options for emscripten
#set(CMAKE_CXX_FLAGS "--closure 1 -std=c++1z -s USE_PTHREADS=1 -Os -s WASM=0 -s ASSERTIONS=1 -s DISABLE_EXCEPTION_CATCHING=0 --bind")


##
## floyd_speak Files
##

# uncomment this for mulle-objc to search libraries first
# set( CMAKE_FIND_FRAMEWORK "LAST")

set( FLOYD_SPEAK_SOURCES
ast_typeid.cpp
ast_typeid_helpers.cpp
ast_value.cpp
benchmark_basics.cpp
#benchmark_game_of_life.cpp
bytecode_interpreter/bytecode_generator.cpp
bytecode_interpreter/bytecode_interpreter.cpp
bytecode_interpreter/floyd_interpreter.cpp
bytecode_interpreter/host_functions.cpp
cpp_experiments.cpp
floyd_ast/ast.cpp
floyd_ast/ast_basics.cpp
floyd_ast/expression.cpp
floyd_ast/statement.cpp
floyd_basics.cpp
floyd_main.cpp
floyd_parser/floyd_parser.cpp
floyd_parser/parse_expression.cpp
floyd_parser/parse_function_def.cpp
floyd_parser/parse_prefixless_statement.cpp
floyd_parser/parse_statement.cpp
floyd_parser/parse_struct_def.cpp
floyd_parser/parser2.cpp
floyd_parser/parser_primitives.cpp
floyd_parser/parse_protocol_def.cpp
#floyd_speak/example.floydsys
hardware_caps.cpp
interpretator_benchmark.cpp
parts/immutable_ref_value.cpp
parts/json_parser.cpp
parts/json_support.cpp
parts/json_writer.cpp
parts/quark.cpp
parts/sha1/sha1.cpp
parts/sha1_class.cpp
parts/text_parser.cpp
parts/utils.cpp
parts/FileHandling.cpp
pass3.cpp
software_system.cpp
)

set( FLOYD_SPEAK_STATIC_DEPENDENCIES
)


set( FLOYD_SPEAK_DEPENDENCIES
)


##
## floyd_speak UT Files
##

set( FLOYD_SPEAK__UT_SOURCES
ast_typeid.cpp
ast_typeid_helpers.cpp
ast_value.cpp
benchmark_basics.cpp
#benchmark_game_of_life.cpp
bytecode_interpreter/bytecode_generator.cpp
bytecode_interpreter/bytecode_interpreter.cpp
bytecode_interpreter/floyd_interpreter.cpp
bytecode_interpreter/host_functions.cpp
cpp_experiments.cpp
floyd_ast/ast.cpp
floyd_ast/ast_basics.cpp
floyd_ast/expression.cpp
floyd_ast/statement.cpp
floyd_basics.cpp
floyd_main.cpp
floyd_parser/floyd_parser.cpp
floyd_parser/parse_expression.cpp
floyd_parser/parse_function_def.cpp
floyd_parser/parse_prefixless_statement.cpp
floyd_parser/parse_statement.cpp
floyd_parser/parse_struct_def.cpp
floyd_parser/parser2.cpp
floyd_parser/parser_primitives.cpp
floyd_parser/parse_protocol_def.cpp
floyd_test_suite.cpp
hardware_caps.cpp
interpretator_benchmark.cpp
parts/immutable_ref_value.cpp
parts/json_parser.cpp
parts/json_support.cpp
parts/json_writer.cpp
parts/quark.cpp
parts/sha1/sha1.cpp
parts/sha1_class.cpp
parts/text_parser.cpp
parts/utils.cpp
parts/FileHandling.cpp
pass3.cpp
software_system.cpp
)

set( FLOYD_SPEAK__UT_STATIC_DEPENDENCIES
)


set( FLOYD_SPEAK__UT_DEPENDENCIES
)


##
## floyd_speak
##

add_executable( floyd MACOSX_BUNDLE
${FLOYD_SPEAK_SOURCES}
${FLOYD_SPEAK_PUBLIC_HEADERS}
${FLOYD_SPEAK_PROJECT_HEADERS}
${FLOYD_SPEAK_PRIVATE_HEADERS}
${FLOYD_SPEAK_RESOURCES}
)

target_include_directories( floyd
   PUBLIC
)



target_link_libraries( floyd
${FLOYD_SPEAK_STATIC_DEPENDENCIES}
${FLOYD_SPEAK_DEPENDENCIES} pthread
)


##
## floyd_speak UT
##


add_executable( floyd-UT MACOSX_BUNDLE
${FLOYD_SPEAK__UT_SOURCES}
${FLOYD_SPEAK__UT_PUBLIC_HEADERS}
${FLOYD_SPEAK__UT_PROJECT_HEADERS}
${FLOYD_SPEAK__UT_PRIVATE_HEADERS}
${FLOYD_SPEAK__UT_RESOURCES}
)

target_compile_definitions(floyd-UT PRIVATE DEBUG=1)

target_include_directories( floyd-UT
   PUBLIC
)

target_link_libraries( floyd-UT
${FLOYD_SPEAK__UT_STATIC_DEPENDENCIES}
${FLOYD_SPEAK__UT_DEPENDENCIES} pthread
)