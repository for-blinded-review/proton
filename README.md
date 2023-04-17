# proton

This repository is intended for peer review of the paper ``stackful coroutine made fast''. It includes source code of:
* an early version of proton tailored for ease of use in the experiments: **/common/protonlib**
* scheduler for boost: **/common/fibsched**
* scheduler for C++20 stackless coroutine: **/common/co20executor**
* benchmarks: **/*-test**

And our modification to Clang 15.03 is placed in another repo at: https://github.com/for-blinded-review/llvm-project

