#pragma once

#include <string>
#include <vector>

namespace workbench {

// Task F network: one training sample and one tanh hidden neuron.
// h = tanh(w*x + b), y_hat = v*h + c, J = 0.5*(y_hat - y)^2.
// Analytical gradients and central differences are separate calculations.
// Training updates use the analytical gradients only.

inline constexpr double kBackpropInput = 1.0;
inline constexpr double kBackpropTarget = 2.0;
inline constexpr double kBackpropInitialWeight = 0.5;
inline constexpr double kBackpropInitialBias = 0.0;
inline constexpr double kBackpropInitialOutputWeight = 1.0;
inline constexpr double kBackpropInitialOutputBias = 0.0;
inline constexpr double kBackpropLearningRate = 0.1;
inline constexpr double kBackpropEpsilon = 1e-6;
inline constexpr double kBackpropGradientTolerance = 1e-5;
inline constexpr int kBackpropSteps = 100;

struct BackpropSample {
    double x = kBackpropInput;
    double y = kBackpropTarget;
};

struct BackpropParameters {
    double w = kBackpropInitialWeight;
    double b = kBackpropInitialBias;
    double v = kBackpropInitialOutputWeight;
    double c = kBackpropInitialOutputBias;
};

struct BackpropForward {
    double z = 0.0;
    double h = 0.0;
    double y_hat = 0.0;
    double error = 0.0;
    double loss = 0.0;
};

struct BackpropGradients {
    double dJ_dw = 0.0;
    double dJ_db = 0.0;
    double dJ_dv = 0.0;
    double dJ_dc = 0.0;
};

enum class BackpropParameter { w, b, v, c };

struct BackpropGradientCheck {
    BackpropParameter parameter = BackpropParameter::w;
    double analytical = 0.0;
    double numerical = 0.0;
    double absolute_error = 0.0;
    bool passed = false;
};

struct BackpropRecord {
    int step = 0;
    double loss = 0.0;
    BackpropParameters parameters;
};

struct BackpropExperiment {
    BackpropSample sample;
    BackpropParameters initial_parameters;
    double learning_rate = kBackpropLearningRate;
    double epsilon = kBackpropEpsilon;
    double gradient_tolerance = kBackpropGradientTolerance;
    int training_steps = kBackpropSteps;
    BackpropForward initial_forward;
    BackpropGradients analytical;
    std::vector<BackpropGradientCheck> checks;
    bool gradient_check_passed = false;
    BackpropParameters after_one_update;
    std::vector<BackpropRecord> history;
};

BackpropSample backprop_sample();
BackpropParameters backprop_initial_parameters();

// Squared-error loss of one forward pass. J = 0.5*(y_hat - y)^2.
double backprop_loss(const BackpropParameters& parameters, const BackpropSample& sample);

// Hidden activation, prediction, residual, and loss at the given parameters.
BackpropForward backprop_forward(const BackpropParameters& parameters, const BackpropSample& sample);

// All four analytical partial derivatives at one unchanged parameter vector.
BackpropGradients backprop_analytical_gradients(const BackpropParameters& parameters,
                                                const BackpropSample& sample);

// Central difference of J for one parameter. The other three parameters stay fixed.
// Loss is recomputed by forward evaluation. Analytical gradients are not used.
double backprop_numerical_gradient(const BackpropParameters& parameters,
                                   const BackpropSample& sample,
                                   BackpropParameter parameter,
                                   double epsilon);

// Analytical and numerical gradients at the same original parameters.
std::vector<BackpropGradientCheck> backprop_compare_gradients(const BackpropParameters& parameters,
                                                              const BackpropSample& sample,
                                                              double epsilon,
                                                              double tolerance);

// Simultaneous gradient step. Every new value uses the supplied pre-update gradient.
BackpropParameters backprop_descent_step(const BackpropParameters& parameters,
                                         const BackpropGradients& gradients,
                                         double learning_rate);

// history[0] is the initial loss and parameters. history[k] follows k simultaneous updates.
std::vector<BackpropRecord> backprop_train(const BackpropParameters& initial,
                                           const BackpropSample& sample,
                                           double learning_rate,
                                           int steps);

// The fixed Task F experiment: the initial sample, one gradient check, and 100 updates.
BackpropExperiment run_backpropagation();

const char* backprop_parameter_name(BackpropParameter parameter);

// Header step,loss,w,b,v,c and one data row per history record. Classic locale, full double precision.
std::string backprop_history_csv(const std::vector<BackpropRecord>& history);

// Creates the parent directory when the path has one.
void write_backprop_history_csv(const std::vector<BackpropRecord>& history, const std::string& path);

}
