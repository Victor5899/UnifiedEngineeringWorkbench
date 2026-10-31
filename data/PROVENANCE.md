# Dataset provenance

These files are **SYNTHETIC** development fixtures for the Unified Engineering Workbench import engine. They were written by hand for this repository. They are not measurements, they are not solver output, and they are **not an ANSYS export**. Do not present them as results from ANSYS, or from any other analysis package.

## data/geometry/sample_geometry.stl

- Origin: hand-authored ASCII STL.
- Format: one ASCII solid named `rectangular_block`. Binary STL is not used.
- Geometry: rectangular block, 0.10 m by 0.05 m by 0.02 m, with a corner at the origin. That is 100 mm by 50 mm by 20 mm.
- Units: the STL format does not store a unit. Coordinates in this file are metres.
- Mesh: 12 triangles (two per face) and 36 vertex records. Corners are repeated because ASCII STL does not share vertex indices. Normals point outward.
- Faces, in file order: z = 0, z = 0.02, y = 0, y = 0.05, x = 0, x = 0.10.

## data/thermal/sample_temperature.csv

- Origin: hand-authored nodal table. **SYNTHETIC**. Not an ANSYS `.csv`, `.rst`, `.rth`, or `.cdb` export.
- Schema: `node_id,x_m,y_m,z_m,temperature_C`
- Units: `node_id` is a dimensionless integer. `x_m`, `y_m`, and `z_m` are metres. `temperature_C` is degrees Celsius.
- Layout: 27 nodes on a 3 by 3 by 3 lattice covering the same corner ranges as the block. x is fastest, then y, then z. Node IDs are 1 through 27 in that order.
- Coordinates: x = 0, 0.05, 0.10 m; y = 0, 0.025, 0.05 m; z = 0, 0.01, 0.02 m.
- Temperatures were precomputed from `T = 25 + 400*x + 100*z` and written as numbers. y does not change the temperature. The resulting range is 25 C to 67 C, and the arithmetic mean is 46 C.
- The table has no element connectivity and is not registered to the STL triangles.

## Limitations

- The temperature field is an invented linear function, not a solution of the heat equation and not a check of the 1D thermal solver.
- The lattice is too coarse to represent a real component.
- No loads, materials, boundary-condition records, or ANSYS headers are included.
- The STL block and the CSV lattice use the same coordinate ranges so a later viewer can draw them together. This repository does not match nodes to triangles.
