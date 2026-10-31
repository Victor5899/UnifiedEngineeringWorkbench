# Unified Engineering Workbench

Initial implementation scaffold for the PASS Science Works common assignment.

## Current status
- [x] C++17 CMake project structure
- [x] 1D steady-state thermal FEM solver (two-node linear elements)
- [x] Analytical solution and element-midpoint error
- [x] CSV export
- [x] Initial positive/negative/convergence tests
- [x] ASCII and binary STL import
- [x] Nodal temperature CSV import
- [x] Local HTTP API over the C++ importers
- [x] STL and temperature visualization
- [x] Serial/parallel batch benchmark
- [ ] Embedded mock adapters
- [ ] Backpropagation implementation
- [ ] Windows 11 x64 `Launch.exe` packaging

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

Run the thermal solver:
```bash
./build/workbench 10 results_N10.csv
./build/workbench 20 results_N20.csv
./build/workbench 40 results_N40.csv
./build/workbench 80 results_N80.csv
```

Inspect a geometry file or a nodal temperature table:
```bash
./build/workbench inspect-stl data/geometry/sample_geometry.stl
./build/workbench inspect-temperature data/thermal/sample_temperature.csv
```

`inspect-stl` and `inspect-temperature` take exactly one file path. Any other first argument is still the thermal element count, so the original solver command is unchanged.

## Workbench UI

The viewer is a React application. It does not parse STL or CSV itself. It uploads the file to `workbench_server`, which calls the existing C++ importers and returns JSON.

Prerequisites:

- CMake 3.16 or newer, and a C++17 compiler
- Node.js 18 or newer
- Network access the first time CMake configures the API, so it can fetch cpp-httplib v0.18.3 and nlohmann/json v3.11.3. Later builds use the CMake download cache. The running API does not need network access.
- Python is not required.

Build the C++ backend and tests:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

On Visual Studio, the server executable is `build\Release\workbench_server.exe` (or `build\Debug\...` for a Debug build). With Makefiles or Ninja it is `build/workbench_server`.

Start the API. It binds to `127.0.0.1:8080` unless you change the host or port:

```bash
./build/workbench_server
```

```text
workbench_server --host 127.0.0.1 --port 8080
```

Install and start the frontend from `frontend/`:

```bash
npm install
npm run dev
```

Open `http://127.0.0.1:5173`. Vite proxies `/api` to `http://127.0.0.1:8080`. Override that with `WORKBENCH_API_ORIGIN` when the API is on another port.

Production frontend build:

```bash
npm run build
npm run preview
```

`npm run preview` serves `frontend/dist` on `http://127.0.0.1:4173` and uses the same API proxy. The C++ server can also serve that directory:

```bash
./build/workbench_server --static frontend/dist
```

Direct dependency versions are pinned in `frontend/package.json`. `frontend/package-lock.json` records the full tree.

### HTTP API

| Method | Path | Body |
| --- | --- | --- |
| GET | `/api/health` | none |
| POST | `/api/geometry/stl` | `multipart/form-data` field `file` |
| POST | `/api/thermal/csv` | `multipart/form-data` field `file` |
| GET | `/api/performance/hardware` | none |
| POST | `/api/performance/benchmark` | JSON workload configuration |
| GET | `/api/performance/status` | none |
| GET | `/api/performance/results` | none |
| GET | `/api/performance/export` | none |

The upload limit defaults to 64 MiB, matching the importer limits. The client-supplied filename is a display label only. The server stores the bytes under a generated name in a temporary directory and deletes that file before the response is returned.

A binary STL is much smaller than the JSON triangle list. Geometry responses are written as one string, without a JSON object tree. A mesh whose JSON would exceed 384 MiB (about 899,000 triangles at the budgeted size) is rejected with HTTP 413 after parsing. A 25 MiB binary file is about 525,000 triangles and stays inside that budget. The 64 MiB upload limit was not raised.

A successful STL response contains `filename`, `solid_name`, `triangle_count`, `vertex_count`, `triangles` (`normal` and `vertices` as `[x, y, z]`), `bounding_box.min`, `bounding_box.max`, `dimensions`, and `coordinate_units` (`file`).

A successful temperature response contains `filename`, `node_count`, `nodes` (`node_id`, `x_m`, `y_m`, `z_m`, `temperature_C`), `minimum_temperature_C`, `maximum_temperature_C`, `average_temperature_C`, `bounding_box`, `dimensions`, `coordinate_units` (`m`), and `temperature_units` (`C`).

Failures return JSON `{ "error": "..." }` with status 400 for a rejected file, 413 for an oversized upload, and 500 for an unexpected server failure. Parser messages do not include the temporary path.

Geometry, Thermal Analysis, and Performance are implemented. Hardware remains a labelled placeholder. The point cloud is not registered to the STL.

`POST /api/performance/benchmark` accepts JSON `seed`, `batch_size`, `mesh_size`, `repetitions`, and an optional `worker_counts` array of 1, 2, 3, and 4. It returns 202 and runs the job on a background thread, so `/api/health` and the geometry routes stay available. A second benchmark while one is running returns 409. Invalid workloads return 400. Results and CSV are published only after the job finishes. `GET /api/performance/export` returns `text/csv`.

Run the same benchmark from the command line:

```bash
./build/workbench benchmark --seed 42 --batch 8 --mesh 128 --repetitions 5 --workers 1,2,3,4 --output benchmark.csv
```

## ASCII STL import

The importer reads one ASCII STL solid. Keywords are the lowercase words from the STL specification:

```text
solid <name>
  facet normal <ni> <nj> <nk>
    outer loop
      vertex <x> <y> <z>
      vertex <x> <y> <z>
      vertex <x> <y> <z>
    endloop
  endfacet
endsolid <name>
```

- The solid name is the rest of the `solid` line. A `facet` or `endsolid` keyword on that line is still recognized.
- Whitespace, including CR LF, separates tokens. Scientific notation is accepted.
- Triangle count is the number of `facet` records.
- Vertex count is the number of `vertex` records, three per triangle. ASCII STL does not share vertices between triangles, and this importer does not weld them.
- The bounding box and dimensions use vertex coordinates only. Normals are stored and are not part of the box.
- The STL format has no unit field. Coordinates are reported as file units. `data/geometry/sample_geometry.stl` is a synthetic block authored in metres. See `data/PROVENANCE.md`.

Validation:

- An empty or whitespace-only file is rejected.
- A solid with no facets is rejected.
- A missing or unexpected keyword, the wrong number of vertices, or a non-numeric token is rejected with the file path and line number.
- NaN and infinity are rejected in normals and vertex coordinates.
- Text that does not start with `solid` is rejected by the ASCII parser.
- Only one solid is accepted. Tokens after the `endsolid` line are rejected.
- A non-regular path or a missing file is rejected.
- `parse_ascii_stl` still rejects binary input. `parse_stl` and the HTTP API accept both.

Default limits (`StlImportLimits`):

- Maximum file size: 64 MiB (67,108,864 bytes).
- Maximum triangle count: 2,000,000.

Both limits are arguments of `parse_ascii_stl` and `parse_stl`. A limit of zero is rejected before the file is read.

### Binary STL

`parse_stl` selects the encoding before reading triangles:

- A file whose length is exactly `84 + triangle_count * 50` is binary. This includes an 80-byte header that begins with the letters `solid`.
- If that same length belongs to ASCII text that starts with `solid` and contains `facet` or `endsolid`, it stays on the ASCII parser.
- Anything else with non-text bytes is diagnosed as binary, so a truncated or over-long file reports a size error instead of an ASCII syntax error.

Each record is a little-endian normal, three vertices, and a 2-byte attribute count. The attribute is ignored. The header's printable text, with a leading `solid` keyword removed, becomes `solid_name`. Coordinates must be finite. A count of zero, a truncated file, and a file whose length does not match the declared count are rejected. The bounding box uses vertices only, and the vertex count is three times the triangle count. Vertices are not welded.

## Temperature CSV import

The importer reads a nodal table. It does not read ANSYS `.rst`, `.rth`, or `.cdb` files, and it does not register nodes to an STL mesh.

Required columns:

```text
node_id,x_m,y_m,z_m,temperature_C
```

- `node_id` is a base-10 signed integer. A leading `+` is accepted. `1` and `01` are the same id.
- `x_m`, `y_m`, and `z_m` are metres.
- `temperature_C` is degrees Celsius.
- The header names are required. Their order may vary. Extra or unrecognized columns are rejected.
- Fields are unquoted. Spaces around fields are ignored.
- Blank lines are rejected.
- The node count, minimum, maximum, and arithmetic-mean temperature are calculated from the accepted rows.
- The spatial bounding box is calculated from `x_m`, `y_m`, and `z_m`.
- Rows stay in file order.

`data/thermal/sample_temperature.csv` is a **synthetic** 27-node lattice. It is not an ANSYS export. Its origin, formula, and units are recorded in `data/PROVENANCE.md`.

Validation:

- An empty file is rejected.
- A header with no data rows is rejected.
- A missing column, missing field, or wrong number of fields is rejected with the 1-based row number.
- A non-numeric token is rejected with the row number and column name.
- NaN and infinity are rejected.
- A repeated `node_id` is rejected. The message includes the current row and the row that first used that id.
- A missing file or a non-regular path is rejected.

Default limits (`TemperatureCsvLimits`):

- Maximum file size: 64 MiB (67,108,864 bytes).
- Maximum node count: 5,000,000.

Numeric text is interpreted in the C locale. The program does not change the process locale.

## Performance benchmark

The Performance Lab solves batches of the existing 1D steady thermal model. It does not replace that model. The governing equation remains `-d/dx(kA dT/dx) = qA`. For constant `k`, `A`, and `q`, the solver uses two-node linear elements, the element stiffness `(kA/h) [[1,-1],[-1,1]]`, the consistent load `(qAh/2)[1,1]^T`, and Dirichlet temperatures at the two ends. Each benchmark case calls `solve_steady_1d`.

### Workload generation

`generate_workload` builds the batch once. Every warm-up and every measured repetition, serial and parallel, receives that same vector of cases.

The generator is `std::mt19937` seeded with the configured `uint32` seed. Each case draws four values, in order: conductivity, volumetric heat generation, left temperature, right temperature. A draw is

```text
u = mt19937() / 2^32          # [0, 1)
value = minimum + (maximum - minimum) * u
```

| Quantity | Range | Units |
| --- | --- | --- |
| Conductivity | [0.2, 400) | W/(m·K) |
| Heat generation | [1, 500000) | W/m³ |
| Left temperature | [-20, 500) | °C |
| Right temperature | [-20, 400) | °C |

Length is fixed at 1 m and cross-section area at `1e-4` m², both positive. The mesh size is the element count for every case, from 1 to 320. Batch size is 1 to 256. The same seed, batch size, and mesh size produce the same cases. Conductivity and heat generation are strictly positive. Both boundary values are finite Dirichlet temperatures, which is the boundary-condition set the existing solver accepts.

A request is also rejected when `executions * batch_size * mesh_size^3` exceeds 80,000,000,000. `executions` is `(1 + worker_counts) * (1 + repetitions)`: one excluded warm-up and the measured repetitions, for the serial baseline and every worker count. That limit keeps one request from scheduling a very large dense factorization. On this solver a mesh of a few hundred elements is still a short run, so the cap sits well above the workloads used for measurement.

### Worker pool

Parallel cases run on a fixed pool of `std::thread` workers. The pool size is 1, 2, 3, or 4 and is never larger than the logical CPU count reported for this process. When that count cannot be discovered, the limit is 1. The pool does not create one thread per case.

The control thread owns the pool. Workers wait on a condition variable. The queue mutex is held only while a worker claims the next case index. The thermal solve runs without that mutex. Each case index is handed out once and written into its own pre-sized result slot, so workers do not share a mutable solver or a mutable result element. If a solve throws, the remaining cases still finish, the first exception is rethrown after the batch, and the pool can be reused. The pool destructor sets a stop flag, wakes the workers, and joins them.

Result slot `i` always belongs to generated case `i`. Completion order does not change that.

### Benchmark method

For one benchmark the runner:

1. Generates the workload once and records that duration separately.
2. Creates each worker pool before that pool is timed. Thread construction stays outside the warm-up and the measured repetitions.
3. Runs one warm-up of the full batch for every configuration that will be measured: the serial baseline and each worker count (1, 2, 3, and 4, or the requested subset that the machine allows). Every warm-up uses the same generated cases. Those durations are stored separately and are excluded from the repetition lists, the medians, the speedups, and the efficiency.
4. Runs five measured rounds by default. Each round solves the same batch once on the serial baseline and once on every worker pool. Round 0 uses the order serial, then worker counts ascending. Round `r` is that order rotated left by `r` positions, so the same configuration is not always first. The five raw repetition times are retained for every configuration.
5. Reports the median of the measured repetitions only. For an odd count the median is the middle sample after sorting. For an even count it is the mean of the two middle samples. The serial median remains the speedup baseline.

Timers use `std::chrono::steady_clock`. A warm-up or repetition timer starts after the pool exists and stops when that batch of solves returns. It does not include workload generation, thread construction, JSON serialization, CSV writing, or frontend rendering. Total benchmark duration includes generation, every warm-up, pool lifetime, and the measured repetitions. The aggregate warm-up time is the sum of the per-configuration warm-ups.

```text
Speedup(p) = T_serial / T_p
Efficiency(p) = Speedup(p) / p
```

`T_serial` and `T_p` are medians of the measured compute times. Warm-up times are not included. `p` is the actual worker count for that configuration. Serial speedup and efficiency are 1 because the first measured serial run is the reference. The program does not replace a measured time that is slower than serial.

A parallel efficiency slightly above 1 (100%) can come from measurement variation. It is most visible with one worker: that pool does almost the same work as the serial baseline, so the two medians can trade places by a small amount. The value is reported as calculated.

Numerical agreement is the maximum absolute nodal temperature difference, in °C, between the first measured serial repetition and every other measured repetition, and between that same serial repetition and each warm-up. Warm-up timings stay excluded either way. The run passes when that difference is at most `1e-8` °C.

### Hardware

The result records the CPU brand string, physical core count, logical CPU count, RAM bytes, OS name from `uname` or the Windows version API, the compiler from the preprocessor, the CMake build type, and the C++ flags for that build type. A field that cannot be read is omitted (JSON `null`, CSV empty) rather than filled with a guessed value. Optimization flags come from `CMAKE_CXX_FLAGS` and `CMAKE_CXX_FLAGS_<CONFIG>` at configure time. A multi-config generator that leaves `CMAKE_BUILD_TYPE` empty reports those flags as unavailable.

### CSV export

`benchmark_result_csv` writes one row per measured repetition. Warm-up solves do not add rows. Columns include the benchmark id, seed, batch size, mesh size, execution mode, worker count, repetition index, that repetition's time, the median, speedup, efficiency, maximum absolute error, validation, generation time, the aggregate warm-up time, that configuration's excluded warm-up time, total duration, and the hardware fields. The header row uses those names. The same table is returned by `GET /api/performance/export`.

## Known limitations

- Multi-body STL and vertex welding are not implemented. Binary files with extra bytes after the last triangle are rejected.
- Very large meshes still allocate the file, one triangle record per facet, and one JSON response. The browser parses that JSON on the main thread. Edge lines are drawn only for meshes of 100,000 triangles or fewer.
- There is no mesh-to-result registration. The viewers draw the STL and the temperature table separately.
- The temperature table is a simplified nodal CSV, not a full ANSYS result database.
- The sample temperature field is synthetic. It is not a solution of the heat equation and it is not output from the 1D thermal solver.
- STL coordinates have whatever unit the author used. Only the sample block is defined to be in metres.
- Degenerate triangles are preserved. The importer checks structure and finite coordinates; it does not check watertightness or normal consistency.
- Benchmark parallelism is across independent cases. One factorization still runs on one worker. Dense elimination is O(n³) in time and O(n²) in memory, so mesh size and schedule cost are capped.
- Only one benchmark job is kept, in process memory. There is no cancellation and no history of earlier jobs.
- Each measured configuration, including every worker count, is warmed up once. Those times are excluded from the medians. A small efficiency above 100% can still appear from timing variation, particularly for one worker.
- A small batch can be slower in parallel than in serial. The measured speedup is reported as calculated, including values below 1.
- Logical CPU count, physical cores, RAM, or the CPU model can be unavailable in restricted environments. The worker limit is then 1 when the logical count is unknown.
- The worker pool never uses more than four threads, even when the machine has more logical CPUs.

## Engineering notes
The governing equation is `-d/dx(kA dT/dx) = qA`. For a linear two-node element of length `h`, the element stiffness is
`(kA/h) [[1,-1],[-1,1]]` and the consistent element load is `(qAh/2)[1,1]^T`.
The end temperatures are imposed as essential (Dirichlet) boundary conditions.

## Platform note
Development can be done on macOS or Windows. The API and frontend use portable C++17, Node, and a localhost HTTP boundary. They do not call macOS-only frameworks.

The required Windows 11 x64 `Launch.exe` is not produced or verified here. Remaining packaging work, on a Windows 11 x64 machine or a Windows CI runner:

1. Configure with MSVC x64 (Visual Studio 2022 or Ninja) and build `workbench_server.exe` plus the existing tests.
2. Build the frontend with `npm run build`.
3. Package a `Launch.exe` that starts `workbench_server.exe --host 127.0.0.1 --port 8080 --static <frontend dist>` and opens the UI. `--static` already serves the built frontend.
4. Run the C++ tests and both viewers on Windows before treating that package as verified.

A macOS `workbench_server` binary is not a substitute for `Launch.exe`.
