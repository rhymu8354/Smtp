# CMakeLists.txt for Smtp
#
# © 2019 by Richard Walters

cmake_minimum_required(VERSION 3.8)
set(This Smtp)

set(Headers
    include/Smtp/Client.hpp
)

set(Sources
    src/Client.cpp
)

add_library(${This} STATIC ${Sources} ${Headers})
set_target_properties(${This} PROPERTIES
    FOLDER Libraries
)

target_include_directories(${This} PUBLIC include)

target_link_libraries(${This} PUBLIC
    MessageHeaders
    StringExtensions
    SystemAbstractions
)

add_subdirectory(test)
