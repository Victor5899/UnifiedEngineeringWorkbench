# Unified Engineering Workbench

Initial implementation scaffold for the PASS Science Works common assignment.

## Current status
- [x] C++17 CMake project structure
- [x] 1D steady-state thermal FEM solver (two-node linear elements)
- [x] Analytical solution and element-midpoint error
- [x] CSV export
- [x] Initial positive/negative/convergence tests
- [ ] STL and temperature CSV import/visualization
- [ ] Serial/parallel batch benchmark
- [ ] Embedded mock adapters
- [ ] Backpropagation implementation
- [ ] Frontend and Windows standalone packaging

## Build on macOS

Install the command-line tools if needed:
```bash
xcode-select --install
```

Install CMake (Homebrew):
```bash
brew install cmake
```

Build and test:
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Run the solver:
```bash
./build/workbench 10 results_N10.csv
./build/workbench 20 results_N20.csv
./build/workbench 40 results_N40.csv
./build/workbench 80 results_N80.csv
```

## Engineering notes
The governing equation is `-d/dx(kA dT/dx) = qA`. For a linear two-node element of length `h`, the element stiffness is
`(kA/h) [[1,-1],[-1,1]]` and the consistent element load is `(qAh/2)[1,1]^T`.
The end temperatures are imposed as essential (Dirichlet) boundary conditions.

## Platform note
Development is initially on macOS. The required Windows 11 x64 `Launch.exe` must be built and tested on Windows (for example, using a Windows GitHub Actions runner or a Windows machine). A macOS executable is not a substitute for that deliverable.
