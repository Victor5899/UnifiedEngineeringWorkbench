#include "workbench/learning/backpropagation.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace workbench {
namespace {

void require_finite(double value, const char* name) {
    if (!std::isfinite(value)) {
        throw std::invalid_argument(std::string(name) + " must be finite.");
    }
}

void require_sample(const BackpropSample& sample) {
    require_finite(sample.x, "Input");
    require_finite(sample.y, "Target");
}

void require_parameters(const BackpropParameters& parameters) {
    require_finite(parameters.w, "w");
    require_finite(parameters.b, "b");
    require_finite(parameters.v, "v");
    require_finite(parameters.c, "c");
}

double& parameter_ref(BackpropParameters& parameters, BackpropParameter parameter) {
    switch (parameter) {
    case BackpropParameter::w: return parameters.w;
    case BackpropParameter::b: return parameters.b;
    case BackpropParameter::v: return parameters.v;
    case BackpropParameter::c: return parameters.c;
    }
    throw std::invalid_argument("Unknown backpropagation parameter.");
}

double gradient_component(const BackpropGradients& gradients, BackpropParameter parameter) {
    switch (parameter) {
    case BackpropParameter::w: return gradients.dJ_dw;
    case BackpropParameter::b: return gradients.dJ_db;
    case BackpropParameter::v: return gradients.dJ_dv;
    case BackpropParameter::c: return gradients.dJ_dc;
    }
    throw std::invalid_argument("Unknown backpropagation parameter.");
}

// J = 0.5*e^2, so dJ/de = e. The one-half is part of the loss, not a later rescale.
double squared_error_loss(double prediction, double target) {
    const double error = prediction - target;
    return 0.5 * error * error;
}

// Partial derivatives from one already computed forward state.
// dJ/dv = e*h, dJ/dc = e, dJ/dw = e*v*(1-h*h)*x, dJ/db = e*v*(1-h*h).
BackpropGradients gradients_from_forward(const BackpropParameters& parameters,
                                         const BackpropForward& state,
                                         const BackpropSample& sample) {
    const double hidden_derivative = 1.0 - state.h * state.h;
    const double output_through_hidden = state.error * parameters.v * hidden_derivative;
    BackpropGradients gradients;
    gradients.dJ_dw = output_through_hidden * sample.x;
    gradients.dJ_db = output_through_hidden;
    gradients.dJ_dv = state.error * state.h;
    gradients.dJ_dc = state.error;
    return gradients;
}

const BackpropParameter kParameterOrder[] = {
    BackpropParameter::w,
    BackpropParameter::b,
    BackpropParameter::v,
    BackpropParameter::c,
};

}  // namespace

BackpropSample backprop_sample() {
    return BackpropSample{};
}

BackpropParameters backprop_initial_parameters() {
    return BackpropParameters{};
}

BackpropForward backprop_forward(const BackpropParameters& parameters, const BackpropSample& sample) {
    require_parameters(parameters);
    require_sample(sample);
    BackpropForward state;
    state.z = parameters.w * sample.x + parameters.b;
    state.h = std::tanh(state.z);
    state.y_hat = parameters.v * state.h + parameters.c;
    state.error = state.y_hat - sample.y;
    state.loss = squared_error_loss(state.y_hat, sample.y);
    return state;
}

double backprop_loss(const BackpropParameters& parameters, const BackpropSample& sample) {
    return backprop_forward(parameters, sample).loss;
}

BackpropGradients backprop_analytical_gradients(const BackpropParameters& parameters,
                                                const BackpropSample& sample) {
    const BackpropForward state = backprop_forward(parameters, sample);
    return gradients_from_forward(parameters, state, sample);
}

double backprop_numerical_gradient(const BackpropParameters& parameters,
                                   const BackpropSample& sample,
                                   BackpropParameter parameter,
                                   double epsilon) {
    require_finite(epsilon, "Finite-difference epsilon");
    if (!(epsilon > 0.0)) {
        throw std::invalid_argument("Finite-difference epsilon must be positive.");
    }
    // Copy the original parameters, then move only the selected component.
    BackpropParameters raised = parameters;
    BackpropParameters lowered = parameters;
    parameter_ref(raised, parameter) += epsilon;
    parameter_ref(lowered, parameter) -= epsilon;
    const double loss_raised = backprop_loss(raised, sample);
    const double loss_lowered = backprop_loss(lowered, sample);
    return (loss_raised - loss_lowered) / (2.0 * epsilon);
}

std::vector<BackpropGradientCheck> backprop_compare_gradients(const BackpropParameters& parameters,
                                                              const BackpropSample& sample,
                                                              double epsilon,
                                                              double tolerance) {
    require_finite(tolerance, "Gradient tolerance");
    if (tolerance < 0.0) {
        throw std::invalid_argument("Gradient tolerance must be zero or positive.");
    }
    const BackpropGradients analytical = backprop_analytical_gradients(parameters, sample);
    std::vector<BackpropGradientCheck> checks;
    checks.reserve(4);
    for (const BackpropParameter parameter : kParameterOrder) {
        BackpropGradientCheck check;
        check.parameter = parameter;
        check.analytical = gradient_component(analytical, parameter);
        check.numerical = backprop_numerical_gradient(parameters, sample, parameter, epsilon);
        check.absolute_error = std::fabs(check.analytical - check.numerical);
        check.passed = std::isfinite(check.absolute_error) && check.absolute_error <= tolerance;
        checks.push_back(check);
    }
    return checks;
}

BackpropParameters backprop_descent_step(const BackpropParameters& parameters,
                                         const BackpropGradients& gradients,
                                         double learning_rate) {
    require_parameters(parameters);
    require_finite(learning_rate, "Learning rate");
    require_finite(gradients.dJ_dw, "dJ/dw");
    require_finite(gradients.dJ_db, "dJ/db");
    require_finite(gradients.dJ_dv, "dJ/dv");
    require_finite(gradients.dJ_dc, "dJ/dc");
    BackpropParameters updated = parameters;
    updated.w -= learning_rate * gradients.dJ_dw;
    updated.b -= learning_rate * gradients.dJ_db;
    updated.v -= learning_rate * gradients.dJ_dv;
    updated.c -= learning_rate * gradients.dJ_dc;
    return updated;
}

std::vector<BackpropRecord> backprop_train(const BackpropParameters& initial,
                                           const BackpropSample& sample,
                                           double learning_rate,
                                           int steps) {
    if (steps < 0) {
        throw std::invalid_argument("Training step count must be zero or positive.");
    }
    require_finite(learning_rate, "Learning rate");
    std::vector<BackpropRecord> history;
    history.reserve(static_cast<std::size_t>(steps) + 1);

    BackpropParameters current = initial;
    history.push_back(BackpropRecord{0, backprop_loss(current, sample), current});
    for (int step = 1; step <= steps; ++step) {
        const BackpropForward state = backprop_forward(current, sample);
        const BackpropGradients gradients = gradients_from_forward(current, state, sample);
        current = backprop_descent_step(current, gradients, learning_rate);
        history.push_back(BackpropRecord{step, backprop_loss(current, sample), current});
    }
    return history;
}

BackpropExperiment run_backpropagation() {
    BackpropExperiment experiment;
    experiment.sample = backprop_sample();
    experiment.initial_parameters = backprop_initial_parameters();
    experiment.learning_rate = kBackpropLearningRate;
    experiment.epsilon = kBackpropEpsilon;
    experiment.gradient_tolerance = kBackpropGradientTolerance;
    experiment.training_steps = kBackpropSteps;
    experiment.initial_forward = backprop_forward(experiment.initial_parameters, experiment.sample);
    experiment.analytical = backprop_analytical_gradients(experiment.initial_parameters, experiment.sample);
    experiment.checks = backprop_compare_gradients(
        experiment.initial_parameters,
        experiment.sample,
        experiment.epsilon,
        experiment.gradient_tolerance);
    experiment.gradient_check_passed = experiment.checks.size() == 4;
    for (const BackpropGradientCheck& check : experiment.checks) {
        if (!check.passed) experiment.gradient_check_passed = false;
    }
    experiment.after_one_update = backprop_descent_step(
        experiment.initial_parameters,
        experiment.analytical,
        experiment.learning_rate);
    experiment.history = backprop_train(
        experiment.initial_parameters,
        experiment.sample,
        experiment.learning_rate,
        experiment.training_steps);
    if (static_cast<int>(experiment.history.size()) != experiment.training_steps + 1) {
        throw std::logic_error("Backpropagation history does not contain the initial row and every update.");
    }
    return experiment;
}

const char* backprop_parameter_name(BackpropParameter parameter) {
    switch (parameter) {
    case BackpropParameter::w: return "w";
    case BackpropParameter::b: return "b";
    case BackpropParameter::v: return "v";
    case BackpropParameter::c: return "c";
    }
    return "unknown";
}

std::string backprop_history_csv(const std::vector<BackpropRecord>& history) {
    std::ostringstream csv;
    csv.imbue(std::locale::classic());
    csv << std::setprecision(std::numeric_limits<double>::max_digits10);
    csv << "step,loss,w,b,v,c\n";
    for (const BackpropRecord& row : history) {
        csv << row.step << ','
            << row.loss << ','
            << row.parameters.w << ','
            << row.parameters.b << ','
            << row.parameters.v << ','
            << row.parameters.c << '\n';
    }
    return csv.str();
}

void write_backprop_history_csv(const std::vector<BackpropRecord>& history, const std::string& path) {
    if (path.empty()) {
        throw std::invalid_argument("Backpropagation CSV path must not be empty.");
    }
    const std::filesystem::path destination(path);
    const std::filesystem::path parent = destination.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
    std::ofstream file(destination);
    if (!file) {
        throw std::runtime_error("Cannot open backpropagation CSV: " + path);
    }
    file << backprop_history_csv(history);
    if (!file) {
        throw std::runtime_error("Cannot write backpropagation CSV: " + path);
    }
}

}  // namespace workbench
