#include "learning_api.hpp"

#include "workbench/learning/backpropagation.hpp"

#include <iostream>
#include <nlohmann/json.hpp>
#include <string>

namespace {

void apply_cors(httplib::Response& response) {
    response.set_header("Access-Control-Allow-Origin", "*");
    response.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    response.set_header("Access-Control-Allow-Headers", "Content-Type, Accept");
    response.set_header("Access-Control-Max-Age", "600");
}

void send_json(httplib::Response& response, int status, const nlohmann::json& body) {
    response.status = status;
    response.set_content(body.dump(), "application/json");
    response.set_header("Cache-Control", "no-store");
    apply_cors(response);
}

nlohmann::json parameters_json(const workbench::BackpropParameters& parameters) {
    return {
        {"w", parameters.w},
        {"b", parameters.b},
        {"v", parameters.v},
        {"c", parameters.c},
    };
}

nlohmann::json experiment_json(const workbench::BackpropExperiment& experiment) {
    nlohmann::json checks = nlohmann::json::array();
    for (const workbench::BackpropGradientCheck& check : experiment.checks) {
        checks.push_back({
            {"parameter", workbench::backprop_parameter_name(check.parameter)},
            {"analytical_gradient", check.analytical},
            {"numerical_gradient", check.numerical},
            {"absolute_error", check.absolute_error},
            {"passed", check.passed},
        });
    }
    nlohmann::json history = nlohmann::json::array();
    for (const workbench::BackpropRecord& row : experiment.history) {
        history.push_back({
            {"step", row.step},
            {"loss", row.loss},
            {"w", row.parameters.w},
            {"b", row.parameters.b},
            {"v", row.parameters.v},
            {"c", row.parameters.c},
        });
    }
    const workbench::BackpropRecord& final_row = experiment.history.back();
    return {
        {"training_samples", 1},
        {"hidden_neurons", 1},
        {"activation", "tanh"},
        {"loss_definition", "0.5*(y_hat-y)^2"},
        {"sample", {{"x", experiment.sample.x}, {"y", experiment.sample.y}}},
        {"learning_rate", experiment.learning_rate},
        {"epsilon", experiment.epsilon},
        {"gradient_tolerance", experiment.gradient_tolerance},
        {"training_steps", experiment.training_steps},
        {"initial_parameters", parameters_json(experiment.initial_parameters)},
        {"initial_forward",
         {{"z", experiment.initial_forward.z},
          {"h", experiment.initial_forward.h},
          {"y_hat", experiment.initial_forward.y_hat},
          {"error", experiment.initial_forward.error},
          {"loss", experiment.initial_forward.loss}}},
        {"analytical_gradients",
         {{"dJ_dw", experiment.analytical.dJ_dw},
          {"dJ_db", experiment.analytical.dJ_db},
          {"dJ_dv", experiment.analytical.dJ_dv},
          {"dJ_dc", experiment.analytical.dJ_dc}}},
        {"gradient_checks", std::move(checks)},
        {"gradient_check_passed", experiment.gradient_check_passed},
        {"parameters_after_one_update", parameters_json(experiment.after_one_update)},
        {"initial_loss", experiment.history.front().loss},
        {"final_loss", final_row.loss},
        {"final_parameters", parameters_json(final_row.parameters)},
        {"history", std::move(history)},
    };
}

}  // namespace

namespace workbench {

void register_learning_routes(httplib::Server& server) {
    server.Get("/api/learning/backpropagation", [](const httplib::Request&, httplib::Response& response) {
        const BackpropExperiment experiment = run_backpropagation();
        send_json(response, 200, experiment_json(experiment));
        std::cout << "GET /api/learning/backpropagation 200\n";
    });

    server.Get("/api/learning/backpropagation/csv", [](const httplib::Request&, httplib::Response& response) {
        const BackpropExperiment experiment = run_backpropagation();
        response.status = 200;
        response.set_content(backprop_history_csv(experiment.history), "text/csv; charset=utf-8");
        response.set_header("Cache-Control", "no-store");
        response.set_header("Content-Disposition", "attachment; filename=\"backpropagation_training.csv\"");
        apply_cors(response);
        std::cout << "GET /api/learning/backpropagation/csv 200\n";
    });
}

}  // namespace workbench
