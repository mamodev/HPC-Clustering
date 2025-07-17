cmake_minimum_required(VERSION 3.10)
project(Kmeans CXX)

set(CMAKE_CXX_STANDARD 23)
# set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")


find_package(MPI REQUIRED)
find_package(OpenMP REQUIRED)

find_library(mpiP NAMES mpiP)
if(mpiP)
    message(STATUS "Found mpiP: ${mpiP}")
    get_filename_component(MPIP_DIR ${mpiP} DIRECTORY)
    get_filename_component(MPIP_DIR ${MPIP_DIR} DIRECTORY)
    message(STATUS "mpiP directory: ${MPIP_DIR}")
    include_directories(${MPIP_DIR}/include)
    link_directories(${MPIP_DIR}/lib)
else()
    message(STATUS "mpiP not found. Skipping mpiP-enabled builds.")
endif()


# Find Python and Pybind11
find_package(pybind11 REQUIRED)  # or `add_subdirectory(pybind11)`

set(LIB_DIR ${CMAKE_SOURCE_DIR}/lib)
file(GLOB MAIN_SOURCES RELATIVE ${CMAKE_SOURCE_DIR} src/*/main.cpp)
foreach(MAIN_SRC ${MAIN_SOURCES})
    get_filename_component(VERSION_DIR ${MAIN_SRC} DIRECTORY)
    get_filename_component(VERSION_NAME ${VERSION_DIR} NAME)

    set(EXECUTABLE_NAME "${VERSION_NAME}")
    add_executable(${EXECUTABLE_NAME} ${MAIN_SRC})
    target_link_libraries(${EXECUTABLE_NAME} MPI::MPI_CXX  OpenMP::OpenMP_CXX stdc++exp pybind11::embed)
    target_compile_options(${EXECUTABLE_NAME} PRIVATE -g -Ofast -funroll-loops -ffast-math -ffp-contract=fast -march=native)
    # target_compile_options(${EXECUTABLE_NAME} PRIVATE -g -O3 -fno-omit-frame-pointer  -funroll-loops -ffast-math -ffp-contract=fast -march=native)
    target_include_directories(${EXECUTABLE_NAME} PRIVATE ${LIB_DIR})
    target_compile_definitions(${EXECUTABLE_NAME} PRIVATE -DNDEBUG)

    set(EXECUTABLE_NAME "${VERSION_NAME}-mpip")
    add_executable(${EXECUTABLE_NAME} ${MAIN_SRC})
    target_link_libraries(${EXECUTABLE_NAME} MPI::MPI_CXX mpiP stdc++exp)
    target_compile_options(${EXECUTABLE_NAME} PRIVATE -O3  -funroll-loops -ffast-math -ffp-contract=fast -march=native)
    target_include_directories(${EXECUTABLE_NAME} PRIVATE ${LIB_DIR})
    target_compile_definitions(${EXECUTABLE_NAME} PRIVATE -DNDEBUG)

    set(EXECUTABLE_NAME "${VERSION_NAME}-perf")
    add_executable(${EXECUTABLE_NAME} ${MAIN_SRC})
    target_link_libraries(${EXECUTABLE_NAME} MPI::MPI_CXX stdc++exp)
    # disable inlining and optimizations for performance analysis
    target_compile_options(${EXECUTABLE_NAME} PRIVATE -O1 -g 
        -fno-omit-frame-pointer     
        -fno-inline -fno-inline-small-functions -fno-inline-functions -fno-inline-functions-called-once
        -ffast-math -ffp-contract=fast -march=native)
        
    target_include_directories(${EXECUTABLE_NAME} PRIVATE ${LIB_DIR})
    target_compile_definitions(${EXECUTABLE_NAME} PRIVATE -DNDEBUG)


    set(EXECUTABLE_NAME "${VERSION_NAME}-asserts")
    add_executable(${EXECUTABLE_NAME} ${MAIN_SRC}) 
    target_link_libraries(${EXECUTABLE_NAME} MPI::MPI_CXX stdc++exp)
    target_compile_options(${EXECUTABLE_NAME} PRIVATE -O2  -g  -ffast-math -ffp-contract=fast -march=native)
    target_include_directories(${EXECUTABLE_NAME} PRIVATE ${LIB_DIR})

endforeach()