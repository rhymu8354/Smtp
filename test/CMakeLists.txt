# CMakeLists.txt for SmtpTests
#
# © 2019 by Richard Walters

cmake_minimum_required(VERSION 3.8)
set(This SmtpTests)

set(Sources
    src/ClientTests.cpp
    src/Common.cpp
    src/Common.hpp
    src/ExtensionTests.cpp
)

add_executable(${This} ${Sources})
set_target_properties(${This} PROPERTIES
    FOLDER Tests
)

target_include_directories(${This} PRIVATE ..)

target_link_libraries(${This} PUBLIC
    gtest_main
    MessageHeaders
    Smtp
    SystemAbstractions
    TlsDecorator
)

add_test(
    NAME ${This}
    COMMAND ${This}
)
