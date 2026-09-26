// audio_render_test.cpp — headless proof of the aero-audio-physics engine.
//
// Reads the Brain's NDJSON stream (set_audio_params frames written by
// Brain/bin/audio_demo) and renders it into a stereo WAV via the SAME
// dependency-free synth (Born2FlapAudioEngine.h) the UMG host will use. This
// proves the full chain end-to-end at the byte level:
//
//   Ruby aero-audio physics ── NDJSON ──▶ FAudioEngine ──▶ flight.wav
//
// Build: clang++ -std=c++17 -O2 audio_render_test.cpp -o audio_render_test
// Run:   ./audio_render_test <ops.ndjson> <out.wav>

#include "Born2FlapAudioEngine.h"
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using born2flap::audio::FAudioEngine;
using born2flap::ui::EOp;
using born2flap::ui::FOp;
using born2flap::ui::ParseOps;

static void WriteWav(const std::string& path, const std::vector<float>& L,
                     const std::vector<float>& R, int sampleRate) {
    size_t n = L.size();
    std::vector<char> data(n * 4);
    for (size_t i = 0; i < n; ++i) {
        int16_t l = (int16_t)(L[i] * 32767.0);
        int16_t r = (int16_t)(R[i] * 32767.0);
        data[i * 4 + 0] = (char)(l & 0xFF);
        data[i * 4 + 1] = (char)((l >> 8) & 0xFF);
        data[i * 4 + 2] = (char)(r & 0xFF);
        data[i * 4 + 3] = (char)((r >> 8) & 0xFF);
    }
    uint32_t dataSize = (uint32_t)data.size();
    uint32_t byteRate = (uint32_t)(sampleRate * 4);
    std::ofstream out(path, std::ios::binary);
    auto w32 = [&](uint32_t v) { out.write((const char*)&v, 4); };
    auto w16 = [&](uint16_t v) { out.write((const char*)&v, 2); };
    out.write("RIFF", 4); w32(36 + dataSize); out.write("WAVE", 4);
    out.write("fmt ", 4); w32(16); w16(1); w16(2); w32((uint32_t)sampleRate);
    w32(byteRate); w16(4); w16(16);
    out.write("data", 4); w32(dataSize);
    out.write(data.data(), (std::streamsize)data.size());
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: audio_render_test <ops.ndjson> <out.wav>\n";
        return 2;
    }
    const int sampleRate = 44100;
    const double tickHz = 60.0;
    const int framesPerTick = (int)(sampleRate / tickHz);

    FAudioEngine engine(sampleRate);
    std::vector<float> L, R;

    std::ifstream in(argv[1]);
    std::string line;
    int ticks = 0, audioOps = 0;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::vector<FOp> ops;
        if (!ParseOps(line, ops)) {
            std::cerr << "parse error on line " << (ticks + 1) << "\n";
            return 1;
        }
        for (const FOp& op : ops) {
            if (op.op == EOp::SetAudioParams) {
                engine.SetParams(op.voice, op.props);
                ++audioOps;
            }
        }
        for (int i = 0; i < framesPerTick; ++i) {
            float l = 0, r = 0;
            engine.RenderFrame(l, r);
            L.push_back(l);
            R.push_back(r);
        }
        ++ticks;
    }

    WriteWav(argv[2], L, R, sampleRate);
    std::cout << "audio_render_test: " << ticks << " tick(s), " << audioOps
              << " set_audio_params op(s), " << L.size() << " samples ("
              << (L.size() / (double)sampleRate) << " s) → " << argv[2] << "\n";
    return 0;
}
