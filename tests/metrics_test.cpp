#include <catch_amalgamated.hpp>
#include <metrics.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

TEST_CASE("LatencyHistogram calculates nearest-rank percentiles", "[metrics]")
{
    lsmkv::bench::LatencyHistogram histogram;
    histogram.record(30);
    histogram.record(10);
    histogram.record(50);
    histogram.record(20);
    histogram.record(40);
    REQUIRE(histogram.size() == 5);
    const lsmkv::bench::LatencySummary summary = histogram.summarize();
    REQUIRE(summary.p50_ns == 30);
    REQUIRE(summary.p99_ns == 50);
    REQUIRE(summary.p999_ns == 50);
}

TEST_CASE("LatencyHistogram handles an empty sample set", "[metrics]")
{
    lsmkv::bench::LatencyHistogram histogram;
    const lsmkv::bench::LatencySummary summary = histogram.summarize();
    REQUIRE(histogram.size() == 0);
    REQUIRE(summary.p50_ns == 0);
    REQUIRE(summary.p99_ns == 0);
    REQUIRE(summary.p999_ns == 0);
}

TEST_CASE("Metrics calculate write and space amplification", "[metrics]")
{
    REQUIRE(lsmkv::bench::calculateWriteAmplification(220, 580, 200) == 4.0);
    REQUIRE(lsmkv::bench::calculateWriteAmplification(220, 580, 0) == 0.0);
    REQUIRE(lsmkv::bench::calculateSpaceAmplification(750, 500) == 1.5);
    REQUIRE(lsmkv::bench::calculateSpaceAmplification(750, 0) == 0.0);
}

TEST_CASE("Metrics write CSV and JSON results", "[metrics]")
{
    const std::filesystem::path csv_path = std::filesystem::temp_directory_path() / "lsmkv_metrics_test.csv";
    const std::filesystem::path json_path = std::filesystem::temp_directory_path() / "lsmkv_metrics_test.json";
    std::filesystem::remove(csv_path);
    std::filesystem::remove(json_path);
    lsmkv::bench::BenchmarkResult result;
    result.seed = 42;
    result.operation_count = 1000;
    result.key_distribution = "zipfian";
    result.sync_mode = "off";
    result.read_latency.p99_ns = 81;
    result.write_amplification = 4.0;
    REQUIRE(lsmkv::bench::writeCsv(csv_path.string(), result));
    REQUIRE(lsmkv::bench::writeCsv(csv_path.string(), result));
    REQUIRE(lsmkv::bench::writeJson(json_path.string(), result));
    std::ifstream csv_file(csv_path);
    std::ifstream json_file(json_path);
    REQUIRE(csv_file.is_open());
    REQUIRE(json_file.is_open());
    const std::string csv{std::istreambuf_iterator<char>(csv_file), std::istreambuf_iterator<char>()};
    const std::string json{std::istreambuf_iterator<char>(json_file), std::istreambuf_iterator<char>()};
    REQUIRE(std::count(csv.begin(), csv.end(), '\n') == 3);
    REQUIRE(csv.find("seed,operations") == 0);
    REQUIRE(csv.find("42,1000") != std::string::npos);
    REQUIRE(json.find("\"seed\": 42") != std::string::npos);
    REQUIRE(json.find("\"read_p99_ns\": 81") != std::string::npos);
    std::filesystem::remove(csv_path);
    std::filesystem::remove(json_path);
}
