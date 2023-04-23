# Proton

This repository is intended for peer review of the paper _stackful coroutine made fast_. 

It includes source code of:
* an early version of proton tailored for ease of use in the experiments: **/common/protonlib**
* scheduler for boost: **/common/fibsched**
* scheduler for C++20 stackless coroutine: **/common/co20executor**
* benchmarks: **/*-test**

Our modifications to Clang 15.03 are placed in another repo at: https://github.com/for-blinded-review/llvm-project

The relavent commits are:
* [[PATCH 1/2] Add preserve_none calling convension](https://github.com/for-blinded-review/llvm-project/commit/4aed4ec8b32a3abc24f805d2a68e5d3903e19f45)
* [[PATCH 2/2] add x86-disable-align-stack-via-push option to disable us…](https://github.com/for-blinded-review/llvm-project/commit/2e84059271faacccbb9a7d25347a7d56681cf294)


We'll change the name and repo url into real ones, after passing the blided review process.


Thanks!


# How to build

## 1. build customized llvm from scratch

```
git clone https://github.com/for-blinded-review/llvm-project.git
cd llvm-project && git checkout preserve_none

BUILD_DIR="${PWD}/build"

cmake -G Ninja \
    -S "${SRC_DIR}/llvm" \
    -B "${BUILD_DIR}" \
    -DLLVM_ENABLE_PROJECTS="clang;lld;compiler-rt" \
    -DCMAKE_BUILD_TYPE=Release \
    -DLLVM_ENABLE_RUNTIMES="libcxx;libcxxabi" \
    -DLLVM_ENABLE_ASSERTIONS=On \
    -DLLVM_TARGETS_TO_BUILD=X86 \
    -DLLVM_APPEND_VC_REV=OFF

pushd ${BUILD_DIR}
ninja
popd

# add clang library to the search path
export LD_LIBRARY_PATH=${BUILD_DIR}/lib/x86_64-unknown-linux-gnu:$LD_LIBRARY_PATH
```

## 2. build proton with customized clang-15
```
git clone https://github.com/for-blinded-review/proton.git
cd proton

compiler_path="${PWD}/build"
mkdir build && cd build

cmake .. -DCMAKE_C_COMPILER=${compiler_path}/bin/clang -DCMAKE_CXX_COMPILER=${compiler_path}/bin/clang++
make -j
```
