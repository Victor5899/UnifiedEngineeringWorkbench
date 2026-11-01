#include "workbench/learning/backpropagation.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef WORKBENCH_TEST_DIR
#define WORKBENCH_TEST_DIR "tmp_tests"
#endif

namespace {

int g_failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "TEST FAILURE: " << message << '\n';
        ++g_failures;
    }
}

void expect_close(double actual, double expected, double tolerance, const std::string& message) {
    if (!std::isfinite(actual) || !std::isfinite(expected) || std::fabs(actual - expected) > tolerance) {
        std::cerr << std::setprecision(17);
        std::cerr << "TEST FAILURE: " << message << " actual=" << actual << " expected=" << expected
                  << " tolerance=" << tolerance << '\n';
        ++g_failures;
    }
}

std::vector<std::string> split_csv_line(const std::string& line) {
    std::vector<std::string> fields;
    std::string field;
    std::istringstream stream(line);
    while (std::getline(stream, field, ',')) fields.push_back(field);
    return fields;
}

double parse_classic(const std::string& text) {
    std::istringstream stream(text);
    stream.imbue(std::locale::classic());
    double value = 0.0;
    stream >> value;
    if (!stream || !stream.eof()) {
        throw std::runtime_error("Could not parse CSV number: " + text);
    }
    return value;
}

// Independent central difference. Uses only the scalar loss, not the analytical gradient.
double central_difference(const workbench::BackpropParameters& parameters,
                          const workbench::BackpropSample& sample,
                          double epsilon,
                          void (*perturb)(workbench::BackpropParameters&, double)) {
    workbench::BackpropParameters raised = parameters;
    workbench::BackpropParameters lowered = parameters;
    perturb(raised, epsilon);
    perturb(lowered, -epsilon);
    return (workbench::backprop_loss(raised, sample) - workbench::backprop_loss(lowered, sample)) / (2.0 * epsilon);
}

void test_initial_forward_and_analytical() {
    const workbench::BackpropSample sample = workbench::backprop_sample();
    const workbench::BackpropParameters initial = workbench::backprop_initial_parameters();
    expect(sample.x == 1.0 && sample.y == 2.0, "sample is x=1, y=2");
    expect(initial.w == 0.5 && initial.b == 0.0 && initial.v == 1.0 && initial.c == 0.0, "initial parameters");

    const workbench::BackpropForward state = workbench::backprop_forward(initial, sample);
    expect_close(state.h, 0.46211715726000974, 1e-12, "initial hidden activation");
    expect_close(state.y_hat, 0.46211715726000974, 1e-12, "initial prediction");
    expect(state.y_hat == state.h, "initial prediction equals h at v=1, c=0");
    expect_close(state.loss, 1.1825418189970167, 1e-12, "initial loss");
    expect(std::fabs(state.loss - 1.182545) <= 5e-6, "initial loss is approximately 1.182545");
    expect_close(workbench::backprop_loss(initial, sample), state.loss, 0.0, "loss function matches the forward pass");
    expect_close(state.loss, 0.5 * (state.y_hat - sample.y) * (state.y_hat - sample.y), 0.0, "loss identity");

    const workbench::BackpropGradients gradients = workbench::backprop_analytical_gradients(initial, sample);
    const double error = state.y_hat - sample.y;
    const double slope = 1.0 - state.h * state.h;
    expect_close(gradients.dJ_dw, error * initial.v * slope * sample.x, 0.0, "analytical dJ/dw formula");
    expect_close(gradients.dJ_db, error * initial.v * slope, 0.0, "analytical dJ/db formula");
    expect_close(gradients.dJ_dv, error * state.h, 0.0, "analytical dJ/dv formula");
    expect_close(gradients.dJ_dc, error, 0.0, "analytical dJ/dc formula");
    expect_close(gradients.dJ_dw, -1.2094644752400612, 1e-12, "published dJ/dw");
    expect_close(gradients.dJ_db, -1.2094644752400612, 1e-12, "published dJ/db");
    expect_close(gradients.dJ_dv, -0.7106820474859469, 1e-12, "published dJ/dv");
    expect_close(gradients.dJ_dc, -1.5378828427399902, 1e-12, "published dJ/dc");

    const workbench::BackpropParameters updated =
        workbench::backprop_descent_step(initial, gradients, 0.1);
    expect_close(updated.w, initial.w - 0.1 * gradients.dJ_dw, 0.0, "w uses the original dJ/dw");
    expect_close(updated.b, initial.b - 0.1 * gradients.dJ_db, 0.0, "b uses the original dJ/db");
    expect_close(updated.v, initial.v - 0.1 * gradients.dJ_dv, 0.0, "v uses the original dJ/dv");
    expect_close(updated.c, initial.c - 0.1 * gradients.dJ_dc, 0.0, "c uses the original dJ/dc");
    expect(initial.w == 0.5 && initial.b == 0.0 && initial.v == 1.0 && initial.c == 0.0,
           "descent leaves the original parameters unchanged");
    expect_close(updated.w, 0.6209464475240061, 1e-12, "w after one simultaneous update");
    expect_close(updated.b, 0.12094644752400613, 1e-12, "b after one simultaneous update");
    expect_close(updated.v, 1.0710682047485947, 1e-12, "v after one simultaneous update");
    expect_close(updated.c, 0.15378828427399904, 1e-12, "c after one simultaneous update");
}

void test_numerical_gradients() {
    const workbench::BackpropSample sample = workbench::backprop_sample();
    const workbench::BackpropParameters initial = workbench::backprop_initial_parameters();
    constexpr double epsilon = 1e-6;
    constexpr double tolerance = 1e-5;

    const auto perturb_w = [](workbench::BackpropParameters& parameters, double delta) { parameters.w += delta; };
    const auto perturb_b = [](workbench::BackpropParameters& parameters, double delta) { parameters.b += delta; };
    const auto perturb_v = [](workbench::BackpropParameters& parameters, double delta) { parameters.v += delta; };
    const auto perturb_c = [](workbench::BackpropParameters& parameters, double delta) { parameters.c += delta; };

    const double numerical_w = workbench::backprop_numerical_gradient(initial, sample, workbench::BackpropParameter::w, epsilon);
    const double numerical_b = workbench::backprop_numerical_gradient(initial, sample, workbench::BackpropParameter::b, epsilon);
    const double numerical_v = workbench::backprop_numerical_gradient(initial, sample, workbench::BackpropParameter::v, epsilon);
    const double numerical_c = workbench::backprop_numerical_gradient(initial, sample, workbench::BackpropParameter::c, epsilon);
    expect_close(numerical_w, central_difference(initial, sample, epsilon, +perturb_w), 0.0, "numerical dJ/dw");
    expect_close(numerical_b, central_difference(initial, sample, epsilon, +perturb_b), 0.0, "numerical dJ/db");
    expect_close(numerical_v, central_difference(initial, sample, epsilon, +perturb_v), 0.0, "numerical dJ/dv");
    expect_close(numerical_c, central_difference(initial, sample, epsilon, +perturb_c), 0.0, "numerical dJ/dc");

    const workbench::BackpropGradients analytical = workbench::backprop_analytical_gradients(initial, sample);
    expect(std::fabs(analytical.dJ_dw - numerical_w) <= tolerance, "dJ/dw absolute error");
    expect(std::fabs(analytical.dJ_db - numerical_b) <= tolerance, "dJ/db absolute error");
    expect(std::fabs(analytical.dJ_dv - numerical_v) <= tolerance, "dJ/dv absolute error");
    expect(std::fabs(analytical.dJ_dc - numerical_c) <= tolerance, "dJ/dc absolute error");
    expect(initial.w == 0.5 && initial.b == 0.0 && initial.v == 1.0 && initial.c == 0.0,
           "gradient checks leave the original parameters unchanged");

    const std::vector<workbench::BackpropGradientCheck> checks =
        workbench::backprop_compare_gradients(initial, sample, epsilon, tolerance);
    expect(checks.size() == 4, "four gradient checks");
    const workbench::BackpropParameter order[] = {
        workbench::BackpropParameter::w,
        workbench::BackpropParameter::b,
        workbench::BackpropParameter::v,
        workbench::BackpropParameter::c,
    };
    const double analytical_values[] = {analytical.dJ_dw, analytical.dJ_db, analytical.dJ_dv, analytical.dJ_dc};
    const double numerical_values[] = {numerical_w, numerical_b, numerical_v, numerical_c};
    for (int index = 0; index < 4; ++index) {
        expect(checks[static_cast<std::size_t>(index)].parameter == order[index], "gradient check order");
        expect_close(checks[static_cast<std::size_t>(index)].analytical, analytical_values[index], 0.0,
                     "check uses the analytical gradient");
        expect_close(checks[static_cast<std::size_t>(index)].numerical, numerical_values[index], 0.0,
                     "check uses the numerical gradient");
        expect(checks[static_cast<std::size_t>(index)].absolute_error <= tolerance, "check absolute error");
        expect(checks[static_cast<std::size_t>(index)].passed, "check passes");
    }
}

void test_training_and_csv() {
    const workbench::BackpropSample sample = workbench::backprop_sample();
    const workbench::BackpropParameters initial = workbench::backprop_initial_parameters();
    const std::vector<workbench::BackpropRecord> history = workbench::backprop_train(initial, sample, 0.1, 100);
    expect(history.size() == 101, "history has the initial row and 100 updates");
    expect(history.front().step == 0, "history starts at step 0");
    expect(history.back().step == 100, "history ends at step 100");
    expect_close(history.front().parameters.w, 0.5, 0.0, "step 0 weight");
    expect_close(history.front().parameters.b, 0.0, 0.0, "step 0 bias");
    expect_close(history.front().parameters.v, 1.0, 0.0, "step 0 output weight");
    expect_close(history.front().parameters.c, 0.0, 0.0, "step 0 output bias");
    expect_close(history.front().loss, workbench::backprop_loss(initial, sample), 0.0, "step 0 loss");

    workbench::BackpropParameters cursor = initial;
    for (int step = 0; step <= 100; ++step) {
        const workbench::BackpropRecord& row = history[static_cast<std::size_t>(step)];
        expect(row.step == step, "step index " + std::to_string(step));
        expect_close(row.parameters.w, cursor.w, 0.0, "trained w at step " + std::to_string(step));
        expect_close(row.parameters.b, cursor.b, 0.0, "trained b at step " + std::to_string(step));
        expect_close(row.parameters.v, cursor.v, 0.0, "trained v at step " + std::to_string(step));
        expect_close(row.parameters.c, cursor.c, 0.0, "trained c at step " + std::to_string(step));
        expect_close(row.loss, workbench::backprop_loss(cursor, sample), 0.0, "recorded loss at step " + std::to_string(step));
        if (step == 100) break;
        const workbench::BackpropGradients gradients = workbench::backprop_analytical_gradients(cursor, sample);
        cursor = workbench::backprop_descent_step(cursor, gradients, 0.1);
    }

    const workbench::BackpropGradients initial_gradients = workbench::backprop_analytical_gradients(initial, sample);
    const workbench::BackpropParameters first =
        workbench::backprop_descent_step(initial, initial_gradients, 0.1);
    expect_close(history[1].parameters.w, 0.6209464475240061, 1e-12, "history w after the first update");
    expect_close(history[1].parameters.b, 0.12094644752400613, 1e-12, "history b after the first update");
    expect_close(history[1].parameters.v, 1.0710682047485947, 1e-12, "history v after the first update");
    expect_close(history[1].parameters.c, 0.15378828427399904, 1e-12, "history c after the first update");
    expect_close(history[1].parameters.w, first.w, 0.0, "first history update matches one simultaneous step");
    expect(history.back().loss < history.front().loss, "loss decreases overall");

    const std::string csv = workbench::backprop_history_csv(history);
    std::istringstream lines(csv);
    std::string line;
    std::vector<std::string> rows;
    while (std::getline(lines, line)) {
        if (!line.empty()) rows.push_back(line);
    }
    expect(rows.size() == 102, "CSV has a header and 101 data rows");
    expect(!rows.empty() && rows.front() == "step,loss,w,b,v,c", "CSV header");
    if (rows.size() == 102) {
        const std::vector<std::string> first_row = split_csv_line(rows[1]);
        const std::vector<std::string> last_row = split_csv_line(rows.back());
        expect(first_row.size() == 6 && last_row.size() == 6, "CSV row width");
        if (first_row.size() == 6 && last_row.size() == 6) {
            expect(first_row[0] == "0", "CSV step 0");
            expect(last_row[0] == "100", "CSV step 100");
            expect_close(parse_classic(first_row[1]), history.front().loss, 0.0, "CSV initial loss round trip");
            expect_close(parse_classic(last_row[1]), history.back().loss, 0.0, "CSV final loss round trip");
            expect(parse_classic(last_row[1]) < parse_classic(first_row[1]), "CSV loss decreases overall");
        }
    }

    const auto written = std::filesystem::path(WORKBENCH_TEST_DIR) / "backprop" / "history.csv";
    workbench::write_backprop_history_csv(history, written.string());
    std::ifstream file(written);
    expect(static_cast<bool>(file), "CSV file can be read back");
    std::string file_text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    expect(file_text == csv, "written CSV matches the history export");
}

void test_experiment() {
    const workbench::BackpropExperiment experiment = workbench::run_backpropagation();
    expect(experiment.training_steps == 100, "experiment trains for 100 steps");
    expect(experiment.epsilon == 1e-6, "experiment epsilon");
    expect(experiment.gradient_tolerance == 1e-5, "experiment tolerance");
    expect(experiment.learning_rate == 0.1, "experiment learning rate");
    expect(experiment.checks.size() == 4, "experiment checks all four parameters");
    expect(experiment.gradient_check_passed, "experiment gradient verification passes");
    expect(experiment.history.size() == 101, "experiment history length");
    expect_close(experiment.after_one_update.w, experiment.history[1].parameters.w, 0.0, "stored first update weight");
    expect_close(experiment.after_one_update.b, experiment.history[1].parameters.b, 0.0, "stored first update bias");
    expect_close(experiment.after_one_update.v, experiment.history[1].parameters.v, 0.0, "stored first update output weight");
    expect_close(experiment.after_one_update.c, experiment.history[1].parameters.c, 0.0, "stored first update output bias");
    expect(experiment.history.back().loss < experiment.history.front().loss, "experiment loss decreases overall");

    if (g_failures == 0) {
        std::cout << std::setprecision(17);
        std::cout << "Initial loss: " << experiment.history.front().loss << '\n';
        std::cout << "Final loss: " << experiment.history.back().loss << '\n';
        for (const workbench::BackpropGradientCheck& check : experiment.checks) {
            std::cout << workbench::backprop_parameter_name(check.parameter)
                      << " absolute error " << check.absolute_error << '\n';
        }
    }
}

}  // namespace

int main() {
    try {
        test_initial_forward_and_analytical();
        test_numerical_gradients();
        test_training_and_csv();
        test_experiment();
    } catch (const std::exception& error) {
        std::cerr << "TEST FAILURE: " << error.what() << '\n';
        return 1;
    }
    if (g_failures != 0) {
        std::cerr << g_failures << " backpropagation checks failed.\n";
        return 1;
    }
    std::cout << "All backpropagation tests passed.\n";
    return 0;
}
