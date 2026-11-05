# RLtools dependency

The `include` directory and `LICENSE` are unmodified upstream files from
https://github.com/rl-tools/rl-tools at commit
`b32d9985c65a5e098a6bbf190fd994962d288b99` (also recorded in `REVISION`).

Headers are committed so a clone can build without fetching another dependency.
This is the canonical copy for Schola and the existing native examples. The
`RLTools` Unreal external module provides system include paths, validates the pin,
and stages the MIT notice as `RLTools-LICENSE.txt`. No Python service, downloaded
binary, BLAS library, or GPU runtime is required for the generic CPU path.

`ScholaRLTools` owns the native integration's C++20 and precise floating-point
settings. Its operation headers are compiled in one private translation unit;
public Schola headers never include RLtools. Existing standalone example build
scripts reference this same include tree without requiring Unreal.

The headers and license were moved byte-for-byte from
`Examples/AdaptiveTag/ThirdParty/RLtools`. No upstream code was patched. The hash
embedded in upstream `version.h` is historical; `REVISION` records the vendored
source revision. To restore a missing copy, run
`Examples/AdaptiveTag/Prepare-LearningDemo.ps1` from the repository checkout.

The native integration is currently validated on Unreal Engine 5.8 / Windows.
Other engine versions, platforms, and accelerated numerical backends require
separate validation.
