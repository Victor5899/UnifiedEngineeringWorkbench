# Task F: Backpropagation

This note separates the mathematical specification from the program that evaluates it. The network uses **one training sample** and **one hidden neuron**. It is not a general training library.

The equations in the first sections are the specification. The tables after that are outputs of the C++ program in this repository. Those outputs were not typed in by hand and were not smoothed.

## Specification

One sample:

```text
x = 1
y = 2
```

The model is

```text
z     = w*x + b
h     = tanh(z)
y_hat = v*h + c
e     = y_hat - y
J     = 0.5*e^2
```

Initial parameters and the learning rate:

```text
w = 0.5,  b = 0,  v = 1,  c = 0
eta = 0.1
```

All quantities are real numbers. The program evaluates them in IEEE-754 double precision.

## Chain rule

Differentiate `J = 0.5*e^2` with `e = y_hat - y`. The target does not depend on the parameters, so

```text
dJ/dy_hat = e
```

The output is linear in the hidden activation:

```text
dy_hat/dv = h     =>  dJ/dv = e*h
dy_hat/dc = 1     =>  dJ/dc = e
```

The hidden unit is `h = tanh(z)` and `z = w*x + b`, so

```text
dh/dz = 1 - tanh(z)^2 = 1 - h^2
dz/dw = x
dz/db = 1
dy_hat/dh = v
```

The two remaining derivatives share that path:

```text
dJ/dw = e * v * (1 - h^2) * x
dJ/db = e * v * (1 - h^2)
```

All four derivatives are taken at the same parameter vector. A gradient step then replaces every parameter together:

```text
w <- w - eta * dJ/dw
b <- b - eta * dJ/db
v <- v - eta * dJ/dv
c <- c - eta * dJ/dc
```

No derivative is recomputed between those four replacements.

## Central finite difference

For a parameter `theta`, with the other three held at their original values,

```text
numerical_gradient(theta) = (J(theta + epsilon) - J(theta - epsilon)) / (2*epsilon)
epsilon = 1e-6
```

`J` on the right-hand side is the scalar loss from a forward pass. This check does not call the analytical derivative. The analytical implementation does not call the finite-difference routine. An absolute error at most `1e-5` is a pass:

```text
absolute_error = |analytical_gradient - numerical_gradient|
```

## Training record

Step 0 stores the initial parameters and the initial loss. Each later step does five things, in order:

1. Evaluate the forward pass at the current parameters.
2. Evaluate the four analytical derivatives from that forward pass.
3. Apply one simultaneous gradient step with learning rate `0.1`.
4. Evaluate the loss again at the updated parameters.
5. Store that loss with the updated parameters.

There are 100 updates, so the history has 101 rows: step 0 through step 100.

## Values produced by the program

These numbers come from `./build/workbench backprop`. The forward pass at the initial parameters is

| Quantity | Value |
| --- | --- |
| z | 0.5 |
| h | 0.46211715726000974 |
| y_hat | 0.46211715726000974 |
| e | -1.5378828427399902 |
| J | 1.1825418189970167 |

The loss is the quantity described as approximately 1.182545. The value above is `0.5*e^2` at this `h`, and it differs from 1.182545 by about `3.2e-6`.

The analytical derivatives at those same initial parameters are

| Derivative | Value |
| --- | --- |
| dJ/dw | -1.2094644752400612 |
| dJ/db | -1.2094644752400612 |
| dJ/dv | -0.71068204748594688 |
| dJ/dc | -1.5378828427399902 |

`dJ/dv` printed to 17 significant digits is the same double that the specification writes as `-0.7106820474859469`. The automated tests compare the computed values with the specified digits using an absolute tolerance of `1e-12`.

The central-difference check at the initial parameters, with `epsilon = 1e-6`, produced this table. Every absolute error is below `1e-5`.

| Parameter | Analytical gradient | Numerical gradient | Absolute error | Result |
| --- | --- | --- | --- | --- |
| w | -1.2094644752400612 | -1.2094644751270422 | 1.1301892755000154e-10 | PASS |
| b | -1.2094644752400612 | -1.2094644751270422 | 1.1301892755000154e-10 | PASS |
| v | -0.71068204748594688 | -0.71068204743340857 | 5.2538307038219045e-11 | PASS |
| c | -1.5378828427399902 | -1.5378828426282354 | 1.1175482761416333e-10 | PASS |

One simultaneous update from the initial parameters gives

| Parameter | Value |
| --- | --- |
| w | 0.62094644752400607 |
| b | 0.12094644752400613 |
| v | 1.0710682047485947 |
| c | 0.15378828427399904 |

These agree with the specified one-step values to the digits required by the tests (`1e-12`). The extra trailing digits are the full double print. `w` in the specification is `0.6209464475240061`.

## Training and the loss figure

The history file is `results/backpropagation_training.csv`. It has a header plus 101 data rows. Row `step = 0` is the initial state. Row `step = 100` is the state after 100 updates. The loss column decreases at every step. Selected rows from that file:

| Step | Loss |
| --- | --- |
| 0 | 1.1825418189970167 |
| 1 | 0.68577376504484233 |
| 5 | 0.10132043422396392 |
| 10 | 0.010211015582825693 |
| 20 | 0.00010741333626903479 |
| 50 | 1.2760769996583145e-10 |
| 100 | 1.7026224286601817e-20 |

The parameters after 100 updates are

```text
w = 0.90372928550252696
b = 0.40372928550252768
v = 1.4929479173781868
c = 0.71064397768741472
```

`results/backpropagation_loss.png` plots loss against training step on a linear axis. The drop is steep for the first 20 steps. From there the line lies on the axis because a loss near `1e-10`, and later near `1e-20`, is invisible next to a vertical scale set by the initial loss. The CSV still falls through that tail. The figure is not smoothed, and the plotting script does not change the CSV.

A single sample with a free output bias can be fit by this model, so a loss that reaches roundoff is expected here. The curve says nothing about performance on any other input.

## What the program does

The mathematics above is implemented in `include/workbench/learning/backpropagation.hpp` and `src/core/learning/backpropagation.cpp`.

| Piece | Role |
| --- | --- |
| `backprop_forward` | `z`, `h`, `y_hat`, the residual, and the loss |
| `backprop_loss` | The scalar `J` of one forward pass |
| `backprop_analytical_gradients` | The four analytical derivatives from one forward pass |
| `backprop_numerical_gradient` | One central difference, using `backprop_loss` only |
| `backprop_descent_step` | One simultaneous update from a gradient that was already computed |
| `backprop_train` | Step 0 and the following updates |
| `run_backpropagation` | The fixed experiment: the initial sample, the gradient check, and 100 steps |

`workbench backprop` prints the forward pass, the analytical derivatives, the comparison table, and the final loss. It writes the CSV and returns status 0 only when every gradient check passes.

`GET /api/learning/backpropagation` returns the same experiment as JSON. `GET /api/learning/backpropagation/csv` returns the history. The Backpropagation page draws those values. It does not run the chain rule in TypeScript.

No automatic-differentiation library is used.

## Build, test, and redraw the figure

From the repository root:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/workbench backprop --output results/backpropagation_training.csv
python3 scripts/plot_backpropagation_loss.py
```

The plot command reads the CSV and writes `results/backpropagation_loss.png`. It needs Matplotlib (`python3 -m pip install matplotlib` if the import fails). Python is not required to build or run the C++ experiment. Another CSV path and another PNG path can be passed as the two optional arguments.

`results/*.csv` is ignored by git. Generate the CSV with the command above before redrawing the figure.

Count the history rows, including the header line:

```bash
wc -l results/backpropagation_training.csv
```

The file written by the command above has 102 lines: one header and steps 0 through 100.
