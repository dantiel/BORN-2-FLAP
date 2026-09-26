#pragma once
// Born2FlapTransport.h — the NDJSON transport reader (dependency-free).
//
// The C++ half of the Brain↔UMG bridge's framing. The Ruby `Transport`
// (Brain/lib/born2flap/ui/transport.rb) writes newline-delimited JSON: one
// `Wire.dump_ops` array per line (one frame per Brain tick). This header reads
// that stream line-by-line and applies each frame to an FRenderer host.
//
// It has NO Unreal dependency, so the SAME reader is used by the UMG host
// (Born2FlapUIRenderer, which already exposes ApplyOpsJson) and by
// Tools/umg_transport_test.cpp (headless clang++). In Unreal, a game-loop tick
// reads one line/file and calls ApplyOpsJson — the framing here is what makes
// that process boundary trivial.

#include "Born2FlapUiOps.h"
#include "Born2FlapUiTree.h"

#include <istream>
#include <string>
#include <vector>

namespace born2flap::ui {

struct FTransport {
    std::istream& in;
    std::string line;

    explicit FTransport(std::istream& stream) : in(stream) {}

    // Apply every frame currently available. Returns the number of frames
    // applied. Call once per tick (or once from a headless harness).
    template <typename Host>
    int Poll(FRenderer<Host>& renderer) {
        int frames = 0;
        while (ReadLine()) {
            if (ApplyFrame(renderer, line)) ++frames;
        }
        return frames;
    }

    // Parse + apply a single frame (one JSON op array). Returns false on a
    // malformed frame so a bad/partial line can be skipped without breaking the
    // stream — the next '\n' re-synchronizes.
    template <typename Host>
    bool ApplyFrame(FRenderer<Host>& renderer, const std::string& frame) {
        std::vector<FOp> ops;
        if (!ParseOps(frame, ops)) return false;
        renderer.Apply(ops);
        return true;
    }

private:
    // Read one newline-terminated frame into `line`. Returns false at EOF with
    // nothing read; a final partial line (no trailing newline) is still
    // returned once so the last frame is not dropped.
    bool ReadLine() {
        line.clear();
        char ch;
        while (in.get(ch)) {
            if (ch == '\n') { TrimCR(); return true; }
            line.push_back(ch);
        }
        TrimCR();
        return !line.empty();
    }

    void TrimCR() {
        if (!line.empty() && line.back() == '\r') line.pop_back();
    }
};

}  // namespace born2flap::ui
