#include "workbench/thermal/temperature_csv_parser.hpp"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

#ifndef WORKBENCH_SOURCE_DIR
#define WORKBENCH_SOURCE_DIR "."
#endif
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

void expect_near(double actual, double expected, const std::string& message) {
    if (!(std::abs(actual - expected) <= 1e-9)) {
        std::cerr << "TEST FAILURE: " << message << " actual=" << actual
                  << " expected=" << expected << '\n';
        ++g_failures;
    }
}

class TempFile {
public:
    TempFile(const std::string& contents, const std::string& extension)
        : path_(make_path(extension)), path_string_(path_.string()) {
        std::filesystem::create_directories(path_.parent_path());
        std::ofstream output(path_, std::ios::binary);
        if (!output) throw std::runtime_error("Cannot create temporary CSV test file.");
        if (!contents.empty()) {
            output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        }
        if (!output) throw std::runtime_error("Cannot write temporary CSV test file.");
    }

    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;

    ~TempFile() {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }

    const std::string& path() const { return path_string_; }

private:
    std::filesystem::path path_;
    std::string path_string_;

    static std::filesystem::path make_path(const std::string& extension) {
        static unsigned sequence = 0;
        const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
        return std::filesystem::path(WORKBENCH_TEST_DIR) /
               ("csv_" + std::to_string(ticks) + "_" + std::to_string(++sequence) + extension);
    }
};

const char* kHeader = "node_id,x_m,y_m,z_m,temperature_C\n";

std::string with_rows(const std::string& rows) {
    return std::string(kHeader) + rows;
}

workbench::TemperatureDataset load_csv(const std::string& text) {
    TempFile file(text, ".csv");
    return workbench::parse_temperature_csv(file.path());
}

void expect_csv_message(const std::string& label,
                        const std::string& text,
                        std::initializer_list<const char*> needles) {
    TempFile file(text, ".csv");
    try {
        workbench::parse_temperature_csv(file.path());
        expect(false, label + " did not throw");
    } catch (const workbench::TemperatureCsvParseError& error) {
        const std::string message = error.what();
        for (const char* needle : needles) {
            if (message.find(needle) == std::string::npos) {
                expect(false, label + " message '" + message + "' does not contain '" + needle + "'");
            }
        }
    } catch (const std::exception& error) {
        expect(false, label + " threw unexpected exception: " + error.what());
    }
}

void test_valid_dataset() {
    try {
        const workbench::TemperatureDataset dataset = load_csv(
            "node_id, x_m, y_m, z_m, temperature_C\r\n"
            "1, 0, 0, 0, 10\r\n"
            "+2, 1, 2, 3, 20\r\n"
            "3, 1, 2, 4, 30\r\n");
        expect(dataset.node_count() == 3, "node count " + std::to_string(dataset.node_count()));
        expect(dataset.nodes[0].node_id == 1, "first id");
        expect(dataset.nodes[1].node_id == 2, "plus sign id");
        expect(dataset.nodes[2].node_id == 3, "third id");
        expect_near(dataset.minimum_temperature_C, 10.0, "minimum temperature");
        expect_near(dataset.maximum_temperature_C, 30.0, "maximum temperature");
        expect_near(dataset.average_temperature_C, 20.0, "average temperature");
        expect_near(dataset.bounds.min.x, 0.0, "min x");
        expect_near(dataset.bounds.min.y, 0.0, "min y");
        expect_near(dataset.bounds.min.z, 0.0, "min z");
        expect_near(dataset.bounds.max.x, 1.0, "max x");
        expect_near(dataset.bounds.max.y, 2.0, "max y");
        expect_near(dataset.bounds.max.z, 4.0, "max z");
    } catch (const std::exception& error) {
        expect(false, std::string("valid CSV threw: ") + error.what());
    }
}

void test_negative_values_and_reordered_columns() {
    try {
        const workbench::TemperatureDataset negative = load_csv(with_rows(
            "1,5,6,7,-10\n"
            "2,8,6,9,-4\n"
            "3,5,10,7,-1\n"));
        expect(negative.node_count() == 3, "negative node count");
        expect_near(negative.minimum_temperature_C, -10.0, "negative minimum");
        expect_near(negative.maximum_temperature_C, -1.0, "negative maximum");
        expect_near(negative.average_temperature_C, -5.0, "negative average");
        expect_near(negative.bounds.min.x, 5.0, "negative-set min x");
        expect_near(negative.bounds.min.y, 6.0, "negative-set min y");
        expect_near(negative.bounds.min.z, 7.0, "negative-set min z");
        expect_near(negative.bounds.max.x, 8.0, "negative-set max x");
        expect_near(negative.bounds.max.y, 10.0, "negative-set max y");
        expect_near(negative.bounds.max.z, 9.0, "negative-set max z");

        const workbench::TemperatureDataset reordered = load_csv(
            "temperature_C,z_m,y_m,x_m,node_id\n"
            "1.5e1,3,2,1e-1,9\n");
        expect(reordered.node_count() == 1, "single node count");
        expect(reordered.nodes[0].node_id == 9, "reordered id");
        expect_near(reordered.nodes[0].x_m, 0.1, "scientific x");
        expect_near(reordered.nodes[0].y_m, 2.0, "reordered y");
        expect_near(reordered.nodes[0].z_m, 3.0, "reordered z");
        expect_near(reordered.minimum_temperature_C, 15.0, "single minimum");
        expect_near(reordered.maximum_temperature_C, 15.0, "single maximum");
        expect_near(reordered.average_temperature_C, 15.0, "single average");
        expect_near(reordered.bounds.min.x, 0.1, "single min x");
        expect_near(reordered.bounds.max.z, 3.0, "single max z");
    } catch (const std::exception& error) {
        expect(false, std::string("negative or reordered CSV threw: ") + error.what());
    }
}

void test_sample_dataset_and_provenance() {
    const auto root = std::filesystem::path(WORKBENCH_SOURCE_DIR);
    try {
        const workbench::TemperatureDataset dataset =
            workbench::parse_temperature_csv((root / "data" / "thermal" / "sample_temperature.csv").string());
        expect(dataset.node_count() == 27, "sample node count " + std::to_string(dataset.node_count()));
        expect(dataset.nodes.front().node_id == 1, "sample first id");
        expect(dataset.nodes.back().node_id == 27, "sample last id");
        expect_near(dataset.nodes.front().temperature_C, 25.0, "sample first temperature");
        expect_near(dataset.nodes.back().temperature_C, 67.0, "sample last temperature");
        expect(dataset.nodes[13].node_id == 14, "sample node 14 id");
        expect_near(dataset.nodes[13].x_m, 0.05, "sample node 14 x");
        expect_near(dataset.nodes[13].y_m, 0.025, "sample node 14 y");
        expect_near(dataset.nodes[13].z_m, 0.01, "sample node 14 z");
        expect_near(dataset.nodes[13].temperature_C, 46.0, "sample node 14 temperature");
        expect_near(dataset.minimum_temperature_C, 25.0, "sample minimum");
        expect_near(dataset.maximum_temperature_C, 67.0, "sample maximum");
        expect_near(dataset.average_temperature_C, 46.0, "sample average");
        expect_near(dataset.bounds.min.x, 0.0, "sample min x");
        expect_near(dataset.bounds.min.y, 0.0, "sample min y");
        expect_near(dataset.bounds.min.z, 0.0, "sample min z");
        expect_near(dataset.bounds.max.x, 0.1, "sample max x");
        expect_near(dataset.bounds.max.y, 0.05, "sample max y");
        expect_near(dataset.bounds.max.z, 0.02, "sample max z");
    } catch (const std::exception& error) {
        expect(false, std::string("sample CSV threw: ") + error.what());
    }

    const auto provenance = root / "data" / "PROVENANCE.md";
    std::ifstream input(provenance);
    expect(static_cast<bool>(input), "provenance file opens");
    const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    expect(text.find("SYNTHETIC") != std::string::npos, "provenance labels the data SYNTHETIC");
    expect(text.find("not an ANSYS export") != std::string::npos,
           "provenance states the CSV is not an ANSYS export");
}

void test_rejections() {
    expect_csv_message("empty file", "", {"empty"});
    expect_csv_message("empty dataset", kHeader, {"no data"});
    expect_csv_message(
        "missing columns",
        "node_id,x_m,y_m,temperature_C\n1,0,0,10\n",
        {"row 1", "expected 5 columns"});
    expect_csv_message(
        "unrecognized column",
        "node_id,x_m,y_m,z,temperature_C\n1,0,0,0,10\n",
        {"unrecognized column 'z'"});
    expect_csv_message("missing value", with_rows("1,0,,0,10\n"), {"row 2", "y_m is missing"});
    expect_csv_message(
        "malformed row",
        with_rows("1,0,0,0\n"),
        {"row 2", "expected 5 values"});
    expect_csv_message("blank line", with_rows("1,0,0,0,1\n\n"), {"row 3", "row is empty"});
    expect_csv_message(
        "duplicate node",
        with_rows("7,0,0,0,1\n7,1,0,0,2\n"),
        {"row 3", "duplicate node_id 7", "row 2"});
    expect_csv_message(
        "duplicate leading zero",
        with_rows("1,0,0,0,10\n01,1,0,0,11\n"),
        {"duplicate node_id 1"});
    expect_csv_message("non-numeric coordinate", with_rows("1,abc,0,0,10\n"),
                       {"row 2", "not a number"});
    expect_csv_message("non-numeric id", with_rows("abc,0,0,0,10\n"),
                       {"row 2", "not an integer"});
    expect_csv_message("nan", with_rows("1,0,0,0,nan\n"), {"row 2", "not finite"});
    expect_csv_message("NaN", with_rows("1,0,0,0,NaN\n"), {"not finite"});
    expect_csv_message("infinity", with_rows("1,inf,0,0,10\n"), {"row 2", "not finite"});
    expect_csv_message("negative infinity", with_rows("1,0,0,0,-inf\n"), {"not finite"});
}

void test_limits_and_cleanup() {
    const std::string text = with_rows("1,0,0,0,1\n2,1,0,0,2\n");
    TempFile file(text, ".csv");

    workbench::TemperatureCsvLimits tiny_file;
    tiny_file.max_file_bytes = 8;
    try {
        workbench::parse_temperature_csv(file.path(), tiny_file);
        expect(false, "oversized CSV was accepted");
    } catch (const workbench::TemperatureCsvParseError& error) {
        expect(std::string(error.what()).find("exceeds the limit") != std::string::npos,
               std::string("file limit message: ") + error.what());
    } catch (const std::exception& error) {
        expect(false, std::string("file limit threw: ") + error.what());
    }

    workbench::TemperatureCsvLimits one_node;
    one_node.max_nodes = 1;
    try {
        workbench::parse_temperature_csv(file.path(), one_node);
        expect(false, "node limit was not enforced");
    } catch (const workbench::TemperatureCsvParseError& error) {
        const std::string message = error.what();
        expect(message.find("exceeds the limit of 1") != std::string::npos,
               "node limit message: " + message);
        expect(message.find("row 3") != std::string::npos, "node limit row: " + message);
    } catch (const std::exception& error) {
        expect(false, std::string("node limit threw: ") + error.what());
    }

    bool rejected_limit = false;
    try {
        workbench::TemperatureCsvLimits invalid;
        invalid.max_nodes = 0;
        workbench::parse_temperature_csv(file.path(), invalid);
    } catch (const std::invalid_argument&) {
        rejected_limit = true;
    } catch (const std::exception& error) {
        expect(false, std::string("zero node limit threw: ") + error.what());
    }
    expect(rejected_limit, "zero node limit was not rejected");

    const std::string stored = file.path();
    expect(std::filesystem::exists(stored), "temporary CSV exists before cleanup");
}

void test_temporary_file_is_removed() {
    std::string stored;
    {
        TempFile file(kHeader, ".csv");
        stored = file.path();
        bool rejected = false;
        try {
            workbench::parse_temperature_csv(stored);
        } catch (const workbench::TemperatureCsvParseError&) {
            rejected = true;
        }
        expect(rejected, "header-only file is rejected");
        expect(std::filesystem::exists(stored), "temporary CSV remains after a parse error");
    }
    expect(!std::filesystem::exists(stored), "temporary CSV is removed");
}

}  // namespace

int main() {
    try {
        test_valid_dataset();
        test_negative_values_and_reordered_columns();
        test_sample_dataset_and_provenance();
        test_rejections();
        test_limits_and_cleanup();
        test_temporary_file_is_removed();
    } catch (const std::exception& error) {
        std::cerr << "TEST FAILURE: uncaught exception: " << error.what() << '\n';
        return 1;
    }
    if (g_failures != 0) {
        std::cerr << g_failures << " temperature CSV test failure(s).\n";
        return 1;
    }
    std::cout << "All temperature CSV tests passed.\n";
    return 0;
}
