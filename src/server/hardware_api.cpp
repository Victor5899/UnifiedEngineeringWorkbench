#include "hardware_api.hpp"

#include "workbench/hardware/device_manager.hpp"

#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>

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

void send_error(httplib::Response& response, int status, const nlohmann::json& body) {
    send_json(response, status, body);
}

int http_status(workbench::HardwareError error) {
    switch (error) {
    case workbench::HardwareError::ok:
        return 200;
    case workbench::HardwareError::unknown_device:
        return 404;
    case workbench::HardwareError::already_connected:
    case workbench::HardwareError::not_connected:
        return 409;
    case workbench::HardwareError::simulated_timeout:
        return 504;
    case workbench::HardwareError::unsupported_operation:
    case workbench::HardwareError::invalid_request:
        return 400;
    }
    return 400;
}

nlohmann::json simulation_envelope() {
    return {
        {"simulated", true},
        {"physical_hardware_connected", false},
    };
}

nlohmann::json capability_json(const workbench::Capability& capability) {
    return {{"id", capability.id}, {"summary", capability.summary}};
}

nlohmann::json device_json(const workbench::DeviceInfo& info) {
    nlohmann::json capabilities = nlohmann::json::array();
    for (const workbench::Capability& capability : info.capabilities) {
        capabilities.push_back(capability_json(capability));
    }
    nlohmann::json body = simulation_envelope();
    body.update({
        {"id", info.id},
        {"device_type", workbench::to_string(info.type)},
        {"display_name", info.display_name},
        {"reference_profile", info.reference_profile},
        {"manufacturer_reference", info.manufacturer_reference},
        {"connection_state", workbench::to_string(info.connection)},
        {"available", info.available},
        {"capabilities", std::move(capabilities)},
    });
    return body;
}

nlohmann::json error_json(const workbench::HardwareResult& result, const std::string& device_id) {
    nlohmann::json body = simulation_envelope();
    body.update({
        {"error", result.message},
        {"code", workbench::to_string(result.error)},
    });
    if (!device_id.empty() && result.error != workbench::HardwareError::unknown_device &&
        result.error != workbench::HardwareError::invalid_request) {
        body["device_id"] = device_id;
    }
    if (result.connection) {
        body["connection_state"] = workbench::to_string(*result.connection);
    }
    return body;
}

void log_route(const char* method, const char* route, int status) {
    std::cout << method << ' ' << route << ' ' << status << '\n';
}

workbench::HardwareResult read_device_id(const nlohmann::json& body, std::string& device_id) {
    if (!body.is_object()) {
        return workbench::HardwareResult{
            workbench::HardwareError::invalid_request,
            "Request body must be a JSON object.",
            std::nullopt};
    }
    for (auto field = body.begin(); field != body.end(); ++field) {
        if (field.key() != "device_id") {
            return workbench::HardwareResult{
                workbench::HardwareError::invalid_request,
                "Unrecognized field \"" + field.key() + "\".",
                std::nullopt};
        }
    }
    if (!body.contains("device_id") || !body.at("device_id").is_string()) {
        return workbench::HardwareResult{
            workbench::HardwareError::invalid_request,
            "device_id must be a string.",
            std::nullopt};
    }
    device_id = body.at("device_id").get<std::string>();
    if (device_id.empty()) {
        return workbench::HardwareResult{
            workbench::HardwareError::invalid_request,
            "A device identifier is required.",
            std::nullopt};
    }
    return workbench::HardwareResult{workbench::HardwareError::ok, "", std::nullopt};
}

workbench::HardwareResult read_failure(const nlohmann::json& body, std::string& device_id, std::string& failure_id) {
    if (!body.is_object()) {
        return workbench::HardwareResult{
            workbench::HardwareError::invalid_request,
            "Request body must be a JSON object.",
            std::nullopt};
    }
    for (auto field = body.begin(); field != body.end(); ++field) {
        if (field.key() != "device_id" && field.key() != "failure") {
            return workbench::HardwareResult{
                workbench::HardwareError::invalid_request,
                "Unrecognized field \"" + field.key() + "\".",
                std::nullopt};
        }
    }
    if (!body.contains("device_id") || !body.at("device_id").is_string()) {
        return workbench::HardwareResult{
            workbench::HardwareError::invalid_request,
            "device_id must be a string.",
            std::nullopt};
    }
    if (!body.contains("failure") || !body.at("failure").is_string()) {
        return workbench::HardwareResult{
            workbench::HardwareError::invalid_request,
            "failure must be a string.",
            std::nullopt};
    }
    device_id = body.at("device_id").get<std::string>();
    failure_id = body.at("failure").get<std::string>();
    if (device_id.empty()) {
        return workbench::HardwareResult{
            workbench::HardwareError::invalid_request,
            "A device identifier is required.",
            std::nullopt};
    }
    if (failure_id.empty()) {
        return workbench::HardwareResult{
            workbench::HardwareError::invalid_request,
            "A simulated failure identifier is required.",
            std::nullopt};
    }
    return workbench::HardwareResult{workbench::HardwareError::ok, "", std::nullopt};
}

bool parse_object(const httplib::Request& request, nlohmann::json& parsed, workbench::HardwareResult& error) {
    try {
        parsed = nlohmann::json::parse(request.body);
    } catch (const nlohmann::json::exception&) {
        error = workbench::HardwareResult{
            workbench::HardwareError::invalid_request,
            "Request body must be a JSON object.",
            std::nullopt};
        return false;
    }
    if (!parsed.is_object()) {
        error = workbench::HardwareResult{
            workbench::HardwareError::invalid_request,
            "Request body must be a JSON object.",
            std::nullopt};
        return false;
    }
    return true;
}

}  // namespace

namespace workbench {

void register_hardware_routes(httplib::Server& server) {
    const auto manager = std::make_shared<DeviceManager>();

    server.Get("/api/hardware/devices", [manager](const httplib::Request&, httplib::Response& response) {
        nlohmann::json devices = nlohmann::json::array();
        for (const DeviceInfo& info : manager->discover()) {
            devices.push_back(device_json(info));
        }
        nlohmann::json body = simulation_envelope();
        body["devices"] = std::move(devices);
        send_json(response, 200, body);
        log_route("GET", "/api/hardware/devices", 200);
    });

    server.Get(R"(/api/hardware/devices/([^/]+))", [manager](const httplib::Request& request, httplib::Response& response) {
        const std::string id = request.matches.size() > 1 ? request.matches[1].str() : std::string();
        DeviceInfo info;
        const HardwareResult result = manager->describe(id, info);
        if (!result.ok()) {
            const int status = http_status(result.error);
            send_error(response, status, error_json(result, id));
            log_route("GET", "/api/hardware/devices/{id}", status);
            return;
        }
        send_json(response, 200, device_json(info));
        log_route("GET", "/api/hardware/devices/{id}", 200);
    });

    server.Get("/api/hardware/capabilities", [manager](const httplib::Request&, httplib::Response& response) {
        nlohmann::json devices = nlohmann::json::array();
        for (const CapabilityReport& report : manager->capabilities()) {
            nlohmann::json capabilities = nlohmann::json::array();
            for (const Capability& capability : report.capabilities) {
                capabilities.push_back(capability_json(capability));
            }
            devices.push_back({
                {"id", report.id},
                {"device_type", to_string(report.type)},
                {"display_name", report.display_name},
                {"capabilities", std::move(capabilities)},
            });
        }
        nlohmann::json body = simulation_envelope();
        body["devices"] = std::move(devices);
        send_json(response, 200, body);
        log_route("GET", "/api/hardware/capabilities", 200);
    });

    server.Post("/api/hardware/connect", [manager](const httplib::Request& request, httplib::Response& response) {
        nlohmann::json parsed;
        HardwareResult parse_error;
        if (!parse_object(request, parsed, parse_error)) {
            const int status = http_status(parse_error.error);
            send_error(response, status, error_json(parse_error, ""));
            log_route("POST", "/api/hardware/connect", status);
            return;
        }
        std::string device_id;
        const HardwareResult parsed_id = read_device_id(parsed, device_id);
        if (!parsed_id.ok()) {
            const int status = http_status(parsed_id.error);
            send_error(response, status, error_json(parsed_id, ""));
            log_route("POST", "/api/hardware/connect", status);
            return;
        }
        const HardwareResult result = manager->connect(device_id);
        if (!result.ok()) {
            const int status = http_status(result.error);
            send_error(response, status, error_json(result, device_id));
            log_route("POST", "/api/hardware/connect", status);
            return;
        }
        nlohmann::json body = simulation_envelope();
        body.update({
            {"device_id", device_id},
            {"connection_state", result.connection ? to_string(*result.connection) : "connected"},
            {"message", result.message},
        });
        send_json(response, 200, body);
        log_route("POST", "/api/hardware/connect", 200);
    });

    server.Post("/api/hardware/disconnect", [manager](const httplib::Request& request, httplib::Response& response) {
        nlohmann::json parsed;
        HardwareResult parse_error;
        if (!parse_object(request, parsed, parse_error)) {
            const int status = http_status(parse_error.error);
            send_error(response, status, error_json(parse_error, ""));
            log_route("POST", "/api/hardware/disconnect", status);
            return;
        }
        std::string device_id;
        const HardwareResult parsed_id = read_device_id(parsed, device_id);
        if (!parsed_id.ok()) {
            const int status = http_status(parsed_id.error);
            send_error(response, status, error_json(parsed_id, ""));
            log_route("POST", "/api/hardware/disconnect", status);
            return;
        }
        const HardwareResult result = manager->disconnect(device_id);
        if (!result.ok()) {
            const int status = http_status(result.error);
            send_error(response, status, error_json(result, device_id));
            log_route("POST", "/api/hardware/disconnect", status);
            return;
        }
        nlohmann::json body = simulation_envelope();
        body.update({
            {"device_id", device_id},
            {"connection_state", result.connection ? to_string(*result.connection) : "disconnected"},
            {"message", result.message},
        });
        send_json(response, 200, body);
        log_route("POST", "/api/hardware/disconnect", 200);
    });

    server.Get("/api/hardware/telemetry", [manager](const httplib::Request& request, httplib::Response& response) {
        if (!request.has_param("device_id")) {
            const HardwareResult missing{
                HardwareError::invalid_request,
                "device_id must be a query parameter.",
                std::nullopt};
            send_error(response, 400, error_json(missing, ""));
            log_route("GET", "/api/hardware/telemetry", 400);
            return;
        }
        const std::string device_id = request.get_param_value("device_id");
        Telemetry telemetry;
        const HardwareResult result = manager->read_telemetry(device_id, telemetry);
        if (!result.ok()) {
            const int status = http_status(result.error);
            send_error(response, status, error_json(result, device_id));
            log_route("GET", "/api/hardware/telemetry", status);
            return;
        }
        nlohmann::json readings = nlohmann::json::array();
        for (const TelemetryReading& reading : telemetry.readings) {
            readings.push_back({
                {"name", reading.name},
                {"value", reading.value},
                {"unit", reading.unit},
                {"simulated", reading.simulated},
            });
        }
        nlohmann::json body = simulation_envelope();
        body.update({
            {"device_id", telemetry.device_id},
            {"notice", telemetry.notice},
            {"readings", std::move(readings)},
        });
        send_json(response, 200, body);
        log_route("GET", "/api/hardware/telemetry", 200);
    });

    server.Post("/api/hardware/simulate-failure", [manager](const httplib::Request& request, httplib::Response& response) {
        nlohmann::json parsed;
        HardwareResult parse_error;
        if (!parse_object(request, parsed, parse_error)) {
            const int status = http_status(parse_error.error);
            send_error(response, status, error_json(parse_error, ""));
            log_route("POST", "/api/hardware/simulate-failure", status);
            return;
        }
        std::string device_id;
        std::string failure_id;
        const HardwareResult parsed_body = read_failure(parsed, device_id, failure_id);
        if (!parsed_body.ok()) {
            const int status = http_status(parsed_body.error);
            send_error(response, status, error_json(parsed_body, ""));
            log_route("POST", "/api/hardware/simulate-failure", status);
            return;
        }
        const HardwareResult result = manager->arm_simulated_failure(device_id, failure_id);
        if (!result.ok()) {
            const int status = http_status(result.error);
            send_error(response, status, error_json(result, device_id));
            log_route("POST", "/api/hardware/simulate-failure", status);
            return;
        }
        nlohmann::json body = simulation_envelope();
        body.update({
            {"device_id", device_id},
            {"failure", failure_id},
            {"armed", true},
            {"connection_state", result.connection ? to_string(*result.connection) : "disconnected"},
            {"message", result.message},
        });
        send_json(response, 200, body);
        log_route("POST", "/api/hardware/simulate-failure", 200);
    });
}

}  // namespace workbench
