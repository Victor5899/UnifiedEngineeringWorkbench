# Unified Engineering Workbench

Initial implementation scaffold for the PASS Science Works common assignment.

## Current status
- [x] C++17 CMake project structure
- [x] 1D steady-state thermal FEM solver (two-node linear elements)
- [x] Analytical solution and element-midpoint error
- [x] CSV export
- [x] Initial positive/negative/convergence tests
- [x] ASCII STL import
- [x] Nodal temperature CSV import
- [x] Local HTTP API over the C++ importers
- [x] STL and temperature visualization
- [ ] Serial/parallel batch benchmark
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

The upload limit defaults to 64 MiB, matching the importer limits. The client-supplied filename is a display label only. The server stores the bytes under a generated name in a temporary directory and deletes that file before the response is returned.

A successful STL response contains `filename`, `solid_name`, `triangle_count`, `vertex_count`, `triangles` (`normal` and `vertices` as `[x, y, z]`), `bounding_box.min`, `bounding_box.max`, `dimensions`, and `coordinate_units` (`file`).

A successful temperature response contains `filename`, `node_count`, `nodes` (`node_id`, `x_m`, `y_m`, `z_m`, `temperature_C`), `minimum_temperature_C`, `maximum_temperature_C`, `average_temperature_C`, `bounding_box`, `dimensions`, `coordinate_units` (`m`), and `temperature_units` (`C`).

Failures return JSON `{ "error": "..." }` with status 400 for a rejected file, 413 for an oversized upload, and 500 for an unexpected server failure. Parser messages do not include the temporary path.

Geometry and Thermal Analysis are implemented. Performance and Hardware are labelled placeholders for later phases. The point cloud is not registered to the STL.

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
- Binary STL is rejected, including a binary file whose 80-byte header begins with `solid`.
- Text that does not start with `solid` is rejected.
- Only one solid is accepted. Tokens after the `endsolid` line are rejected.
- A non-regular path or a missing file is rejected.

Default limits (`StlImportLimits`):

- Maximum file size: 64 MiB (67,108,864 bytes).
- Maximum triangle count: 2,000,000.

Both limits are arguments of `parse_ascii_stl`. A limit of zero is rejected before the file is read.

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

## Known limitations

- Binary STL, multi-body STL, and vertex welding are not implemented.
- There is no mesh-to-result registration. The viewers draw the STL and the temperature table separately.
- The temperature table is a simplified nodal CSV, not a full ANSYS result database.
- The sample temperature field is synthetic. It is not a solution of the heat equation and it is not output from the 1D thermal solver.
- STL coordinates have whatever unit the author used. Only the sample block is defined to be in metres.
- Degenerate triangles are preserved. The importer checks structure and finite coordinates; it does not check watertightness or normal consistency.

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
