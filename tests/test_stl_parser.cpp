#include "workbench/geometry/stl_parser.hpp"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
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
        std::cerr << "TEST FAILURE: " << message << " actual=" << std::setprecision(17)
                  << actual << " expected=" << expected << '\n';
        ++g_failures;
    }
}

class TempFile {
public:
    TempFile(const std::string& contents, const std::string& extension)
        : path_(make_path(extension)), path_string_(path_.string()) {
        std::filesystem::create_directories(path_.parent_path());
        std::ofstream output(path_, std::ios::binary);
        if (!output) throw std::runtime_error("Cannot create temporary STL test file.");
        if (!contents.empty()) {
            output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        }
        if (!output) throw std::runtime_error("Cannot write temporary STL test file.");
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
               ("stl_" + std::to_string(ticks) + "_" + std::to_string(++sequence) + extension);
    }
};

workbench::StlMesh load_stl(const std::string& text) {
    TempFile file(text, ".stl");
    return workbench::parse_ascii_stl(file.path());
}

void expect_stl_error(const std::string& label, const std::string& text, const std::string& needle) {
    TempFile file(text, ".stl");
    try {
        workbench::parse_ascii_stl(file.path());
        expect(false, label + " did not throw");
    } catch (const workbench::StlParseError& error) {
        const std::string message = error.what();
        if (message.find(needle) == std::string::npos) {
            expect(false, label + " message '" + message + "' does not contain '" + needle + "'");
        }
    } catch (const std::exception& error) {
        expect(false, label + " threw unexpected exception: " + error.what());
    }
}

const char* kTwoTriangles =
    "solid wedge\r\n"
    "  facet normal 100 0 0\r\n"
    "    outer loop\r\n"
    "      vertex 0 0 0\r\n"
    "      vertex 1 0 0\r\n"
    "      vertex 0 1 0\r\n"
    "    endloop\r\n"
    "  endfacet\r\n"
    "  facet normal 0 0 1\r\n"
    "    outer loop\r\n"
    "      vertex 0 0 2\r\n"
    "      vertex 0 3 2\r\n"
    "      vertex 0 0 5\r\n"
    "    endloop\r\n"
    "  endfacet\r\n"
    "endsolid wedge\r\n";

void test_multiple_triangles_and_bounds() {
    try {
        const workbench::StlMesh mesh = load_stl(kTwoTriangles);
        expect(mesh.solid_name == "wedge", "solid name");
        expect(mesh.triangle_count() == 2, "triangle count " + std::to_string(mesh.triangle_count()));
        expect(mesh.vertex_count() == 6, "vertex count " + std::to_string(mesh.vertex_count()));
        expect_near(mesh.triangles[0].normal.x, 100.0, "stored normal is not part of the box");
        expect_near(mesh.bounds.min.x, 0.0, "min x");
        expect_near(mesh.bounds.min.y, 0.0, "min y");
        expect_near(mesh.bounds.min.z, 0.0, "min z");
        expect_near(mesh.bounds.max.x, 1.0, "max x excludes the normal");
        expect_near(mesh.bounds.max.y, 3.0, "max y");
        expect_near(mesh.bounds.max.z, 5.0, "max z");
        expect_near(mesh.bounds.dimension_x(), 1.0, "dimension x");
        expect_near(mesh.bounds.dimension_y(), 3.0, "dimension y");
        expect_near(mesh.bounds.dimension_z(), 5.0, "dimension z");
        expect_near(mesh.triangles[1].vertices[0].z, 2.0, "second triangle was stored");
    } catch (const std::exception& error) {
        expect(false, std::string("valid STL threw: ") + error.what());
    }
}

void test_bounds_do_not_start_at_origin() {
    try {
        const workbench::StlMesh mesh = load_stl(
            "solid offset\n"
            "facet normal 0 0 1\n"
            "outer loop\n"
            "vertex +5 5 5\n"
            "vertex 6 5 5\n"
            "vertex 5 6 5\n"
            "endloop\n"
            "endfacet\n"
            "endsolid offset\n");
        expect(mesh.triangle_count() == 1, "offset triangle count");
        expect_near(mesh.bounds.min.x, 5.0, "offset min x");
        expect_near(mesh.bounds.min.y, 5.0, "offset min y");
        expect_near(mesh.bounds.min.z, 5.0, "offset min z");
        expect_near(mesh.bounds.max.x, 6.0, "offset max x");
        expect_near(mesh.bounds.max.y, 6.0, "offset max y");
        expect_near(mesh.bounds.max.z, 5.0, "offset max z");
    } catch (const std::exception& error) {
        expect(false, std::string("offset STL threw: ") + error.what());
    }
}

void test_name_and_compact_solid() {
    try {
        const workbench::StlMesh named = load_stl(
            "solid my block\n"
            "facet normal 0 0 1\n"
            "outer loop\n"
            "vertex 0 0 0\n"
            "vertex 1 0 0\n"
            "vertex 0 1 0\n"
            "endloop\n"
            "endfacet\n"
            "endsolid my block\n");
        expect(named.solid_name == "my block", "multi-word solid name");

        const workbench::StlMesh compact = load_stl(
            "solid compact facet normal 0 0 1 outer loop "
            "vertex 0 0 0 vertex 1e1 0 0 vertex 0 1 0 "
            "endloop endfacet endsolid compact\n");
        expect(compact.solid_name == "compact", "compact solid name");
        expect(compact.triangle_count() == 1, "compact triangle count");
        expect_near(compact.bounds.max.x, 10.0, "scientific vertex coordinate");
    } catch (const std::exception& error) {
        expect(false, std::string("name or compact STL threw: ") + error.what());
    }
}

void test_sample_geometry() {
    const auto sample = std::filesystem::path(WORKBENCH_SOURCE_DIR) /
                        "data" / "geometry" / "sample_geometry.stl";
    try {
        const workbench::StlMesh mesh = workbench::parse_ascii_stl(sample.string());
        expect(mesh.solid_name == "rectangular_block", "sample solid name");
        expect(mesh.triangle_count() == 12, "sample triangle count");
        expect(mesh.vertex_count() == 36, "sample vertex count");
        expect_near(mesh.triangles.front().normal.x, 0.0, "sample first normal x");
        expect_near(mesh.triangles.front().normal.y, 0.0, "sample first normal y");
        expect_near(mesh.triangles.front().normal.z, -1.0, "sample first normal z");
        expect_near(mesh.bounds.min.x, 0.0, "sample min x");
        expect_near(mesh.bounds.min.y, 0.0, "sample min y");
        expect_near(mesh.bounds.min.z, 0.0, "sample min z");
        expect_near(mesh.bounds.max.x, 0.10, "sample max x");
        expect_near(mesh.bounds.max.y, 0.05, "sample max y");
        expect_near(mesh.bounds.max.z, 0.02, "sample max z");
        expect_near(mesh.bounds.dimension_x(), 0.10, "sample dimension x");
        expect_near(mesh.bounds.dimension_y(), 0.05, "sample dimension y");
        expect_near(mesh.bounds.dimension_z(), 0.02, "sample dimension z");
    } catch (const std::exception& error) {
        expect(false, std::string("sample STL threw: ") + error.what());
    }
}

void test_rejections() {
    expect_stl_error("empty file", "", "empty");
    expect_stl_error("whitespace file", " \n\t\r\n", "empty");
    expect_stl_error("no facets", "solid empty\nendsolid empty\n", "no facets");
    expect_stl_error(
        "malformed structure",
        "solid bad\nfacet normal 0 0 1\nvertex 0 0 0\n",
        "line 3");
    expect_stl_error(
        "malformed keyword",
        "solid bad\nfacet normal 0 0 1\nvertex 0 0 0\n",
        "expected 'outer'");
    expect_stl_error(
        "truncated facet",
        "solid bad\nfacet normal 0 0 1\n",
        "unexpected end of file");
    expect_stl_error(
        "non-finite vertex",
        "solid bad\nfacet normal 0 0 1\nouter loop\nvertex 0 0 nan\n",
        "line 4");
    expect_stl_error(
        "non-finite value",
        "solid bad\nfacet normal 0 0 1\nouter loop\nvertex 0 0 nan\n",
        "not finite");
    expect_stl_error("infinite normal", "solid bad\nfacet normal inf 0 0\n", "not finite");
    expect_stl_error("not stl text", "this is not an stl file\n", "ASCII STL");
    expect_stl_error(
        "second solid",
        "solid a\n"
        "facet normal 0 0 1\nouter loop\n"
        "vertex 0 0 0\nvertex 1 0 0\nvertex 0 1 0\n"
        "endloop\nendfacet\nendsolid a\n"
        "solid b\n"
        "facet normal 0 0 1\nouter loop\n"
        "vertex 0 0 0\nvertex 1 0 0\nvertex 0 1 0\n"
        "endloop\nendfacet\nendsolid b\n",
        "Only one solid");
}

void test_binary_is_rejected() {
    std::string binary(80, 'B');
    binary.push_back(1);
    binary.push_back(0);
    binary.push_back(0);
    binary.push_back(0);
    binary.append(50, '\0');
    expect_stl_error("binary stl", binary, "binary STL");

    std::string prefixed(80, ' ');
    prefixed.replace(0, 5, "solid");
    prefixed[6] = '\0';
    prefixed.push_back(1);
    prefixed.push_back(0);
    prefixed.push_back(0);
    prefixed.push_back(0);
    prefixed.append(50, '\0');
    expect_stl_error("binary stl with solid header", prefixed, "binary STL");
}

void test_limits() {
    workbench::StlImportLimits tiny_file;
    tiny_file.max_file_bytes = 20;
    TempFile file(kTwoTriangles, ".stl");
    try {
        workbench::parse_ascii_stl(file.path(), tiny_file);
        expect(false, "oversized file was accepted");
    } catch (const workbench::StlParseError& error) {
        expect(std::string(error.what()).find("exceeds the limit") != std::string::npos,
               std::string("file limit message: ") + error.what());
    } catch (const std::exception& error) {
        expect(false, std::string("file limit threw: ") + error.what());
    }

    workbench::StlImportLimits one_triangle;
    one_triangle.max_triangles = 1;
    try {
        workbench::parse_ascii_stl(file.path(), one_triangle);
        expect(false, "triangle limit was not enforced");
    } catch (const workbench::StlParseError& error) {
        expect(std::string(error.what()).find("exceeds the limit of 1") != std::string::npos,
               std::string("triangle limit message: ") + error.what());
    } catch (const std::exception& error) {
        expect(false, std::string("triangle limit threw: ") + error.what());
    }

    bool rejected_limit = false;
    try {
        workbench::StlImportLimits invalid;
        invalid.max_triangles = 0;
        workbench::parse_ascii_stl(file.path(), invalid);
    } catch (const std::invalid_argument&) {
        rejected_limit = true;
    } catch (const std::exception& error) {
        expect(false, std::string("zero triangle limit threw: ") + error.what());
    }
    expect(rejected_limit, "zero triangle limit was not rejected");

    bool missing = false;
    try {
        const auto path = std::filesystem::path(WORKBENCH_TEST_DIR) / "missing_file.stl";
        workbench::parse_ascii_stl(path.string());
    } catch (const workbench::StlParseError& error) {
        missing = std::string(error.what()).find("cannot open") != std::string::npos;
        if (!missing) expect(false, std::string("missing file message: ") + error.what());
    } catch (const std::exception& error) {
        expect(false, std::string("missing file threw: ") + error.what());
    }
    expect(missing, "missing file was not rejected");
}

void test_temporary_file_is_removed() {
    std::string stored;
    {
        TempFile file("solid empty\nendsolid empty\n", ".stl");
        stored = file.path();
        expect(std::filesystem::exists(stored), "temporary file exists while in scope");
        bool rejected = false;
        try {
            workbench::parse_ascii_stl(stored);
        } catch (const workbench::StlParseError&) {
            rejected = true;
        }
        expect(rejected, "facet-free solid is rejected");
        expect(std::filesystem::exists(stored), "temporary file remains after a parse error");
    }
    expect(!std::filesystem::exists(stored), "temporary file is removed");
}

}  // namespace

int main() {
    try {
        test_multiple_triangles_and_bounds();
        test_bounds_do_not_start_at_origin();
        test_name_and_compact_solid();
        test_sample_geometry();
        test_rejections();
        test_binary_is_rejected();
        test_limits();
        test_temporary_file_is_removed();
    } catch (const std::exception& error) {
        std::cerr << "TEST FAILURE: uncaught exception: " << error.what() << '\n';
        return 1;
    }
    if (g_failures != 0) {
        std::cerr << g_failures << " STL parser test failure(s).\n";
        return 1;
    }
    std::cout << "All STL parser tests passed.\n";
    return 0;
}
