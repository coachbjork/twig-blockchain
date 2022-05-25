#! /usr/bin/bash

echo "Mandel Pinned Build"

if [[ $NAME != "Ubuntu" ]]
   then
      echo "Currently only supporting Ubuntu based builds. Proceed at your own risk."
fi

if [ $# -eq 0 ] || [ -z "$1" ]
   then
      echo "Please supply a directory for the build dependencies to be placed and a value for the number of jobs to use for building."
      echo "./pinned_build.sh <directory> 1-100"
      exit -1
fi

CORE_SYM=EOS
DEP_DIR=$1
JOBS=$2
CLANG_VER=11.0.1
BOOST_VER=1.70.0
LLVM_VER=7.1.0
LIBPQXX_VER=7.2.1
ARCH=`uname -m`
SCRIPT_DIR="$( cd -- "$( dirname -- "${BASH_SOURCE[0]:-$0}"; )" &> /dev/null && pwd 2> /dev/null; )";


pushdir() {
   DIR=$1
   mkdir -p ${DIR}
   pushd ${DIR} &> /dev/null
}

popdir() {
   EXPECTED=$1
   D=`popd`
   popd &> /dev/null
   echo ${D}
   D=`eval echo $D | head -n1 | cut -d " " -f1`

   if [[ ${D} != ${EXPECTED} ]]; then
     echo "Directory is not where expected EXPECTED=${EXPECTED} at ${D}"
     exit 1 
   fi
}

install_clang() {
   CLANG_DIR=$1
   if [ ! -d "${CLANG_DIR}" ]; then
      echo "Installing Clang ${CLANG_VER} @ ${CLANG_DIR}"
      mkdir -p ${CLANG_DIR}
      wget https://github.com/llvm/llvm-project/releases/download/llvmorg-${CLANG_VER}/clang+llvm-${CLANG_VER}-x86_64-linux-gnu-ubuntu-16.04.tar.xz
      tar -xvf clang+llvm-${CLANG_VER}-x86_64-linux-gnu-ubuntu-16.04.tar.xz -C ${CLANG_DIR}
      pushdir ${CLANG_DIR}
      mv clang+*/* .
      popdir ${DEP_DIR}
      rm clang+llvm-${CLANG_VER}-x86_64-linux-gnu-ubuntu-16.04.tar.xz
   fi
   export PATH=${CLANG_DIR}/bin:$PATH
   export CLANG_DIR=${CLANG_DIR}
}

install_llvm() {
   LLVM_DIR=$1
   if [ ! -d "${LLVM_DIR}" ]; then
      echo "Installing LLVM ${LLVM_VER} @ ${LLVM_DIR}"
      mkdir -p ${LLVM_DIR}
      wget https://github.com/llvm/llvm-project/releases/download/llvmorg-${LLVM_VER}/llvm-${LLVM_VER}.src.tar.xz
      tar -xvf llvm-${LLVM_VER}.src.tar.xz
      pushdir "${LLVM_DIR}.src"
      pushdir build
      cmake -DCMAKE_TOOLCHAIN_FILE=${SCRIPT_DIR}/pinned_toolchain.cmake -DCMAKE_INSTALL_PREFIX=${LLVM_DIR} -DLLVM_TARGETS_TO_BUILD=host -DLLVM_BUILD_TOOLS=Off -DLLVM_ENABLE_RTTI=On -DLLVM_ENABLE_TERMINFO=Off -DCMAKE_EXE_LINKER_FLAGS=-pthread -DCMAKE_SHARED_LINKER_FLAGS=-pthread -DLLVM_ENABLE_PIC=NO ..
      make -j${JOBS} 
      make -j${JOBS} install
      popdir "${LLVM_DIR}.src"
      popdir ${DEP_DIR}
      rm -rf ${LLVM_DIR}.src 
      rm llvm-${LLVM_VER}.src.tar.xz
   fi
   export LLVM_DIR=${LLVM_DIR}
}

install_boost() {
   BOOST_DIR=$1

   if [ ! -d "${BOOST_DIR}" ]; then
      echo "Installing Boost ${BOOST_VER} @ ${BOOST_DIR}"
      wget https://boostorg.jfrog.io/artifactory/main/release/${BOOST_VER}/source/boost_${BOOST_VER//\./_}.tar.gz
      tar -xvzf boost_${BOOST_VER//\./_}.tar.gz -C ${DEP_DIR}
      pushdir ${BOOST_DIR}
      if [[ ${ARCH}=="x86_64" ]]; then
         ./bootstrap.sh -with-toolset=clang --prefix=${BOOST_DIR}/bin
         ./b2 toolset=clang cxxflags='-stdlib=libc++ -D__STRICT_ANSI__ -nostdinc++ -I${CLANG_DIR}/include/c++/v1 -D_FORTIFY_SOURCE=2 -fstack-protector-strong -fPIE' linkflags='-stdlib=libc++ -pie' link=static threading=multi --with-iostreams --with-date_time --with-filesystem --with-system --with-program_options --with-chrono --with-test -q -j${JOBS} install
      else
         ./bootstrap.sh --prefix=${BOOST_DIR}/bin
         ./b2 cxxflags='-D__STRICT_ANSI__ -D_FORTIFY_SOURCE=2 -fstack-protector-strong -fPIE' linkflags='-pie' link=static threading=multi --with-iostreams --with-date_time --with-filesystem --with-system --with-program_options --with-chrono --with-test -q -j${JOBS} install
      fi
      popdir ${DEP_DIR}
      rm boost_${BOOST_VER//\./_}.tar.gz
   fi
   export BOOST_DIR=${BOOST_DIR}
}

pushdir ${DEP_DIR} # dir stack <DEP_DIR>/

if [[ ${ARCH}=="x86_64" ]]; then
   install_clang ${DEP_DIR}/clang-${CLANG_VER}
   install_llvm ${DEP_DIR}/llvm-${LLVM_VER}
   CMAKE_ARG="-DCMAKE_TOOLCHAIN_FILE=${SCRIPT_DIR}/pinned_toolchain.cmake"
fi

install_boost ${DEP_DIR}/boost_${BOOST_VER//\./_} "-with-toolset=clang"

popdir ${SCRIPT_DIR}


# build Mandel
MANDEL_DIR=${DEP_DIR}/mandel

echo "Building Mandel"
pushdir ../build

cmake ${CMAKE_ARG} -DCMAKE_INSTALL_PREFIX=/usr/local -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=${LLVM_DIR}/lib/cmake -DCMAKE_PREFIX_PATH=${BOOST_DIR}/bin ..

make -j${JOBS}
cpack

echo " .----------------.  .----------------.  .-----------------. .----------------.  .----------------.  .----------------. ";
echo "| .--------------. || .--------------. || .--------------. || .--------------. || .--------------. || .--------------. |";
echo "| | ____    ____ | || |      __      | || | ____  _____  | || |  ________    | || |  _________   | || |   _____      | |";
echo "| ||_   \  /   _|| || |     /  \     | || ||_   \|_   _| | || | |_   ___ \`.  | || | |_   ___  |  | || |  |_   _|     | |";
echo "| |  |   \/   |  | || |    / /\ \    | || |  |   \ | |   | || |   | |   \`. \ | || |   | |_  \_|  | || |    | |       | |";
echo "| |  | |\  /| |  | || |   / ____ \   | || |  | |\ \| |   | || |   | |    | | | || |   |  _|  _   | || |    | |   _   | |";
echo "| | _| |_\/_| |_ | || | _/ /    \ \_ | || | _| |_\   |_  | || |  _| |___.' / | || |  _| |___/ |  | || |   _| |__/ |  | |";
echo "| ||_____||_____|| || ||____|  |____|| || ||_____|\____| | || | |________.'  | || | |_________|  | || |  |________|  | |";
echo "| |              | || |              | || |              | || |              | || |              | || |              | |";
echo "| '--------------' || '--------------' || '--------------' || '--------------' || '--------------' || '--------------' |";
echo " '----------------'  '----------------'  '----------------'  '----------------'  '----------------'  '----------------' ";
echo "Mandel has successfully finished building and creating packages.  Enjoy!!!"
