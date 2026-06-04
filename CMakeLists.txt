cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(katzenbrunnen)

# Test-Komponente registrieren
set(EXTRA_COMPONENT_DIRS "test")
