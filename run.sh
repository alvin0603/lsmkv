cmake -S . -B build
cmake --build build --parallel 8
ctest --test-dir build --output-on-failure