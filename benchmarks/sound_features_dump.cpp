// sound_features_dump: Essentia's descriptors (EssentiaExtractor's 81
// features, without the tagger's embedding) of a list of files, for training
// the sound tagger (tools/sound_tagger/train.py) on labelled sample
// libraries.
//
//   sound_features_dump --list files.txt --out features.bin [--band HZ] [--threads N]
//
// files.txt has a path a line (UTF-8), or a part of a file as a path, its
// start and its length in seconds, tab-separated (a chop of a vocal, a note
// cut short): analysed as an audio clip's part is. --band limits each sound's bandwidth
// first (resampled to 2 x HZ and back, as a sample library made at that rate
// sounds): an augmentation, so the tagger learns sounds whether or not they
// have their top octaves (NSynth's notes are 16 kHz). The output is the
// similarity bench's cache format: "SIMBENC2", the schema's key, dims, then
// per file its path, whether it was analysed, and its features.

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "core/AudioReader.h"
#include "similarity/EssentiaExtractor.h"

using namespace sub::intelligence;

int main(int argc, char** argv) {
    std::string listFile, outFile;
    uint32_t band = 0;
    unsigned threads = std::max(1u, std::thread::hardware_concurrency());
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (arg == "--list") listFile = next();
        else if (arg == "--out") outFile = next();
        else if (arg == "--band") band = static_cast<uint32_t>(std::stoul(next()));
        else if (arg == "--threads") threads = static_cast<unsigned>(std::stoul(next()));
        else {
            std::cerr << "unknown argument " << arg << "\n";
            return 2;
        }
    }
    if (listFile.empty() || outFile.empty()) {
        std::cerr << "usage: sound_features_dump --list files.txt --out features.bin [--band HZ] [--threads N]\n";
        return 2;
    }
    std::vector<std::string> paths;
    {
        std::ifstream in(listFile);
        for (std::string line; std::getline(in, line);) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty()) paths.push_back(line);
        }
    }
    const FeatureSchema& schema = essentiaSchema();
    const size_t dims = schema.dims();
    std::vector<std::vector<float>> features(paths.size());
    std::atomic<size_t> next{0}, failed{0};
    std::vector<std::thread> pool;
    for (unsigned t = 0; t < threads; ++t) {
        pool.emplace_back([&] {
            EssentiaExtractor extractor;
            for (size_t i; (i = next++) < paths.size();) {
                try {
                    std::string path = paths[i];
                    double start = 0.0, length = -1.0;
                    if (const size_t tab = path.find('\t'); tab != std::string::npos) {
                        const size_t tab2 = path.find('\t', tab + 1);
                        start = std::stod(path.substr(tab + 1, tab2 - tab - 1));
                        if (tab2 != std::string::npos) length = std::stod(path.substr(tab2 + 1));
                        path.resize(tab);
                    }
                    const double wanted = kLeadInSeconds + kAnalysisSeconds;
                    const MonoAudio audio =
                        readMonoAt(path, start, length < 0.0 ? wanted : std::min(length, wanted), kAnalysisRate);
                    std::vector<float> samples = audio.samples;
                    if (band > 0 && 2 * band < kAnalysisRate) {
                        const std::vector<float> low = resampleMono(samples.data(), samples.size(), kAnalysisRate, 2 * band);
                        samples = resampleMono(low.data(), low.size(), 2 * band, kAnalysisRate);
                        samples.resize(audio.samples.size(), 0.f);
                    }
                    SoundBuffer sound;
                    sound.samples = samples.data();
                    sound.count = samples.size();
                    sound.sampleRate = kAnalysisRate;
                    const double toEnd = std::max(0.0, audio.fileSeconds - start);
                    sound.fileSeconds = length >= 0.0 ? std::min(length, toEnd) : toEnd;
                    sound.truncated = audio.truncated && (length < 0.0 || length > wanted);
                    std::vector<float> out(dims);
                    if (extractor.extract(sound, out.data()) == Extraction::Done) features[i] = std::move(out);
                    else ++failed;
                } catch (const std::exception&) {
                    ++failed;
                }
                if (i % 1000 == 999) std::fprintf(stderr, "%zu of %zu\n", i + 1, paths.size());
            }
        });
    }
    for (auto& t : pool) t.join();
    std::ofstream out(outFile, std::ios::binary);
    out.write("SIMBENC2", 8);
    const uint64_t key = schema.key();
    const auto dims32 = static_cast<uint32_t>(dims);
    out.write(reinterpret_cast<const char*>(&key), 8);
    out.write(reinterpret_cast<const char*>(&dims32), 4);
    for (size_t i = 0; i < paths.size(); ++i) {
        const auto len = static_cast<uint32_t>(paths[i].size());
        out.write(reinterpret_cast<const char*>(&len), 4);
        out.write(paths[i].data(), len);
        const char ok = !features[i].empty();
        out.write(&ok, 1);
        std::vector<float> fp = features[i];
        fp.resize(dims, 0.f);
        out.write(reinterpret_cast<const char*>(fp.data()), static_cast<std::streamsize>(sizeof(float) * dims));
    }
    std::printf("%zu files, %zu not analysed: %s\n", paths.size(), failed.load(), outFile.c_str());
    return 0;
}
