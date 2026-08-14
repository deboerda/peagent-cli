// Build a RelaGraph from an atomic-operation plan.
// The plan is deliberately simple so the CLI can remain stdlib-only:
//   node|<id>|<type>|<x>|<y>|<text>
//   link|<from>|<to>
// The .rg file is always produced through RelaGraph's native writer.
#include <windows.h>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include "BufferIO.h"

constexpr size_t kGraphSize = 4144;
enum { CURVE = 2, PROGRAM = 6, HUB = 4, BOOT = 11, SCRIPT = 14, BUTTON = 19 };

struct NodeDef { int id; std::string type; float x, y; std::string text; };
struct LinkDef { int from, to; };
struct Plan { std::vector<NodeDef> nodes; std::vector<LinkDef> links; };

using Ctor = void* (__fastcall*)(void*, const char*);
using Dtor = void (__fastcall*)(void*);
using Write = int (__fastcall*)(void*, CBufferIO&);
using Initialize = int (__fastcall*)(void*, HWND*);
using GraphFn = void* (__fastcall*)(void*);
using AddObject = void (__fastcall*)(void*, void*);
using OperatorFn = void* (__fastcall*)(void*);
using CreateObject = void* (__fastcall*)(void*, int, const wchar_t*, int, int);
using BuildFromId = int (__fastcall*)(void*);
using CreateByType = void* (__fastcall*)(int, void*);
using SetText = void (__fastcall*)(void*, const wchar_t*);
using LinkFrom = void (__fastcall*)(void*, void*);
using Translate = void (__fastcall*)(void*, float, float);
using AlignToGrid = void (__fastcall*)(void*);
using Allocate = void* (__fastcall*)(size_t);

static std::vector<std::string> Split(const std::string& line) {
    std::vector<std::string> out; std::string part; std::stringstream ss(line);
    while (std::getline(ss, part, '|')) out.push_back(part);
    return out;
}

static bool ReadPlan(const char* path, Plan& plan, std::string& error) {
    std::ifstream input(path);
    if (!input) { error = "cannot open plan"; return false; }
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto fields = Split(line);
        try {
            if (fields.size() >= 5 && fields[0] == "node") {
                plan.nodes.push_back({std::stoi(fields[1]), fields[2], std::stof(fields[3]), std::stof(fields[4]), fields.size() >= 6 ? fields[5] : ""});
            } else if (fields.size() >= 3 && fields[0] == "link") {
                plan.links.push_back({std::stoi(fields[1]), std::stoi(fields[2])});
            } else { error = "invalid plan line: " + line; return false; }
        } catch (...) { error = "invalid plan values: " + line; return false; }
    }
    return true;
}

static bool Named(const char* path, const char* name) {
    const char* slash = strrchr(path, '\\'); slash = slash ? slash + 1 : path;
    return _stricmp(slash, name) == 0;
}

static std::wstring Wide(const std::string& value) {
    if (value.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, value.data(), (int)value.size(), nullptr, 0);
    std::wstring result((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), (int)value.size(), &result[0], n);
    return result;
}

static int TypeOf(const std::string& type) {
    if (type == "boot") return BOOT;
    if (type == "hub") return HUB;
    if (type == "script") return SCRIPT;
    if (type == "program") return PROGRAM;
    if (type == "curve") return CURVE;
    if (type == "button") return BUTTON;
    return 0;
}

static void SetId(void* object, int id) {
    *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(object) + 0x0c) = id;
}
static int GetId(void* object) {
    return *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(object) + 0x0c);
}
static void SetPrevNext(void* object, int prev, int next) {
    auto* p = reinterpret_cast<unsigned char*>(object);
    *reinterpret_cast<int*>(p + 0x80) = prev;
    *reinterpret_cast<int*>(p + 0x84) = next;
}
static void SetFather(void* object) {
    *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(object) + 0x88) = 1;
}
static void SetDimensions(void* object, float width, float height) {
    auto* p = reinterpret_cast<unsigned char*>(object);
    *reinterpret_cast<float*>(p + 0x3c) = width;
    *reinterpret_cast<float*>(p + 0x40) = height;
}
static void SetHubCache(void* object, int prev, int next, Allocate allocate) {
    auto* p = reinterpret_cast<unsigned char*>(object);
    auto* prevArray = *reinterpret_cast<unsigned char**>(p + 0x100);
    auto* nextArray = *reinterpret_cast<unsigned char**>(p + 0x110);
    if (!prevArray) { prevArray = (unsigned char*)allocate(4); *reinterpret_cast<unsigned char**>(p + 0x100) = prevArray; }
    if (!nextArray) { nextArray = (unsigned char*)allocate(4); *reinterpret_cast<unsigned char**>(p + 0x110) = nextArray; }
    *reinterpret_cast<int*>(prevArray) = prev;
    *reinterpret_cast<int*>(nextArray) = next;
    *reinterpret_cast<int*>(p + 0x108) = 1;
    *reinterpret_cast<int*>(p + 0x118) = 1;
}

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "usage: build_graph_from_plan <output.rg> <RelaGraph.dll> <plan>\n";
        return 2;
    }
    if (!Named(argv[2], "RelaGraph.dll")) {
        std::cerr << "only the verified Release RelaGraph.dll is supported\n";
        return 3;
    }
    Plan plan; std::string error;
    if (!ReadPlan(argv[3], plan, error)) { std::cerr << error << "\n"; return 4; }
    HMODULE module = LoadLibraryA(argv[2]);
    if (!module) { std::cerr << "LoadLibrary failed\n"; return 5; }
    auto symbol = [&](const char* name) { return GetProcAddress(module, name); };
    auto ctor = (Ctor)symbol("??0CRelationGraph@@QEAA@PEBD@Z");
    auto dtor = (Dtor)symbol("??1CRelationGraph@@UEAA@XZ");
    auto write = (Write)symbol("?WriteToBuffer@CRelationGraph@@QEAAHAEAVCBufferIO@@@Z");
    auto initialize = (Initialize)symbol("?Initialize@CRelationGraph@@QEAAHPEAUHWND__@@@Z");
    auto graphFn = (GraphFn)symbol("?Graph@CRelationGraph@@QEAAPEAVCRgComplexObject@@XZ");
    auto addObject = (AddObject)symbol("?AddObject@CRgComplexObject@@QEAAXPEAVCRgObject@@@Z");
    auto operatorFn = (OperatorFn)symbol("?Operator@CRelationGraph@@QEAAPEAVCRgOperator@@XZ");
    auto createObject = (CreateObject)symbol("?CreateObject@CRgOperator@@QEAAPEAVCRgObject@@W4OpType@@PEB_WHH@Z");
    auto buildFromId = (BuildFromId)symbol("?BuildFromID@CRelationGraph@@QEAAHXZ");
    if (!ctor || !dtor || !write || !initialize || !graphFn || !addObject || !operatorFn || !createObject || !buildFromId) {
        std::cerr << "RelaGraph export contract is incomplete\n"; FreeLibrary(module); return 6;
    }
    uintptr_t base = (uintptr_t)module;
    auto createByType = (CreateByType)(base + 0x2ba40);
    auto setText = (SetText)(base + 0x299e0);
    auto linkFrom = (LinkFrom)(base + 0x83f0);
    auto translate = (Translate)(base + 0x29a90);
    auto align = (AlignToGrid)(base + 0x29b70);
    auto allocate = (Allocate)(base + 0x383e0);

    alignas(16) unsigned char storage[kGraphSize] = {};
    void* graph = ctor(storage, argv[1]);
    HWND host = nullptr; if (!initialize(graph, &host)) { dtor(graph); FreeLibrary(module); return 7; }
    void* root = graphFn(graph); if (!root) { dtor(graph); FreeLibrary(module); return 8; }

    std::map<int, NodeDef> defs;
    int nextId = 2;
    for (const auto& node : plan.nodes) { defs[node.id] = node; if (node.id + 1 > nextId) nextId = node.id + 1; }
    for (const auto& link : plan.links) {
        if (!defs.count(link.from) || !defs.count(link.to)) { std::cerr << "link references unknown node\n"; dtor(graph); FreeLibrary(module); return 9; }
    }
    std::vector<NodeDef> all = plan.nodes;
    int curveBase = nextId;
    for (const auto& link : plan.links) {
        NodeDef curve{nextId++, "curve", (defs[link.from].x + defs[link.to].x) / 2.0f, (defs[link.from].y + defs[link.to].y) / 2.0f, ""};
        all.push_back(curve);
    }
    std::map<int, void*> objects;
    for (const auto& node : all) {
        int type = TypeOf(node.type); if (!type) { std::cerr << "unsupported node type: " << node.type << "\n"; dtor(graph); FreeLibrary(module); return 10; }
        void* object = nullptr;
        if (type == HUB) {
            void* op = operatorFn(graph);
            object = op ? createObject(op, 6, L"", (int)node.x, (int)node.y) : nullptr;
            if (!object) object = createByType(type, graph);
        } else object = createByType(type, graph);
        if (!object) { std::cerr << "failed to create node " << node.id << "\n"; dtor(graph); FreeLibrary(module); return 11; }
        SetId(object, node.id); SetFather(object); translate(object, node.x, node.y); align(object);
        if (!node.text.empty()) setText(object, Wide(node.text).c_str());
        if (type == HUB) SetDimensions(object, 0.0f, 30.375f);
        if (type == CURVE) SetDimensions(object, 0.0f, 30.375f);
        if (type == SCRIPT) { SetDimensions(object, 151.875f, 30.375f); *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(object) + 0x4f8) = 1; }
        if (type == PROGRAM) SetDimensions(object, 114.069f, 31.875f);
        addObject(root, object); objects[node.id] = object;
    }

    for (const auto& node : all) SetPrevNext(objects[node.id], 0, 0);
    std::map<int, std::pair<int,int>> hubEndpoints;
    for (size_t i = 0; i < plan.links.size(); ++i) {
        const auto& link = plan.links[i]; int curveId = curveBase + (int)i;
        void* source = objects[link.from], *target = objects[link.to], *curve = objects[curveId];
        SetPrevNext(curve, link.from, link.to);
        auto sourceNext = *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(source) + 0x84);
        auto targetPrev = *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(target) + 0x80);
        if (sourceNext == 0) SetPrevNext(source, *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(source) + 0x80), link.to);
        if (targetPrev == 0 && defs[link.to].type != "hub") SetPrevNext(target, link.from, *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(target) + 0x84));
        linkFrom(target, source);
        if (defs[link.from].type == "hub") hubEndpoints[link.from].second = link.to;
        if (defs[link.to].type == "hub") hubEndpoints[link.to].first = link.from;
    }
    for (const auto& item : hubEndpoints) SetHubCache(objects[item.first], item.second.first, item.second.second, allocate);
    if (!buildFromId(graph)) { std::cerr << "BuildFromID failed\n"; dtor(graph); FreeLibrary(module); return 12; }
    for (const auto& item : hubEndpoints) SetHubCache(objects[item.first], item.second.first, item.second.second, allocate);
    CBufferIO output; int result = write(graph, output); if (!result || output.GetBufferCount() <= 0) { dtor(graph); FreeLibrary(module); return 13; }
    std::vector<unsigned char> bytes((size_t)output.GetBufferCount()); output.GetBuffer(bytes.data());
    std::ofstream file(argv[1], std::ios::binary | std::ios::trunc); file.write((const char*)bytes.data(), bytes.size()); file.close();
    std::cout << "nodes=" << all.size() << " links=" << plan.links.size() << " bytes=" << bytes.size() << "\n";
    dtor(graph); FreeLibrary(module); ExitProcess(0); return 0;
}
