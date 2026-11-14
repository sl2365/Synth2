// s2render: render s1ref-format jobs through the Oxo Synth2 engine.
//   s2render <jobs.txt|->        writes each job's `job` path
//   s2render --suffix X <jobs>   writes <path minus .wav>X.wav instead

#include "../../EngineCore/Jobs.hpp"

#include <fstream>
#include <iostream>
#include <iterator>
#include <thread>

int main(int argc, char **argv) {
    std::string suffix, path;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--suffix" && i + 1 < argc) suffix = argv[++i];
        else path = a;
    }
    if (path.empty()) {
        std::cerr << "usage: s2render [--suffix X] <jobs.txt|->\n";
        return 2;
    }
    std::string text;
    if (path == "-") text.assign(std::istreambuf_iterator<char>(std::cin), {});
    else {
        std::ifstream f(path);
        text.assign(std::istreambuf_iterator<char>(f), {});
    }
    std::vector<oxo::s2::Job> jobs;
    std::string error;
    if (!oxo::s2::ParseJobs(text, jobs, error)) {
        std::cerr << error << "\n";
        return 1;
    }
    std::atomic<size_t> next{0}, failed{0};
    auto worker = [&] {
        std::vector<float> l, r;
        for (size_t i; (i = next++) < jobs.size();) {
            std::string out = jobs[i].out;
            if (!suffix.empty() && out.size() > 4) out = out.substr(0, out.size() - 4) + suffix + ".wav";
            oxo::s2::RenderJob(jobs[i], l, r);
            if (!oxo::s2::WriteWavFloat(out, l, r, jobs[i].sampleRate)) failed++;
        }
    };
    std::vector<std::thread> pool;
    for (unsigned t = 0; t < std::max(1u, std::thread::hardware_concurrency()); ++t) pool.emplace_back(worker);
    for (auto &t : pool) t.join();
    std::cerr << "rendered " << jobs.size() - failed << " jobs" << (failed ? ", FAILED writes " : "")
              << (failed ? std::to_string(failed) : "") << "\n";
    return failed ? 1 : 0;
}
