#include <windows.h>
#include <fstream>
#include <iostream>
#include <vector>
#include "BufferIO.h"

constexpr size_t kRelationGraphSize = 4144;
using Ctor = void(__fastcall*)(void*, const char*);
using Dtor = void(__fastcall*)(void*);
using ReadFromBuffer = int(__fastcall*)(void*, CBufferIO&, int);
using Initialize = int(__fastcall*)(void*, HWND*);
using GraphFn = void*(__fastcall*)(void*);

static FARPROC require(HMODULE module, const char* name) {
    FARPROC proc = GetProcAddress(module, name);
    if (!proc) std::cerr << "missing export: " << name << "\n";
    return proc;
}

static int I32(void* object, size_t offset) {
    return *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(object) + offset);
}

static float F32(void* object, size_t offset) {
    return *reinterpret_cast<float*>(reinterpret_cast<unsigned char*>(object) + offset);
}

static unsigned long long U64(void* object, size_t offset) {
    return *reinterpret_cast<unsigned long long*>(reinterpret_cast<unsigned char*>(object) + offset);
}

static void DumpTail(void* object, int type) {
    auto* bytes = reinterpret_cast<unsigned char*>(object);
    if (type == 14) {
        auto* text = reinterpret_cast<const wchar_t*>(U64(object, 0x30));
        std::wcerr << L"  script text=" << (text ? text : L"<null>") << L"\n";
    }
    std::cerr << "  vtable=" << std::hex << U64(object, 0)
              << " q30=" << U64(object, 0x30)
              << " q50=" << U64(object, 0x50)
              << " q58=" << U64(object, 0x58)
              << " q60=" << U64(object, 0x60)
              << " q68=" << U64(object, 0x68)
              << " q70=" << U64(object, 0x70)
              << " q78=" << U64(object, 0x78)
              << " q90=" << U64(object, 0x90)
              << " q98=" << U64(object, 0x98)
              << " qa0=" << U64(object, 0xa0)
              << " qa8=" << U64(object, 0xa8)
              << " qb0=" << U64(object, 0xb0)
              << " qb8=" << U64(object, 0xb8)
              << " qc0=" << U64(object, 0xc0)
              << " qc8=" << U64(object, 0xc8)
              << " qd0=" << U64(object, 0xd0)
              << " qd8=" << U64(object, 0xd8)
              << " qe0=" << U64(object, 0xe0)
              << " qe8=" << U64(object, 0xe8)
              << " qf0=" << U64(object, 0xf0)
              << " qf8=" << U64(object, 0xf8)
              << " q100=" << U64(object, 0x100)
              << " q108=" << U64(object, 0x108)
              << " q110=" << U64(object, 0x110)
              << " q118=" << U64(object, 0x118)
              << " q120=" << U64(object, 0x120)
              << std::dec << "\n";
    if (type == 4) {
        auto* vtable = reinterpret_cast<unsigned char*>(U64(object, 0));
        std::cerr << "  hub vtable:";
        for (size_t i = 0; i < 48; ++i) {
            if (i % 6 == 0) std::cerr << "\n    ";
            std::cerr << std::hex << i << "=" << U64(vtable, i * 8) << " ";
        }
        std::cerr << std::dec << "\n";
        for (size_t off : {size_t(0x100), size_t(0x110)}) {
            auto* target = reinterpret_cast<unsigned char*>(U64(object, off));
            if (target) {
                std::cerr << "  ptr[0x" << std::hex << off << "]=" << static_cast<void*>(target)
                          << " type=" << std::dec << I32(target, 8)
                          << " id=" << I32(target, 12)
                          << " prev=" << I32(target, 0x80)
                          << " next=" << I32(target, 0x84) << "\n";
                std::cerr << "    target bytes:";
                for (size_t i = 0; i < 0x40; ++i) {
                    if (i % 16 == 0) std::cerr << "\n      ";
                    std::cerr << std::hex << (static_cast<unsigned>(target[i]) >> 4)
                              << (static_cast<unsigned>(target[i]) & 0xf);
                }
                std::cerr << std::dec << "\n";
            }
        }
        std::cerr << "  hub dwords[0x118..0x127]=" << std::hex
                  << I32(object, 0x118) << "," << I32(object, 0x11c)
                  << "," << I32(object, 0x120) << "," << I32(object, 0x124)
                  << std::dec << "\n";
    }
    if (type == 4 || type == 14) {
        std::cerr << "  bytes[0xd0..0x11f]:";
        for (size_t i = 0xd0; i < 0x120; ++i) {
            if ((i - 0xd0) % 16 == 0) std::cerr << "\n    ";
            std::cerr << std::hex << (static_cast<unsigned>(bytes[i]) >> 4)
                      << (static_cast<unsigned>(bytes[i]) & 0xf);
        }
        std::cerr << std::dec << "\n";
    }
}

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: inspect_rg <graph.rg> <RelaGraph.dll>\n";
        return 2;
    }
    std::ifstream input(argv[1], std::ios::binary);
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(input)), {});
    HMODULE module = LoadLibraryA(argv[2]);
    if (!module) return 3;
    auto ctor = reinterpret_cast<Ctor>(require(module, "??0CRelationGraph@@QEAA@PEBD@Z"));
    auto dtor = reinterpret_cast<Dtor>(require(module, "??1CRelationGraph@@UEAA@XZ"));
    auto read = reinterpret_cast<ReadFromBuffer>(require(module, "?ReadFromBuffer@CRelationGraph@@QEAAHAEAVCBufferIO@@H@Z"));
    auto initialize = reinterpret_cast<Initialize>(require(module, "?Initialize@CRelationGraph@@QEAAHPEAUHWND__@@@Z"));
    auto graphFn = reinterpret_cast<GraphFn>(require(module, "?Graph@CRelationGraph@@QEAAPEAVCRgComplexObject@@XZ"));
    if (!ctor || !dtor || !read || !initialize || !graphFn) return 4;

    alignas(16) unsigned char graph[kRelationGraphSize] = {};
    ctor(graph, argv[1]);
    HWND host = nullptr;
    std::cerr << "initialize=" << initialize(graph, &host) << "\n";
    CBufferIO source;
    source.SetBuffer(bytes.data(), static_cast<int64_t>(bytes.size()));
    std::cerr << "read=" << read(graph, source, 0) << " bytes=" << bytes.size() << "\n";

    auto* root = reinterpret_cast<unsigned char*>(graphFn(graph));
    std::cerr << "graph=" << static_cast<void*>(graph) << " root=" << static_cast<void*>(root) << "\n";
    if (root) {
        std::cerr << "root type=" << I32(root, 8) << " id=" << I32(root, 12)
                  << " prev=" << I32(root, 128) << " next=" << I32(root, 132)
                  << " father=" << I32(root, 136) << "\n";
        // MSVC std::list keeps a circular sentinel node in the first field;
        // the second field is the size, not a tail pointer.
        // Release RelaGraph's CRgObject base is 0xe0 bytes (the factory
        // allocates 0xe0 for boot), so CRgComplexObject::m_objects starts at
        // +0xe0.  The Debug PDB layout is 0xf0; this inspector targets the
        // Release DLL passed on the command line.
        constexpr size_t kObjectsOffset = 0xe0;
        auto* listSentinel = *reinterpret_cast<unsigned char**>(root + kObjectsOffset);
        auto* listHead = listSentinel ? *reinterpret_cast<unsigned char**>(listSentinel) : nullptr;
        std::cerr << "list sentinel=" << static_cast<void*>(listSentinel)
                  << " first=" << static_cast<void*>(listHead)
                  << " size=" << *reinterpret_cast<size_t*>(root + kObjectsOffset + 8) << "\n";
        int index = 0;
        for (auto* link = listHead; link && link != listSentinel && index < 64;
             link = *reinterpret_cast<unsigned char**>(link), ++index) {
            auto* object = *reinterpret_cast<unsigned char**>(link + 16);
            std::cerr << "object[" << index << "]=" << static_cast<void*>(object)
                      << " type=" << I32(object, 8) << " id=" << I32(object, 12)
                      << " prim=" << I32(object, 0x10) << " flag=0x" << std::hex << I32(object, 0x14) << std::dec
                      << " status=" << I32(object, 0x38)
                      << " prev=" << I32(object, 128) << " next=" << I32(object, 132)
                      << " father=" << I32(object, 136)
                      << " matrix=" << F32(object, 0x18) << "," << F32(object, 0x1c)
                      << "," << F32(object, 0x20) << "," << F32(object, 0x24)
                      << "," << F32(object, 0x28) << "," << F32(object, 0x2c)
                      << "," << F32(object, 0x30) << "," << F32(object, 0x34)
                      << " matrixTail=" << F32(object, 0x38) << "," << F32(object, 0x3c)
                      << " dims=" << F32(object, 0x40) << "," << F32(object, 0x44)
                      << " strdims=" << F32(object, 0x44) << "," << F32(object, 0x48)
                      << " direction=" << I32(object, 0xa8)
                      << "\n";
            DumpTail(object, I32(object, 8));
        }
    }
    dtor(graph);
    FreeLibrary(module);
    return 0;
}
