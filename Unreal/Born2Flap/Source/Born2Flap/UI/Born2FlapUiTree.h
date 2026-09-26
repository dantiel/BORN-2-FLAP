#pragma once
// Born2FlapUiRenderer.h — the host-agnostic tree-mutation core.
//
// This mirrors React Native's split: the reconciler only *decides* what changed
// (the Ruby side), the HostConfig names the primitive ops (Born2FlapUiOps.h),
// and this template applies those ops to whatever native host implements the
// contract. The host is UMG in Unreal (Born2FlapUIRenderer) and a test fake in
// Tools/umg_host_test.cpp — the SAME Apply() logic runs in both.
//
// Host concept (duck-typed; `Widget` = Host::Widget):
//   Widget CreateInstance(const std::string& type)
//   void   RemoveInstance(Widget w)          // detach from parent + destroy subtree
//   void   SetRoot(Widget w)                 // attach top-level widget to viewport
//   void   AppendChild(Widget parent, int index, Widget child)
//   void   RemoveChild(Widget parent, int index)  // detach + destroy subtree
//   void   UpdateProps(Widget w, const FProps& props)
//   void   SetMaterialParams(Widget w, const FProps& params)

#include "Born2FlapUiOps.h"
#include <map>
#include <vector>

namespace born2flap::ui {

template <typename Host>
struct FRenderer {
    Host& host;
    std::map<FPath, typename Host::Widget> widgets;

    explicit FRenderer(Host& h) : host(h) {}

    void Apply(const std::vector<FOp>& ops) {
        for (const FOp& op : ops) {
            switch (op.op) {
                case EOp::CreateInstance:   Mount(op.path, op.node); break;
                case EOp::RemoveInstance:   Unmount(op.path); break;
                case EOp::AppendChild:      Append(op.path, op.index, op.node); break;
                case EOp::RemoveChild:      RemoveChildAt(op.path, op.index); break;
                case EOp::UpdateProps: {
                    auto it = widgets.find(op.path);
                    if (it != widgets.end()) host.UpdateProps(it->second, op.props);
                    break;
                }
                case EOp::SetMaterialParams: {
                    auto it = widgets.find(op.path);
                    if (it != widgets.end()) host.SetMaterialParams(it->second, op.props);
                    break;
                }
            }
        }
    }

private:
    // Mount a full subtree at `path`. If a widget already sits there (the
    // reconciler's "type changed" replace), unmount it first.
    void Mount(const FPath& path, const FNode& node) {
        if (widgets.count(path)) Unmount(path);
        auto w = host.CreateInstance(node.type);
        host.UpdateProps(w, node.props);
        widgets[path] = w;
        if (path.empty()) {
            host.SetRoot(w);
        } else {
            FPath parent = path; parent.pop_back();
            host.AppendChild(widgets.at(parent), path.back(), w);
        }
        for (size_t i = 0; i < node.children.size(); ++i) {
            FPath child = path; child.push_back((int)i);
            Mount(child, node.children[i]);
        }
    }

    // Insert a new child subtree at (parentPath, index).
    void Append(const FPath& parentPath, int index, const FNode& node) {
        FPath childPath = parentPath; childPath.push_back(index);
        if (widgets.count(childPath)) Unmount(childPath);
        auto parent = widgets.at(parentPath);
        auto w = host.CreateInstance(node.type);
        host.UpdateProps(w, node.props);
        host.AppendChild(parent, index, w);
        widgets[childPath] = w;
        for (size_t i = 0; i < node.children.size(); ++i) {
            FPath grand = childPath; grand.push_back((int)i);
            Mount(grand, node.children[i]);
        }
    }

    void RemoveChildAt(const FPath& parentPath, int index) {
        FPath childPath = parentPath; childPath.push_back(index);
        EraseSubtree(childPath);
        host.RemoveChild(widgets.at(parentPath), index);
    }

    void Unmount(const FPath& path) {
        auto it = widgets.find(path);
        if (it == widgets.end()) return;
        auto w = it->second;
        EraseSubtree(path);
        host.RemoveInstance(w);  // detach from parent + destroy subtree
    }

    // Remove `path` and every descendant path from the index.
    void EraseSubtree(const FPath& path) {
        for (auto it = widgets.begin(); it != widgets.end();) {
            if (IsPrefix(path, it->first)) it = widgets.erase(it);
            else ++it;
        }
    }

    static bool IsPrefix(const FPath& prefix, const FPath& path) {
        if (path.size() < prefix.size()) return false;
        for (size_t i = 0; i < prefix.size(); ++i) if (path[i] != prefix[i]) return false;
        return true;
    }
};

}  // namespace born2flap::ui
