cmake_minimum_required(VERSION 3.8)
project(homegear_velux_klf200)

set(CMAKE_CXX_STANDARD 14)

set(SOURCE_FILES
        src/PhysicalInterfaces/Klf200.cpp
        src/PhysicalInterfaces/Klf200.h
        src/Factory.cpp
        src/Factory.h
        src/GD.cpp
        src/GD.h
        src/Interfaces.cpp
        src/Interfaces.h
        src/Velux.cpp
        src/Velux.h
        src/VeluxCentral.cpp
        src/VeluxCentral.h
        src/VeluxPacket.cpp
        src/VeluxPacket.h
        src/VeluxPeer.cpp
        src/VeluxPeer.h)

add_custom_target(homegear COMMAND ../../makeAll.sh SOURCES ${SOURCE_FILES})

add_library(homegear_velux_klf200 ${SOURCE_FILES})
