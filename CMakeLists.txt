cmake_minimum_required(VERSION 3.18)
project(fermi C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(LLVM_INC "/nix/store/kw70jh9qpn3akb01g8myak1d0g30z9sd-llvm-20.1.6-dev/include")
set(LLVM_LIB "/nix/store/136cbgnv6rrk8ynp1d8x9977plz14cf9-llvm-20.1.6-lib/lib")

find_program(LLVM_CONFIG llvm-config
    HINTS "${LLVM_INC}/../bin"
    REQUIRED)

execute_process(COMMAND ${LLVM_CONFIG} --includedir OUTPUT_VARIABLE _LLVM_INC OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND ${LLVM_CONFIG} --libdir     OUTPUT_VARIABLE _LLVM_LIB OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND ${LLVM_CONFIG} --libs       OUTPUT_VARIABLE _LLVM_LIBS OUTPUT_STRIP_TRAILING_WHITESPACE)

if(_LLVM_INC)
    set(LLVM_INC ${_LLVM_INC})
endif()
if(_LLVM_LIB)
    set(LLVM_LIB ${_LLVM_LIB})
endif()

message(STATUS "LLVM include: ${LLVM_INC}")
message(STATUS "LLVM libdir:  ${LLVM_LIB}")

set(FERMI_SRCS
    src/main.c
    src/fearena/arena.c
    src/felexer/lexer.c
    src/felexer/token.c
    src/feparser/parser.c
    src/fehir/hir.c
    src/fetc/tc.c
    src/fesema/sema.c
    src/fecodegen/fir.c
    src/fecodegen/codegen.c
    src/feopt/opt.c
    src/fellvm/llvm_emit.c
)

add_executable(fermi ${FERMI_SRCS})

target_include_directories(fermi PRIVATE
    src
    ${LLVM_INC}
)

target_compile_options(fermi PRIVATE
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter
    -Wno-unused-function
    -Wno-sign-compare
)

target_link_directories(fermi PRIVATE ${LLVM_LIB})
target_link_libraries(fermi PRIVATE LLVM-20 m)

add_library(fermi_rt STATIC rt/fermi_rt.c)
target_include_directories(fermi_rt PRIVATE rt)
target_compile_options(fermi_rt PRIVATE -Wall -Wextra -Wno-unused-parameter)

set_target_properties(fermi_rt PROPERTIES OUTPUT_NAME "fermi_rt")

install(TARGETS fermi DESTINATION bin)
install(TARGETS fermi_rt DESTINATION lib)
install(FILES rt/fermi_rt.h DESTINATION include)
