#include "adapters/wav_folder_repository.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

#include "miniaudio.h"

namespace sampler {
namespace {

constexpr ma_uint64 kReadChunkFrames = 4096;

bool decodeInto(const std::string& path, uint32_t sampleRate, Sample& out) {
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, kChannels, sampleRate);
    ma_decoder decoder;
    if (ma_decoder_init_file(path.c_str(), &cfg, &decoder) != MA_SUCCESS) return false;

    std::vector<float> chunk(kReadChunkFrames * kChannels);
    for (;;) {
        ma_uint64 read = 0;
        const ma_result res =
            ma_decoder_read_pcm_frames(&decoder, chunk.data(), kReadChunkFrames, &read);
        if (read > 0) {
            out.frames.insert(out.frames.end(), chunk.begin(),
                              chunk.begin() + static_cast<size_t>(read * kChannels));
        }
        if (res != MA_SUCCESS || read == 0) break;
    }
    ma_decoder_uninit(&decoder);

    out.frameCount = out.frames.size() / kChannels;
    return out.frameCount > 0;
}

}  // namespace

LoadResult WavFolderRepository::load(const std::string& folder, uint32_t sampleRate) {
    namespace fs = std::filesystem;
    LoadResult result;
    result.library.sampleRate = sampleRate;

    std::vector<fs::path> paths;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(folder, ec)) {
        if (!entry.is_regular_file()) continue;
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return static_cast<char>(::tolower(c)); });
        if (ext == ".wav") paths.push_back(entry.path());
    }
    std::sort(paths.begin(), paths.end());

    for (const auto& p : paths) {
        Sample s;
        s.name = p.filename().string();
        if (decodeInto(p.string(), sampleRate, s)) {
            result.library.samples.push_back(std::move(s));
        } else {
            result.failed.push_back(s.name);
        }
    }
    return result;
}

}  // namespace sampler
