// umg_transport_test.cpp — headless proof of the NDJSON transport bridge.
//
//   clang++ -std=c++17 -I../Unreal/Born2Flap/Source/Born2Flap/UI umg_transport_test.cpp -o umg_transport_test
//
// Reads newline-delimited JSON op frames (one Wire.dump_ops array per line,
// the Ruby Transport's framing) and applies them via the host-agnostic
// FRenderer. Default: an embedded self-check. With a file argument (or `-` for
// stdin) it consumes REAL Ruby bytes — e.g.:
//   ruby ../Brain/bin/transport_demo > /tmp/ops.ndjson
//   ./umg_transport_test /tmp/ops.ndjson
// The final widget tree is asserted identically for both modes.

#include "Born2FlapUiOps.h"
#include "Born2FlapUiTree.h"
#include "Born2FlapTransport.h"

#include <cstdio>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <map>

using namespace born2flap::ui;

// ---- fake host (mirror of the UMG host, minus the engine) -------------------
struct FWidget {
    std::string type;
    FProps props;
    std::map<std::string, double> material;
    std::vector<FWidget*> children;
    FWidget* parent = nullptr;
};

struct FakeHost {
    using Widget = FWidget*;
    FWidget* root = nullptr;

    static void Destroy(FWidget* w) {
        for (FWidget* c : w->children) Destroy(c);
        delete w;
    }

    FWidget* CreateInstance(const std::string& type) { return new FWidget{type, {}, {}, {}, nullptr}; }

    void RemoveInstance(FWidget* w) {
        if (w->parent) {
            auto& v = w->parent->children;
            for (size_t i = 0; i < v.size(); ++i) if (v[i] == w) v.erase(v.begin() + (int)i);
        } else if (root == w) root = nullptr;
        Destroy(w);
    }

    void SetRoot(FWidget* w) { root = w; }

    void AppendChild(FWidget* parent, int index, FWidget* child) {
        parent->children.insert(parent->children.begin() + index, child);
        child->parent = parent;
    }

    void RemoveChild(FWidget* parent, int index) {
        FWidget* w = parent->children[index];
        parent->children.erase(parent->children.begin() + index);
        Destroy(w);
    }

    void UpdateProps(FWidget* w, const FProps& props) {
        for (auto& kv : props) w->props[kv.first] = kv.second;
    }

    void SetMaterialParams(FWidget* w, const FProps& params) {
        for (auto& kv : params) w->material[kv.first] = kv.second.AsNumber();
    }
};

// ---- assertion helpers ------------------------------------------------------
static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::fprintf(stderr, "FAIL: %s\n", msg); ++failures; } } while (0)
#define CHECK_STR(a, b, msg) do { std::string _got = (a); if (_got != (b)) { std::fprintf(stderr, "FAIL: %s (got '%s', want '%s')\n", msg, _got.c_str(), (b)); ++failures; } } while (0)
#define CHECK_NEAR(a, b, msg) do { if (std::fabs((a) - (b)) > 1e-6) { std::fprintf(stderr, "FAIL: %s (got %f, want %f)\n", msg, (double)(a), (double)(b)); ++failures; } } while (0)

static std::string ReadStream(std::istream& in) {
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static std::string ReadFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return ReadStream(f);
}

// Embedded self-check: three NDJSON frames (mount, retitle, speed-blur) —
// structurally identical to the Ruby `transport_demo` output's final state.
static const char* kEmbeddedFrames =
    "[{\"op\":\"create_instance\",\"path\":[],\"node\":{\"type\":\"Overlay\",\"props\":{\"id\":\"HUD\"},\"children\":["
    "{\"type\":\"VerticalBox\",\"props\":{},\"children\":["
    "{\"type\":\"TextBlock\",\"props\":{\"bind\":\"title\",\"value\":\"BORN-2-FLAP\"},\"children\":[]},"
    "{\"type\":\"Slider\",\"props\":{\"bind\":\"throttle\",\"min\":0,\"max\":100},\"children\":[]}]}]}}]\n"
    "[{\"op\":\"update_props\",\"path\":[0,0],\"props\":{\"value\":\"BORN-2-FLAP II\"}}]\n"
    "[{\"op\":\"set_material_params\",\"path\":[0,1],\"params\":{\"BlurRadius\":1.0,\"BlurStrength\":1.0}}]\n";

static void AssertTree(FakeHost& host, int frames) {
    CHECK(frames > 0, "no frames applied");
    CHECK(host.root != nullptr, "root was not mounted");
    if (!host.root) return;

    CHECK_STR(host.root->type, "Overlay", "root type");
    CHECK_STR(host.root->props["id"].AsString(), "HUD", "root id");
    CHECK(host.root->children.size() == 1, "root has one child (VerticalBox)");

    FWidget* vbox = host.root->children[0];
    CHECK_STR(vbox->type, "VerticalBox", "vbox type");
    CHECK(vbox->children.size() == 2, "vbox has 2 children");

    FWidget* text = vbox->children[0];
    CHECK_STR(text->type, "TextBlock", "text type");
    CHECK_STR(text->props["value"].AsString(), "BORN-2-FLAP II", "text value after update_props");

    FWidget* slider = vbox->children[1];
    CHECK_STR(slider->type, "Slider", "slider type");
    CHECK_NEAR(slider->props["min"].AsNumber(), 0.0, "slider min");
    CHECK_NEAR(slider->props["max"].AsNumber(), 100.0, "slider max");
    CHECK_NEAR(slider->material["BlurRadius"], 1.0, "speed_blur BlurRadius");
    CHECK_NEAR(slider->material["BlurStrength"], 1.0, "speed_blur BlurStrength");
}

int main(int argc, char** argv) {
    std::string src;
    const char* mode = "embedded";
    if (argc > 1) {
        if (std::string(argv[1]) == "-") { src = ReadStream(std::cin); mode = "stdin"; }
        else { src = ReadFile(argv[1]); mode = argv[1]; }
    } else {
        src = kEmbeddedFrames;
    }

    std::istringstream in(src);
    FTransport transport(in);
    FakeHost host;
    FRenderer<FakeHost> renderer(host);

    int frames = transport.Poll(renderer);
    std::printf("umg_transport_test [%s]: %d frame(s) applied\n", mode, frames);
    AssertTree(host, frames);

    if (failures == 0) {
        std::printf("OK: umg_transport_test — all assertions passed\n");
        return 0;
    }
    std::fprintf(stderr, "%d failure(s)\n", failures);
    return 1;
}