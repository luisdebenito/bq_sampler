#include <cstdio>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#include "adapters/wav_folder_repository.h"
#include "doctest.h"

namespace fs = std::filesystem;

static void writeWav(const fs::path& path, uint32_t rate, uint16_t channels,
                     uint32_t frameCount) {
    const uint16_t bits = 16;
    const uint32_t dataBytes = frameCount * channels * (bits / 8);
    const uint32_t byteRate = rate * channels * (bits / 8);
    const uint16_t blockAlign = static_cast<uint16_t>(channels * (bits / 8));
    const uint32_t riffSize = 36 + dataBytes;
    const uint32_t fmtSize = 16;
    const uint16_t pcm = 1;

    std::ofstream f(path, std::ios::binary);
    auto u32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };

    f.write("RIFF", 4); u32(riffSize); f.write("WAVE", 4);
    f.write("fmt ", 4); u32(fmtSize); u16(pcm); u16(channels);
    u32(rate); u32(byteRate); u16(blockAlign); u16(bits);
    f.write("data", 4); u32(dataBytes);
    for (uint32_t i = 0; i < frameCount * channels; ++i) u16(0x1000);
}

static fs::path makeTempDir(const std::string& name) {
    fs::path dir = fs::temp_directory_path() / ("sampler_test_" + name);
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

TEST_CASE("loads wav files sorted alphabetically") {
    auto dir = makeTempDir("sorted");
    writeWav(dir / "02_b.wav", 48000, 2, 100);
    writeWav(dir / "01_a.wav", 48000, 2, 100);
    writeWav(dir / "notes.txt", 48000, 2, 100);

    sampler::WavFolderRepository repo;
    auto r = repo.load(dir.string(), 48000);

    REQUIRE(r.library.samples.size() == 2);
    CHECK(r.library.samples[0].name == "01_a.wav");
    CHECK(r.library.samples[1].name == "02_b.wav");
    CHECK(r.failed.empty());
    fs::remove_all(dir);
}

TEST_CASE("converts mono to stereo at the requested sample rate") {
    auto dir = makeTempDir("mono");
    writeWav(dir / "a.wav", 44100, 1, 4410);

    sampler::WavFolderRepository repo;
    auto r = repo.load(dir.string(), 48000);

    REQUIRE(r.library.samples.size() == 1);
    const auto& s = r.library.samples[0];
    CHECK(s.frames.size() == s.frameCount * sampler::kChannels);
    CHECK(s.frameCount > 4700);   // 0.1 s remuestreado a 48 kHz
    CHECK(s.frameCount < 5100);
    CHECK(r.library.sampleRate == 48000);
    fs::remove_all(dir);
}

TEST_CASE("a corrupt file is reported, not loaded") {
    auto dir = makeTempDir("corrupt");
    writeWav(dir / "good.wav", 48000, 2, 100);
    std::ofstream(dir / "bad.wav", std::ios::binary) << "this is not a wav";

    sampler::WavFolderRepository repo;
    auto r = repo.load(dir.string(), 48000);

    CHECK(r.library.samples.size() == 1);
    REQUIRE(r.failed.size() == 1);
    CHECK(r.failed[0].find("bad.wav") != std::string::npos);
    fs::remove_all(dir);
}

// Review Focus 4: un wav de 0 frames haria bucle infinito en el limite de vuelta
TEST_CASE("an empty wav is rejected instead of loaded") {
    auto dir = makeTempDir("empty");
    writeWav(dir / "silent.wav", 48000, 2, 0);
    writeWav(dir / "good.wav", 48000, 2, 100);

    sampler::WavFolderRepository repo;
    auto r = repo.load(dir.string(), 48000);

    CHECK(r.library.samples.size() == 1);
    CHECK(r.library.samples[0].name == "good.wav");
    REQUIRE(r.failed.size() == 1);
    CHECK(r.failed[0].find("silent.wav") != std::string::npos);
    fs::remove_all(dir);
}

TEST_CASE("a folder with no wav files loads nothing and reports nothing") {
    auto dir = makeTempDir("nowavs");
    std::ofstream(dir / "readme.md") << "hello";

    sampler::WavFolderRepository repo;
    auto r = repo.load(dir.string(), 48000);

    CHECK(r.library.samples.empty());
    CHECK(r.failed.empty());
    fs::remove_all(dir);
}
