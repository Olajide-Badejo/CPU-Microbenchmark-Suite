# Architecture detection. Sets two variables consumed by the add_benchmark
# helpers in the root CMakeLists:
#
#   ARCH_VECTOR_FLAGS    flags for the vectorized and intrinsic kernels
#   ARCH_BASELINE_FLAGS  flags for the deliberately scalar baseline
#
# On x86_64 we build for the native machine (Raptor Lake: AVX2 + FMA, no
# AVX-512). On aarch64 we target a NEON baseline that QEMU and the ARM CI
# runners both accept. Cross compilation sets CMAKE_SYSTEM_PROCESSOR via the
# toolchain file, so we key off that rather than the host.

if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    set(ARCH_IS_AARCH64 TRUE)
    # NEON is mandatory in the AArch64 base, so no special flag is needed to
    # enable it. We avoid -march=native under QEMU emulation because the
    # emulated feature set is not the CI runner feature set.
    set(ARCH_VECTOR_FLAGS "" CACHE INTERNAL "")
    set(ARCH_BASELINE_FLAGS "" CACHE INTERNAL "")
    message(STATUS "Target architecture: AArch64 (NEON baseline)")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    set(ARCH_IS_X86 TRUE)
    set(ARCH_VECTOR_FLAGS "-march=native" CACHE INTERNAL "")
    # The scalar baseline still targets the native machine for a fair clock,
    # it just has vectorization suppressed by the caller.
    set(ARCH_BASELINE_FLAGS "-march=native" CACHE INTERNAL "")
    message(STATUS "Target architecture: x86_64 (-march=native)")
else()
    message(WARNING "Unrecognized processor '${CMAKE_SYSTEM_PROCESSOR}', building generic")
    set(ARCH_VECTOR_FLAGS "" CACHE INTERNAL "")
    set(ARCH_BASELINE_FLAGS "" CACHE INTERNAL "")
endif()
