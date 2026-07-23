# Cross compilation toolchain for AArch64, used for the NEON port. Correctness
# is checked locally under qemu-user; real timing numbers come only from native
# ARM CI runners (Section 4 rule 3: QEMU timing is never reported).
#
# Static linking keeps qemu-user invocation simple (no sysroot library path
# needed at run time), and CMAKE_CROSSCOMPILING_EMULATOR lets ctest run the
# cross built tests through qemu-aarch64 automatically.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)

set(CMAKE_FIND_ROOT_PATH /usr/aarch64-linux-gnu)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# Static so qemu-user does not need the target sysroot at run time.
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static -static-libgcc -static-libstdc++")

# Let ctest run cross built test binaries under emulation.
find_program(QEMU_AARCH64 qemu-aarch64)
if(QEMU_AARCH64)
    set(CMAKE_CROSSCOMPILING_EMULATOR ${QEMU_AARCH64})
endif()
