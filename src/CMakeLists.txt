# Benchmark executables. Each is built through add_benchmark so it gets the
# vector flags, hot warnings, and LTO forced off.

add_benchmark(pointer_chase pointer_chase.cpp)
add_benchmark(stream stream.cpp)
add_benchmark(peak_flops peak_flops.cpp)
