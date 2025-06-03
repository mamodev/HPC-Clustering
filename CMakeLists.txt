cmake_minimum_required(VERSION 3.10)
project(Kmeans CXX)

set(CMAKE_CXX_STANDARD 23)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3  -funroll-loops -ffast-math -ffp-contract=fast")
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -g -fsanitize=address")

find_package(MPI REQUIRED)
find_package(OpenMP REQUIRED)
# find_library(NUMA_LIB numa)
# if(NOT NUMA_LIB)
#     message(FATAL_ERROR "libnuma not found!")
# endif()

# set(MPIP_INSTALL_DIR "/usr/local") 
# include_directories(${MPIP_INSTALL_DIR}/include)
# link_directories(${MPIP_INSTALL_DIR}/lib)

find_library(mpiP mpiP)
if(NOT mpiP)
    message(FATAL_ERROR "mpiP not found! Please install mpiP.")
endif()

message(STATUS "Found mpiP: ${mpiP}")
get_filename_component(MPIP_DIR ${mpiP} DIRECTORY)
get_filename_component(MPIP_DIR ${MPIP_DIR} DIRECTORY)
message(STATUS "mpiP directory: ${MPIP_DIR}")

include_directories(${MPIP_DIR}/include)
link_directories(${MPIP_DIR}/lib)


file(GLOB MAIN_SOURCES RELATIVE ${CMAKE_SOURCE_DIR} src/*/main.cpp)
set(LIB_DIR ${CMAKE_SOURCE_DIR}/lib)

foreach(MAIN_SRC ${MAIN_SOURCES})
    get_filename_component(VERSION_DIR ${MAIN_SRC} DIRECTORY)
    get_filename_component(VERSION_NAME ${VERSION_DIR} NAME)

    set(EXECUTABLE_NAME "${VERSION_NAME}")
    add_executable(${EXECUTABLE_NAME} ${MAIN_SRC})
    target_link_libraries(${EXECUTABLE_NAME} MPI::MPI_CXX OpenMP::OpenMP_CXX)
    target_compile_options(${EXECUTABLE_NAME} PRIVATE -O3 -fopenmp -funroll-loops -ffast-math -ffp-contract=fast -march=native)
    target_include_directories(${EXECUTABLE_NAME} PRIVATE ${LIB_DIR})
    target_compile_definitions(${EXECUTABLE_NAME} PRIVATE -DNDEBUG)

    set(EXECUTABLE_NAME "${VERSION_NAME}-mpip")
    add_executable(${EXECUTABLE_NAME} ${MAIN_SRC})
    target_link_libraries(${EXECUTABLE_NAME} MPI::MPI_CXX OpenMP::OpenMP_CXX mpiP)
    target_compile_options(${EXECUTABLE_NAME} PRIVATE -O3 -fopenmp -funroll-loops -ffast-math -ffp-contract=fast -march=native)
    target_include_directories(${EXECUTABLE_NAME} PRIVATE ${LIB_DIR})
    target_compile_definitions(${EXECUTABLE_NAME} PRIVATE -DNDEBUG)

    set(EXECUTABLE_NAME "${VERSION_NAME}-perf")
    add_executable(${EXECUTABLE_NAME} ${MAIN_SRC})
    target_link_libraries(${EXECUTABLE_NAME} MPI::MPI_CXX OpenMP::OpenMP_CXX)
    target_compile_options(${EXECUTABLE_NAME} PRIVATE -O2 -fopenmp  -g -ffast-math -ffp-contract=fast -march=native)
    target_include_directories(${EXECUTABLE_NAME} PRIVATE ${LIB_DIR})
    target_compile_definitions(${EXECUTABLE_NAME} PRIVATE -DNDEBUG)


    set(EXECUTABLE_NAME "${VERSION_NAME}-asserts")
    add_executable(${EXECUTABLE_NAME} ${MAIN_SRC})
    target_link_libraries(${EXECUTABLE_NAME} MPI::MPI_CXX OpenMP::OpenMP_CXX)
    target_compile_options(${EXECUTABLE_NAME} PRIVATE -O2 -fopenmp -g  -ffast-math -ffp-contract=fast -march=native)
    target_include_directories(${EXECUTABLE_NAME} PRIVATE ${LIB_DIR})

endforeach()

# add_executable(mpi-v1 mpi-v1.cpp)
# target_link_libraries(mpi-v1 MPI::MPI_CXX numa)
# target_compile_options(mpi-v1 PRIVATE -O3 -funroll-loops -ffast-math -ffp-contract=fast)


# add_executable(serial-v1 serial-v1.cpp)
# target_link_libraries(serial-v1 numa)
# target_compile_options(serial-v1 PRIVATE -O1)

# add_executable(serial-v1-opt serial-v1.cpp)
# target_compile_options(serial-v1-opt PRIVATE -O3 -funroll-loops -ffast-math -ffp-contract=fast)

# add_executable(serial-v1-avx2 serial-v1.cpp)
# target_compile_options(serial-v1-avx2 PRIVATE -O3 -funroll-loops -ffast-math -ffp-contract=fast -march=native)
# # target_compile_options(serial-v1-avx2 PRIVATE -O2 -g -fno-inline -march=native -ffast-math -ffp-contract=fast)

# add_executable(serial-v1-avx2-aligned serial-v1.cpp)
# target_compile_definitions(serial-v1-avx2-aligned PRIVATE -DALIGN_32)
# target_compile_options(serial-v1-avx2-aligned PRIVATE -O3 -funroll-loops -ffast-math -ffp-contract=fast -march=native)

# add_executable(serial-v1-avx2-manual serial-v1.cpp)
# target_compile_definitions(serial-v1-avx2-manual PRIVATE -DALIGN_32 -DAVX2_MANUAL)
# target_compile_options(serial-v1-avx2-manual PRIVATE -O3 -funroll-loops -ffast-math -ffp-contract=fast -march=native)

# add_executable(coreset coreset.cpp)
# target_compile_options(coreset PRIVATE -g -O2 -fno-inline -ffast-math -ffp-contract=fast -march=native)
