# Proton

This repository is intended for peer review of the paper _stackful coroutine made fast_. 

It includes source code of:
* an early version of proton tailored for ease of use in the experiments: **/common/protonlib**
* scheduler for boost: **/common/fibsched**
* scheduler for C++20 stackless coroutine: **/common/co20executor**
* benchmarks: **/*-test**

Our modifications to Clang 15.03 are placed in another repo at: https://github.com/for-blinded-review/llvm-project

We'll change the name and repo url into real ones, after passing the blided review process.
