/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/

#include "hailo/genai/llm/llm.hpp"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace
{

using Clock = std::chrono::steady_clock;

double elapsed_ms(const Clock::time_point &start, const Clock::time_point &end)
{
    return std::chrono::duration<double, std::milli>(end - start).count();
}

std::string json_escape(const std::string &value)
{
    std::string escaped;
    escaped.reserve(value.size());
    for (const auto ch : value) {
        switch (ch) {
        case '\\': escaped += "\\\\"; break;
        case '"': escaped += "\\\""; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default: escaped += ch; break;
        }
    }
    return escaped;
}

} // namespace

int main(int argc, char **argv)
{
    try {
        if ((argc < 2) || (argc > 4)) {
            std::cerr << "Usage: " << argv[0] << " <hef_path> [max_generated_tokens] [prompt]" << std::endl;
            return 2;
        }

        const std::string hef_path = argv[1];
        const auto max_generated_tokens = (argc >= 3) ? static_cast<uint32_t>(std::stoul(argv[2])) : 128U;
        const std::string prompt = (argc >= 4)
            ? argv[3]
            : "Explain zero trust network security in about 200 words, with concrete technical details.";

        const auto device_start = Clock::now();
        auto vdevice = hailort::VDevice::create_shared().expect("Failed to create VDevice");
        const auto device_ready = Clock::now();

        const auto model_start = Clock::now();
        auto llm_params = hailort::genai::LLMParams(hef_path);
        auto llm = hailort::genai::LLM::create(vdevice, llm_params).expect("Failed to create LLM");
        const auto model_ready = Clock::now();

        auto generator_params = llm.create_generator_params().expect("Failed to create generator params");
        auto status = generator_params.set_max_generated_tokens(max_generated_tokens);
        if (HAILO_SUCCESS != status) {
            throw hailort::hailort_error(status, "Failed to set max generated tokens");
        }
        status = generator_params.set_do_sample(false);
        if (HAILO_SUCCESS != status) {
            throw hailort::hailort_error(status, "Failed to select greedy decoding");
        }

        const std::vector<std::string> messages = {
            R"({"role":"system","content":"You are a concise technical assistant running locally on a Hailo-10H accelerator."})",
            R"({"role":"user","content":")" + json_escape(prompt) + R"("})"
        };

        const auto generation_start = Clock::now();
        auto completion = llm.generate(generator_params, messages).expect("Failed to generate");

        bool received_first_completion = false;
        Clock::time_point first_completion_time{};
        size_t completion_reads = 0;
        std::string response;

        while (hailort::genai::LLMGeneratorCompletion::Status::GENERATING == completion.generation_status()) {
            auto chunk = completion.read().expect("Failed to read completion");
            const auto read_time = Clock::now();
            if (!received_first_completion) {
                first_completion_time = read_time;
                received_first_completion = true;
            }
            response += chunk;
            completion_reads++;
        }
        const auto generation_end = Clock::now();

        const auto generation_ms = elapsed_ms(generation_start, generation_end);
        const auto ttft_ms = received_first_completion
            ? elapsed_ms(generation_start, first_completion_time)
            : generation_ms;
        const auto decode_ms = received_first_completion
            ? elapsed_ms(first_completion_time, generation_end)
            : 0.0;
        const auto total_chunks_per_second = (generation_ms > 0.0)
            ? (1000.0 * static_cast<double>(completion_reads) / generation_ms)
            : 0.0;
        const auto sustained_chunks_per_second = ((completion_reads > 1) && (decode_ms > 0.0))
            ? (1000.0 * static_cast<double>(completion_reads - 1) / decode_ms)
            : 0.0;

        std::cout << std::fixed << std::setprecision(2);
        std::cout << "METRIC device_create_ms=" << elapsed_ms(device_start, device_ready) << '\n';
        std::cout << "METRIC model_load_ms=" << elapsed_ms(model_start, model_ready) << '\n';
        std::cout << "METRIC ttft_ms=" << ttft_ms << '\n';
        std::cout << "METRIC generation_ms=" << generation_ms << '\n';
        std::cout << "METRIC completion_reads=" << completion_reads << '\n';
        std::cout << "METRIC total_completion_reads_per_second=" << total_chunks_per_second << '\n';
        std::cout << "METRIC sustained_completion_reads_per_second=" << sustained_chunks_per_second << '\n';
        std::cout << "METRIC final_status=" << static_cast<int>(completion.generation_status()) << '\n';
        std::cout << "RESPONSE_BEGIN\n" << response << "\nRESPONSE_END" << std::endl;
    } catch (const hailort::hailort_error &exception) {
        std::cerr << "LLM benchmark failed. status=" << exception.status()
                  << ", error=" << exception.what() << std::endl;
        return 1;
    } catch (const std::exception &exception) {
        std::cerr << "LLM benchmark failed: " << exception.what() << std::endl;
        return 1;
    }

    return 0;
}
