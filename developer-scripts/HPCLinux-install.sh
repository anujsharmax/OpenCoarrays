#!/usr/bin/env bash

# Install OpenCoarrays on 

if [[ ! -f src/libcaf.h  ]]; then
  emergency "Run this script at the top level of the OpenCoarrays source tree." 
fi

pushd prerequisites

  # Build CMake using the system GCC (4.8.1) and prepend its bin subdirectory to the PATH
  export cmake_install_path="${PWD}"/installations/cmake/cmake
  ./build.sh --package cmake --install-prefix "${cmake_install_path}"
  if [[ -z "${PATH}" ]]; then
    export PATH="${cmake_install_path}"/bin
  else
    export PATH="${cmake_install_path}"/bin:$PATH
  fi

  # Build GCC 6.3.0 and prepend its bin subdirectory to the PATH
  export gcc_version=6.3.0
  export gcc_install_path="${PWD}"/installations/gnu/$gcc_version
  ./build.sh --package gcc --install-version $gcc_version --install-prefix "${gcc_install_path}"
  export PATH="${gcc_install_path}"/bin:$PATH
  export LD_LIBRARY_PATH="${gcc_install_path}"/lib64:"${gcc_install_path}"/lib:$LD_LIBRARY_PATH

  # Build MPICH 3.1.4 and prepend its bin subdirectory to the PATH
  export mpich_version=3.1.4
  export mpich_install_path="${PWD}"/installations/mpich
  ./build.sh --package mpich --install-version $mpich_version --install-prefix "${mpich_install_path}" --num-threads 4
  export PATH="${mpich_install_path}"/bin:$PATH

popd # return to top level of OpenCoarrays source tree

# Build OpenCoarrays
if [[ -d build ]]; then
  rm -rf build
fi
mkdir build
pushd build

  export opencoarrays_install_path="${PWD}"/prerequisites/installations/opencoarrays
  FC=gfortran CC=gcc cmake .. -DCMAKE_INSTALL_PREFIX="${opencoarrays_install_path}"
  make
  ctest
  make install
  export PATH="${opencoarrays_install_path}"/bin:$PATH

popd # return to top level of OpenCoarrays source tree
