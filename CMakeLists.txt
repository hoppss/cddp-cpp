#  Copyright 2024 Tomo Sasaki

#  Licensed under the Apache License, Version 2.0 (the "License");
#  you may not use this file except in compliance with the License.
#  You may obtain a copy of the License at

#       https://www.apache.org/licenses/LICENSE-2.0

#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.

cmake_minimum_required(VERSION 3.14) 
# Set policy to support older CMake code
cmake_policy(SET CMP0048 NEW)
# Allow policy version minimum flag to be passed to sub-projects
set(CMAKE_POLICY_VERSION_MINIMUM "3.5" CACHE STRING "Minimum CMake version for policy compatibility")

project(
  cddp
  VERSION 0.1.0
  DESCRIPTION "CDDP: A C++ library for Trajectory Optimization and MPC"
  HOMEPAGE_URL  "https://github.com/astomodynamics/cddp-cpp"
)

set(CMAKE_CXX_STANDARD 17) # Enforce C++17 as the minimum standard
set(CMAKE_CXX_STANDARD_REQUIRED ON)  # Enforce C++17 as the minimum standard
set(ABSL_PROPAGATE_CXX_STD ON) # Enforce C++17 for absl
set(CMAKE_BUILD_TYPE "Release") # Set the build type to Release by default

# Platform-specific settings
if(APPLE)
  # Add macOS-specific compiler and linker flags
  set(CMAKE_MACOSX_RPATH ON)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")
  # Use the new linker on macOS 11+
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND NOT CMAKE_CXX_COMPILER_VERSION VERSION_LESS 12.0)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-ld_classic")
  endif()
endif()

# Options
option(CDDP_CPP_BUILD_TESTS "Whether to build tests." ON)

# SQP Configuration
option(CDDP_CPP_SQP "Whether to use SQP solver" OFF)

# CasADi Configuration
option(CDDP_CPP_CASADI "Whether to use CasADi" OFF)

# Acado Configuration 
option(CDDP_CPP_ACADO "Whether to use Acado" OFF)

# Gurobi Configuration
# If you want to install Gurobi, please follow the instructions at:
# https://support.gurobi.com/hc/en-us/articles/4534161999889-How-do-I-install-Gurobi-Optimizer 
option(CDDP_CPP_GUROBI "Whether to use Gurobi solver." OFF)
option(GUROBI_ROOT "Path to Gurobi installation" "") 
if(APPLE)
  set(GUROBI_ROOT /Library/gurobi1201/macos_universal2)  # macOS default path
else()
  set(GUROBI_ROOT /usr/local/lib/gurobi1201/linux64)  # Linux default path
endif()

# SNOPT Configuration
# If you want to install SNOPT, please follow the instructions at:
# https://ccom.ucsd.edu/~optimizers/
option(CDDP_CPP_SNOPT "Whether to use SNOPT solver." OFF)
option(SNOPT_ROOT "Path to SNOPT installation" "")
if(APPLE)
  set(SNOPT_ROOT /usr/local/lib/snopt)  # macOS default path
else()
  set(SNOPT_ROOT /home/astomodynamics/.local/lib/snopt)  # Linux default path
endif()

# ACADOS Configuration
# For ACADOS installation, see: https://docs.acados.org/
option(CDDP_CPP_ACADOS "Whether to use ACADOS solver." OFF)
option(ACADOS_ROOT "Path to ACADOS installation" "")
if(APPLE)
  set(ACADOS_ROOT /usr/local/lib/acados)  # macOS default path
else()
  set(ACADOS_ROOT /home/astomodynamics/acados)  # Linux default path
endif()

# LibTorch Configuration
# If you want to install LibTorch, please follow the instructions at:
# https://pytorch.org/
# $ sudo cp -r libtorch /usr/local/lib/
option(CDDP_CPP_TORCH "Whether to use LibTorch" OFF)
option(CDDP_CPP_TORCH_GPU "Whether to use GPU support in LibTorch" OFF)
set(LIBTORCH_DIR /usr/local/lib/libtorch CACHE PATH "Path to local LibTorch installation") # FIXME: Change this to your local LibTorch installation directory

# Find packages
find_package(Eigen3 QUIET) 

# If Eigen3 is not found, download it
if(NOT Eigen3_FOUND)
  message(STATUS "Eigen3 not found. Downloading...")
  FetchContent_Declare(
    eigen
    GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git
    GIT_TAG 3.4.0  # Use a stable version
  )
  FetchContent_MakeAvailable(eigen)
  set(EIGEN3_INCLUDE_DIRS ${eigen_SOURCE_DIR})
endif()

if (CDDP_CPP_CASADI)
  # Assuming that CasADi is installed in /usr/local/include/casadi by:
  # https://github.com/casadi/casadi/wiki/InstallationLinux 
  # $ echo "export LD_LIBRARY_PATH=/home/<path_to_casadi>/casadi/build/lib:$LD_LIBRARY_PATH" >> ~/.bashrc && source ~/.bashrc
  # If you use Ipopt or SNOPT, you need to set flags for CasADi installation.

  find_package(casadi REQUIRED)
  set(CASADI_INCLUDE_DIR /usr/local/include/casadi)
  find_library(CASADI_LIBRARY NAMES casadi HINTS ${CASADI_INCLUDE_DIR}/../lib $ENV{CASADI_PREFIX}/lib)
  set(CASADI_LIBRARIES ${CASADI_LIBRARIES} ${CASADI_LIBRARY})
  if (NOT CASADI_LIBRARIES)
    message(FATAL_ERROR "CasADi library not found. Please set CASADI_PREFIX.")
  else()
    message(STATUS "Found CasADi: ${CASADI_LIBRARIES}")
  endif()
endif()

if (CDDP_CPP_ACADO)
  # Assuming that Acado is installed by: https://acado.github.io/install_linux.html
  set(ACADO_DIR $ENV{HOME}/github/ACADOtoolkit) # FIXME: Change this to your local Acado installation directory
  
  # Include ACADO header files
  include_directories(${ACADO_DIR})
  include_directories(${ACADO_DIR}/acado)
endif()

# Enable FetchContent for downloading dependencies
include(FetchContent)

# Matplotplusplus
FetchContent_Declare(matplotplusplus
  GIT_REPOSITORY https://github.com/alandefreitas/matplotplusplus
  GIT_TAG origin/master) # or whatever tag you want
FetchContent_GetProperties(matplotplusplus)
if(NOT matplotplusplus_POPULATED)
    FetchContent_Populate(matplotplusplus)
    add_subdirectory(${matplotplusplus_SOURCE_DIR} ${matplotplusplus_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()

# autodiff
set(AUTODIFF_BUILD_TESTS OFF CACHE BOOL "Don't build autodiff tests")
set(AUTODIFF_BUILD_EXAMPLES OFF CACHE BOOL "Don't build autodiff examples")
set(AUTODIFF_BUILD_PYTHON OFF CACHE BOOL "Don't build autodiff Python bindings")
FetchContent_Declare(
  autodiff
  GIT_REPOSITORY https://github.com/autodiff/autodiff.git
  GIT_TAG v1.1.2  # Use a stable version tag instead of main
)
FetchContent_MakeAvailable(autodiff)

# Googletest
if (CDDP_CPP_BUILD_TESTS)
  enable_testing()
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        origin/main
  )
  FetchContent_MakeAvailable(googletest)
  include(GoogleTest)
endif()

# LibTorch
if (CDDP_CPP_TORCH)
  # Function to download and extract LibTorch
  function(download_libtorch cuda_support download_dir)
    if(APPLE)
      # macOS has no CUDA support in PyTorch's pre-built binaries
      set(LIBTORCH_URL "https://download.pytorch.org/libtorch/cpu/libtorch-macos-2.5.1.zip")
    else()
      if(cuda_support) 
        set(LIBTORCH_URL "https://download.pytorch.org/libtorch/cu124/libtorch-cxx11-abi-shared-with-deps-2.5.1%2Bcu124.zip")
      else()
        set(LIBTORCH_URL "https://download.pytorch.org/libtorch/cpu/libtorch-cxx11-abi-shared-with-deps-2.5.1%2Bcpu.zip")
      endif()
    endif()

    set(DOWNLOAD_PATH "${download_dir}/libtorch-shared-with-deps-latest.zip")

    message(STATUS "Downloading LibTorch from ${LIBTORCH_URL}")
    file(DOWNLOAD "${LIBTORCH_URL}" "${DOWNLOAD_PATH}"
        SHOW_PROGRESS
        STATUS DOWNLOAD_STATUS
    )

    list(GET DOWNLOAD_STATUS 0 STATUS_CODE)
    list(GET DOWNLOAD_STATUS 1 ERROR_MESSAGE)

    if(NOT STATUS_CODE EQUAL 0)
        message(FATAL_ERROR "Failed to download LibTorch: ${ERROR_MESSAGE}")
    endif()

    message(STATUS "Extracting LibTorch...")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E tar xf "${DOWNLOAD_PATH}"
        WORKING_DIRECTORY "${download_dir}"
        RESULT_VARIABLE EXTRACT_RESULT
    )

    if(NOT EXTRACT_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to extract LibTorch")
    endif()

    file(REMOVE "${DOWNLOAD_PATH}")
  endfunction()

  # Try to find LibTorch in the following priority order:
  # 0. Default locations
  # 1. Local LibTorch directory (if specified)
  # 2. Previously installed LibTorch under build directory
  # 3. Download and install new copy
  find_package(Torch QUIET) # Try finding in default locations first
  
  # Priority 0: Check default locations
  if (TORCH_FOUND)
    message(STATUS "Found LibTorch in default locations")
    set(TORCH_FOUND TRUE)
  endif()

  # Priority 1: Check local LibTorch directory
  if(NOT TORCH_FOUND AND LIBTORCH_DIR)
      if(EXISTS "${LIBTORCH_DIR}/share/cmake/Torch/TorchConfig.cmake")
          find_package(Torch REQUIRED PATHS "${LIBTORCH_DIR}" NO_DEFAULT_PATH)
          set(TORCH_FOUND TRUE)
          message(STATUS "Found LibTorch in local directory: ${LIBTORCH_DIR}")
      else()
          message(WARNING "Specified LIBTORCH_DIR does not contain a valid LibTorch installation")
      endif()
  endif()
  
  # Priority 2: Check previously installed LibTorch under build directory
  if(NOT TORCH_FOUND)
      set(BUILD_LIBTORCH_DIR "${CMAKE_BINARY_DIR}/libtorch")
      if(EXISTS "${BUILD_LIBTORCH_DIR}/share/cmake/Torch/TorchConfig.cmake")
          find_package(Torch REQUIRED PATHS "${BUILD_LIBTORCH_DIR}" NO_DEFAULT_PATH)
          set(TORCH_FOUND TRUE)
          message(STATUS "Found LibTorch in build directory: ${BUILD_LIBTORCH_DIR}")
      endif()
  endif()
  
  # Priority 3: Download and install new copy
  if(NOT TORCH_FOUND)
      message(STATUS "LibTorch not found in preferred locations. Downloading fresh copy...")
      download_libtorch(${CDDP_CPP_TORCH_GPU} "${CMAKE_BINARY_DIR}")
      find_package(Torch REQUIRED PATHS "${CMAKE_BINARY_DIR}/libtorch" NO_DEFAULT_PATH)
      message(STATUS "Successfully downloaded and installed LibTorch to: ${CMAKE_BINARY_DIR}/libtorch")
  endif()

  # Set compilation flags for CUDA if GPU support is enabled
  if(CDDP_CPP_TORCH_GPU)
      set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${TORCH_CXX_FLAGS}")
  endif()
 
  # Export LibTorch variables for other parts of the build
  set(TORCH_INSTALL_PREFIX ${Torch_DIR}/../../../ CACHE PATH "LibTorch installation directory")
endif()

# OSQP-CPP for ASDDP
FetchContent_Declare(
  osqp-cpp
  GIT_REPOSITORY https://github.com/astomodynamics/osqp-cpp.git
  GIT_TAG master
  CMAKE_ARGS 
    -DCMAKE_POLICY_DEFAULT_CMP0048=NEW 
    -DCMAKE_POLICY_DEFAULT_CMP0069=NEW
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5
)

# Custom processing before making osqp-cpp available
if(NOT osqp-cpp_POPULATED)
  FetchContent_Populate(osqp-cpp)
  
  # Add this to the top of the OSQP CMakeLists.txt
  file(WRITE ${osqp-cpp_SOURCE_DIR}/CMakeLists.txt.new 
    "cmake_minimum_required(VERSION 3.5)\n\n")
  
  # Append the original content
  file(READ ${osqp-cpp_SOURCE_DIR}/CMakeLists.txt osqp_content)
  file(APPEND ${osqp-cpp_SOURCE_DIR}/CMakeLists.txt.new "${osqp_content}")
  
  # Replace the original file
  file(RENAME ${osqp-cpp_SOURCE_DIR}/CMakeLists.txt.new ${osqp-cpp_SOURCE_DIR}/CMakeLists.txt)
  
  # Add the subdirectory
  add_subdirectory(${osqp-cpp_SOURCE_DIR} ${osqp-cpp_BINARY_DIR})
endif()

# Include directories
include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}/include
) 

# Add your library
set(cddp_core_srcs
  src/cddp_core/dynamical_system.cpp
  src/cddp_core/objective.cpp
  src/cddp_core/constraint.cpp
  src/cddp_core/helper.cpp
  src/cddp_core/boxqp.cpp
  src/cddp_core/qp_solver.cpp
  src/cddp_core/cddp_core.cpp
  src/cddp_core/clddp_solver.cpp
  src/cddp_core/asddp_solver.cpp
  src/cddp_core/logddp_solver.cpp
  src/cddp_core/ipddp_solver.cpp
  src/cddp_core/msipddp_solver.cpp
  src/cddp_core/alddp_solver.cpp
)

if (CDDP_CPP_TORCH)
  list(APPEND cddp_core_srcs src/cddp_core/neural_dynamical_system.cpp)
endif()

set(dynamics_model_srcs
  src/dynamics_model/pendulum.cpp
  src/dynamics_model/unicycle.cpp
  src/dynamics_model/bicycle.cpp
  src/dynamics_model/cartpole.cpp
  src/dynamics_model/acrobot.cpp
  src/dynamics_model/car.cpp
  src/dynamics_model/forklift.cpp
  src/dynamics_model/dubins_car.cpp
  src/dynamics_model/quadrotor.cpp
  src/dynamics_model/quadrotor_rate.cpp
  src/dynamics_model/manipulator.cpp
  src/dynamics_model/spacecraft_linear.cpp
  src/dynamics_model/spacecraft_linear_fuel.cpp
  src/dynamics_model/spacecraft_nonlinear.cpp
  src/dynamics_model/dreyfus_rocket.cpp
  src/dynamics_model/spacecraft_landing2d.cpp
  src/dynamics_model/spacecraft_roe.cpp
  src/dynamics_model/lti_system.cpp
  src/dynamics_model/spacecraft_twobody.cpp
  src/dynamics_model/usv_3dof.cpp
  src/dynamics_model/quaternion_attitude.cpp
  src/dynamics_model/mrp_attitude.cpp
  src/dynamics_model/euler_attitude.cpp
)

add_library(${PROJECT_NAME} 
  ${cddp_core_srcs}
  ${dynamics_model_srcs}
)

target_link_libraries(${PROJECT_NAME} 
  $<IF:$<BOOL:${Eigen3_FOUND}>,Eigen3::Eigen,>
  matplot
  autodiff
  osqp-cpp
) 

if(NOT Eigen3_FOUND)
  target_include_directories(${PROJECT_NAME} PUBLIC ${EIGEN3_INCLUDE_DIRS})
endif()

if (CDDP_CPP_TORCH)
target_compile_definitions(${PROJECT_NAME} PRIVATE CDDP_CPP_TORCH_ENABLED=1)
  target_link_libraries(${PROJECT_NAME} ${TORCH_LIBRARIES})
endif()

target_include_directories(${PROJECT_NAME} PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/cddp-cpp>
  $<INSTALL_INTERFACE:include> 
  ${TORCH_INCLUDE_DIRS}
)

# Ensure proper CUDA support if enabled
if(TORCH_FOUND AND CDDP_CPP_TORCH_GPU)
  set_property(TARGET ${PROJECT_NAME} PROPERTY CUDA_ARCHITECTURES native)
endif()

if (CDDP_CPP_SQP AND CDDP_CPP_CASADI)
  # add sqp solver to cddp
  target_sources(${PROJECT_NAME} PRIVATE src/sqp_core/sqp_core.cpp)
endif()

if (CDDP_CPP_CASADI)
  target_include_directories(${PROJECT_NAME} PUBLIC ${CASADI_INCLUDE_DIR})
  target_link_libraries(${PROJECT_NAME} ${CASADI_LIBRARIES})
endif()

if (CDDP_CPP_ACADO)
  target_link_libraries(${PROJECT_NAME} ${ACADO_DIR}/build/lib/libacado_toolkit_s.so)
endif()

# Gurobi 
if (CDDP_CPP_GUROBI)
  if (NOT GUROBI_ROOT)
    message(FATAL_ERROR "Please set GUROBI_ROOT.")
  endif()
  set(GUROBI_INCLUDE_DIRS ${GUROBI_ROOT}/include) # Set the path to the Gurobi include directory
  set(GUROBI_LIBRARIES ${GUROBI_ROOT}/lib/libgurobi_c++.a) # Set the path to the Gurobi library
  find_library(GUROBI_LIBRARY gurobi_c++ PATHS ${GUROBI_ROOT}/lib)
  find_path(GUROBI_INCLUDE_DIR gurobi_c++.h PATHS ${GUROBI_ROOT}/include)
  link_directories(${GUROBI_ROOT}/lib) 

  if (GUROBI_LIBRARIES AND GUROBI_INCLUDE_DIR)
    message(STATUS "Found Gurobi: ${GUROBI_LIBRARIES} ${GUROBI_LIBRARY}")
    message(STATUS "Gurobi include directory: ${GUROBI_INCLUDE_DIR}")
    
    include_directories(${GUROBI_INCLUDE_DIR})
    target_link_libraries(${PROJECT_NAME} ${GUROBI_LIBRARIES} gurobi_c++ 
    # Platform-specific Gurobi library
    $<IF:$<BOOL:APPLE>,gurobi120,gurobi120>) # Adjust version as needed
    message(STATUS "Successfully linked Gurobi version 120.") 
  else()
    message(FATAL_ERROR "Could not find Gurobi. Please set GUROBI_ROOT.")
  endif()
endif()


# SNOPT
if (CDDP_CPP_SNOPT)
  if (NOT SNOPT_ROOT)
    message(FATAL_ERROR "Please set SNOPT_ROOT to your SNOPT installation directory.")
  endif()
  
  # Set SNOPT paths
  set(SNOPT_INCLUDE_DIRS ${SNOPT_ROOT}/include)
  set(SNOPT_LIB_DIR ${SNOPT_ROOT}/lib)
  
  # Find SNOPT libraries 
  find_library(SNOPT_LIBRARY NAMES snopt7 snopt snopt7_cpp PATHS ${SNOPT_LIB_DIR})
  
  # Find header files
  find_path(SNOPT_INCLUDE_DIR snopt.h PATHS ${SNOPT_INCLUDE_DIRS})
  
  # Set up library list
  set(SNOPT_LIBRARIES)
  if(SNOPT_LIBRARY)
    list(APPEND SNOPT_LIBRARIES ${SNOPT_LIBRARY})
  endif()
  
  if (SNOPT_LIBRARIES AND SNOPT_INCLUDE_DIR)
    message(STATUS "Found SNOPT libraries: ${SNOPT_LIBRARIES}")
    message(STATUS "SNOPT include directory: ${SNOPT_INCLUDE_DIR}")
    
    include_directories(${SNOPT_INCLUDE_DIR})
    link_directories(${SNOPT_LIB_DIR})
    target_link_libraries(${PROJECT_NAME} ${SNOPT_LIBRARIES})
    
    # Add preprocessor definition to enable SNOPT in code
    target_compile_definitions(${PROJECT_NAME} PRIVATE CDDP_CPP_SNOPT_ENABLED=1)
    
    message(STATUS "Successfully linked SNOPT.")
  else()
    message(FATAL_ERROR "Could not find SNOPT libraries. Please check SNOPT_ROOT: ${SNOPT_ROOT}")
  endif()
endif()

# ACADOS
if (CDDP_CPP_ACADOS)
  if (NOT ACADOS_ROOT)
    message(FATAL_ERROR "Please set ACADOS_ROOT to your ACADOS installation directory.")
  endif()
  
  # Set ACADOS paths
  set(ACADOS_INCLUDE_DIRS ${ACADOS_ROOT}/include)
  set(ACADOS_LIB_DIR ${ACADOS_ROOT}/lib)
  set(BLASFEO_INCLUDE_DIR ${ACADOS_ROOT}/include/blasfeo/include)
  set(HPIPM_INCLUDE_DIR ${ACADOS_ROOT}/include/hpipm/include)
  
  # Find ACADOS libraries 
  find_library(ACADOS_LIBRARY NAMES acados PATHS ${ACADOS_LIB_DIR})
  find_library(HPIPM_LIBRARY NAMES hpipm PATHS ${ACADOS_LIB_DIR})
  find_library(BLASFEO_LIBRARY NAMES blasfeo PATHS ${ACADOS_LIB_DIR})
  find_library(QPOASES_E_LIBRARY NAMES qpOASES_e PATHS ${ACADOS_LIB_DIR})
  
  # Find header files
  find_path(ACADOS_C_INCLUDE_DIR acados_c/ocp_nlp_interface.h PATHS ${ACADOS_INCLUDE_DIRS})
  
  # Set up library list
  set(ACADOS_LIBRARIES)
  if(ACADOS_LIBRARY)
    list(APPEND ACADOS_LIBRARIES ${ACADOS_LIBRARY})
  endif()
  if(HPIPM_LIBRARY)
    list(APPEND ACADOS_LIBRARIES ${HPIPM_LIBRARY})
  endif()
  if(BLASFEO_LIBRARY)
    list(APPEND ACADOS_LIBRARIES ${BLASFEO_LIBRARY})
  endif()
  if(QPOASES_E_LIBRARY)
    list(APPEND ACADOS_LIBRARIES ${QPOASES_E_LIBRARY})
  endif()
  
  if (ACADOS_LIBRARIES AND ACADOS_C_INCLUDE_DIR)
    message(STATUS "Found ACADOS libraries: ${ACADOS_LIBRARIES}")
    message(STATUS "ACADOS include directory: ${ACADOS_C_INCLUDE_DIR}")
    
    include_directories(${ACADOS_C_INCLUDE_DIR})
    include_directories(${BLASFEO_INCLUDE_DIR})
    include_directories(${HPIPM_INCLUDE_DIR})
    link_directories(${ACADOS_LIB_DIR})
    target_link_libraries(${PROJECT_NAME} ${ACADOS_LIBRARIES})
    
    # Add preprocessor definition to enable ACADOS in code
    target_compile_definitions(${PROJECT_NAME} PRIVATE CDDP_CPP_ACADOS_ENABLED=1)
    
    message(STATUS "Successfully linked ACADOS.")
  else()
    message(FATAL_ERROR "Could not find ACADOS libraries. Please check ACADOS_ROOT: ${ACADOS_ROOT}")
  endif()
endif()

# Build and register tests.
if (CDDP_CPP_BUILD_TESTS)
  add_subdirectory(tests) 
endif()

# Build examples
add_subdirectory(examples)

# Cmake compile commmand:
# $ mkdir build
# $ cd build
# $ cmake -DCDDP_CPP_BUILD_TESTS=ON -DCDDP_CPP_TORCH=ON -DCDDP_CPP_TORCH_GPU=ON -DLIBTORCH_DIR=/usr/local/lib/libtorch -DCDDP_CPP_SQP=ON -DCDDP_CPP_GUROBI=ON -DGUROBI_ROOT=/usr/local/lib/gurobi1201/linux64 -DCDDP_CPP_CASADI=ON -DCDDP_CPP_ACADO=ON ..
# $ make -j4
# $ make test

