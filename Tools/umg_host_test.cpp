// umg_host_test.cpp — headless proof of the C++ UMG host core.
//
// Compiles with plain clang++ (no Unreal engine needed):
//   clang++ -std=c++17 -I../Unreal/Born2Flap/Source/Born2Flap/UI umg_host_test.cpp -o umg_host_test
//
// It feeds the EXACT JSON the Ruby Brain emits (Brain/lib/born2flap/ui/wire.rb),
// parses it with Born2FlapUiOps.h, applies it with the host-agnostic FRenderer,
// and asserts the resulting widget tree + material params. The FakeHost mirrors
// what UBorn2FlapUIRenderer does with UWidgets — same contract, no engine.

#include "Born2FlapUiOps.h"
#include "Born2FlapUiTree.h"

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <string>
#include <vector>

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
        } else if (root == w) {
            root = nullptr;
        }
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

// The canonical op stream the Ruby Brain emits for a HUD mount + a text update
// + a speed-blur effect (captured verbatim from `Wire.dump_ops`).
static const char* kRubyWire = R"JSON([
{"op":"create_instance","path":[],"node":{"type":"Overlay","props":{"id":"HUD"},"children":[
  {"type":"VerticalBox","props":{},"children":[
    {"type":"TextBlock","props":{"bind":"title","value":"BORN-2-FLAP"},"children":[]},
    {"type":"Slider","props":{"bind":"throttle","min":0,"max":100},"children":[]},
    {"type":"Button","props":{"onPress":"toggle_flap"},"children":[
      {"type":"TextBlock","props":{"value":"FLAP"},"children":[]}
    ]}
  ]}
]}},
{"op":"update_props","path":[0,0],"props":{"value":"BORN-2-FLAP II"}},
{"op":"set_material_params","path":[0,1],"params":{"BlurRadius":0.6,"BlurStrength":0.6}}
])JSON";

int main() {
    // 1. Parse the Ruby wire.
    std::vector<FOp> ops;
    CHECK(ParseOps(kRubyWire, ops), "ParseOps returned false");
    CHECK(ops.size() == 3, "expected 3 ops");

    // 2. Apply via the host-agnostic renderer.
    FakeHost host;
    FRenderer<FakeHost> renderer(host);
    renderer.Apply(ops);

    // 3. Assert the widget tree.
    CHECK(host.root != nullptr, "root was not mounted");
    if (!host.root) return 1;

    CHECK_STR(host.root->type, "Overlay", "root type");
    CHECK_STR(host.root->props["id"].AsString(), "HUD", "root id");
    CHECK(host.root->children.size() == 1, "root has one child (VerticalBox)");

    FWidget* vbox = host.root->children[0];
    CHECK_STR(vbox->type, "VerticalBox", "vbox type");
    CHECK(vbox->children.size() == 3, "vbox has 3 children");

    FWidget* text = vbox->children[0];
    CHECK_STR(text->type, "TextBlock", "text type");
    CHECK_STR(text->props["value"].AsString(), "BORN-2-FLAP II", "text value after update_props");

    FWidget* slider = vbox->children[1];
    CHECK_STR(slider->type, "Slider", "slider type");
    CHECK_NEAR(slider->props["min"].AsNumber(), 0.0, "slider min");
    CHECK_NEAR(slider->props["max"].AsNumber(), 100.0, "slider max");
    CHECK_NEAR(slider->material["BlurRadius"], 0.6, "speed_blur BlurRadius");
    CHECK_NEAR(slider->material["BlurStrength"], 0.6, "speed_blur BlurStrength");

    FWidget* button = vbox->children[2];
    CHECK_STR(button->type, "Button", "button type");
    CHECK_STR(button->props["onPress"].AsString(), "toggle_flap", "button onPress");
    CHECK(button->children.size() == 1 && button->children[0]->props["value"].AsString() == "FLAP",
          "button child text FLAP");

    // 4. Removal: drop the slider (child index 1 of the vbox).
    static const char* kRemove = R"JSON([{"op":"remove_child","path":[0],"index":1}])JSON";
    ops.clear();
    CHECK(ParseOps(kRemove, ops), "remove parse");
    renderer.Apply(ops);
    CHECK(vbox->children.size() == 2, "vbox has 2 children after remove_child");
    CHECK_STR(vbox->children[0]->props["value"].AsString(), "BORN-2-FLAP II", "text survives remove");
    CHECK_STR(vbox->children[1]->type, "Button", "button shifted to index 1");

    // 5. Insert a new child back at index 1.
    static const char* kInsert = R"JSON([
      {"op":"append_child","path":[0],"index":1,"node":{"type":"ProgressBar","props":{"value":0.75},"children":[]}}
    ])JSON";
    ops.clear();
    CHECK(ParseOps(kInsert, ops), "insert parse");
    renderer.Apply(ops);
    CHECK(vbox->children.size() == 3, "vbox has 3 children after insert");
    CHECK_STR(vbox->children[1]->type, "ProgressBar", "inserted at index 1");
    CHECK_NEAR(vbox->children[1]->props["value"].AsNumber(), 0.75, "progress value");

    // 6. Unmount the whole tree.
    static const char* kUnmount = R"JSON([{"op":"remove_instance","path":[]}])JSON";
    ops.clear();
    CHECK(ParseOps(kUnmount, ops), "unmount parse");
    renderer.Apply(ops);
    CHECK(host.root == nullptr, "root unmounted");
    CHECK(renderer.widgets.empty(), "renderer index empty after unmount");

    if (failures == 0) {
        std::printf("OK: umg_host_test — %s\n", "all assertions passed");
        return 0;
    }
    std::fprintf(stderr, "%d failure(s)\n", failures);
    return 1;
}