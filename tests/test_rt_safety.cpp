/// AmbiTap: target-independent ambisonics library
/// Real-time-safety tests: the audio-path process methods must neither
/// allocate nor free heap memory. Enforced with a replaced global
/// operator new/delete that counts activity while a thread-local guard is
/// armed (the SampleRateTap approach to making the RT claim testable).
/// Timothy Place
/// Copyright 2026 Timothy Place.

#include "ambitap/analysis/energy_vector.h"
#include "ambitap/analysis/soundfield_grid.h"
#include "ambitap/dsp/binaural_renderer.h"
#include "ambitap/dsp/decoder.h"
#include "ambitap/dsp/directional_loudness.h"
#include "ambitap/dsp/doppler.h"
#include "ambitap/dsp/encoder.h"
#include "ambitap/dsp/format_converter.h"
#include "ambitap/dsp/mirror.h"
#include "ambitap/dsp/rotator.h"
#include "ambitap/dsp/spatial_compressor.h"
#include "ambitap/dsp/virtual_mic.h"
#include "ambitap/math/geometry/layouts.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdlib>
#include <new>
#include <vector>

namespace {

std::atomic<long>       g_allocs {0};
std::atomic<long>       g_frees {0};
thread_local bool g_armed = false;

struct rt_guard {
    long a0, f0;
    rt_guard()
        : a0(g_allocs.load())
        , f0(g_frees.load()) {
        g_armed = true;
    }
    ~rt_guard() { g_armed = false; }
    long allocations() const { return g_allocs.load() - a0; }
    long frees() const { return g_frees.load() - f0; }
};

} // namespace

void* operator new(std::size_t size) {
    if (g_armed) g_allocs.fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void* operator new(std::size_t size, std::align_val_t al) {
    if (g_armed) g_allocs.fetch_add(1, std::memory_order_relaxed);
    void* p = nullptr;
    if (posix_memalign(&p, static_cast<std::size_t>(al), size ? size : 1) != 0) {
        throw std::bad_alloc();
    }
    return p;
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void* operator new[](std::size_t size, std::align_val_t al) { return ::operator new(size, al); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    if (g_armed) g_allocs.fetch_add(1, std::memory_order_relaxed);
    return std::malloc(size ? size : 1);
}
void* operator new[](std::size_t size, const std::nothrow_t& t) noexcept {
    return ::operator new(size, t);
}
void* operator new(std::size_t size, std::align_val_t al, const std::nothrow_t&) noexcept {
    if (g_armed) g_allocs.fetch_add(1, std::memory_order_relaxed);
    void* p = nullptr;
    return posix_memalign(&p, static_cast<std::size_t>(al), size ? size : 1) == 0 ? p : nullptr;
}
void* operator new[](std::size_t size, std::align_val_t al, const std::nothrow_t& t) noexcept {
    return ::operator new(size, al, t);
}
void operator delete(void* p) noexcept {
    if (g_armed) g_frees.fetch_add(1, std::memory_order_relaxed);
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }
void operator delete(void* p, std::align_val_t) noexcept { ::operator delete(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { ::operator delete(p); }
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { ::operator delete(p); }
void operator delete[](void* p, std::align_val_t) noexcept { ::operator delete(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { ::operator delete(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { ::operator delete(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { ::operator delete(p); }
void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept {
    ::operator delete(p);
}
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept {
    ::operator delete(p);
}

using namespace ambitap;

namespace {

struct planar {
    std::vector<std::vector<float>> bufs;
    std::vector<const float*>       in;
    std::vector<float*>             out;
    planar(size_t channels, size_t frames)
        : bufs(channels, std::vector<float>(frames, 0.25f)) {
        for (auto& b : bufs) {
            in.push_back(b.data());
            out.push_back(b.data()); // in-place where the processor allows it
        }
    }
};

} // namespace

TEST(RtSafety, EncoderMirrorMicLoudnessProcessIsAllocationFree) {
    constexpr int    order  = 5;
    constexpr size_t frames = 64;

    dsp::encoder              enc(order);
    dsp::mirror               mir(order);
    dsp::virtual_mic          mic(order);
    dsp::directional_loudness dl(order);
    dsp::format_converter     conv(3);

    enc.set_direction(0.3f, 0.1f); // mid-ramp exercises the smoothing path too
    mir.set_flip_lr(true);
    mic.set_direction(-0.5f, 0.2f);
    dl.set_gain(0.5f);

    planar             io(36, frames);
    planar             io3(16, frames);
    std::vector<float> mono(frames, 1.f);

    rt_guard guard;
    enc.process(mono.data(), io.out.data(), frames);
    mir.process(io.in.data(), io.out.data(), frames);
    mic.process(io.in.data(), mono.data(), frames);
    dl.process(io.in.data(), io.out.data(), frames);
    conv.process_frame(io3.in[0], const_cast<float*>(io3.in[1]));
    EXPECT_EQ(guard.allocations(), 0);
    EXPECT_EQ(guard.frees(), 0);
}

TEST(RtSafety, RotatorAndDecoderProcessIsAllocationFree) {
    constexpr int    order  = 3;
    constexpr size_t frames = 64;

    dsp::rotator rot(order);
    rot.set_rotation(0.4f, 0.1f, -0.2f);
    rot.wait_for_settling();

    dsp::decoder dec(order);
    dec.set_speakers(layouts::cube());
    dec.wait_for_settling();

    planar             hoa(rot.channels(), frames);
    planar             spk(8, frames);
    std::vector<float> frame_in(rot.channels(), 0.5f), frame_out(rot.channels());

    {
        rt_guard guard;
        // Includes the crossfade path (first call after publish) and the
        // settled path.
        for (int i = 0; i < 8; ++i) {
            rot.process(hoa.in.data(), hoa.out.data(), frames);
            dec.process(hoa.in.data(), spk.out.data(), 8, frames);
            rot.process_frame(frame_in.data(), frame_out.data());
            dec.process_frame(frame_in.data(), spk.out[0], 8);
        }
        EXPECT_EQ(guard.allocations(), 0);
        EXPECT_EQ(guard.frees(), 0);
    }

    // A publish while the audio thread holds no guard must not push frees
    // onto a subsequently-armed audio section either (the worker frees).
    dec.set_speakers(layouts::octagon());
    dec.wait_for_settling();
    {
        rt_guard guard;
        dec.process(hoa.in.data(), spk.out.data(), 8, frames);
        EXPECT_EQ(guard.allocations(), 0);
        EXPECT_EQ(guard.frees(), 0);
    }
}

TEST(RtSafety, StatefulProcessorsAreAllocationFree) {
    constexpr size_t frames = 64;

    dsp::doppler dop(1);
    dop.set_distance(2.f);
    dop.prepare(48000.f);

    dsp::spatial_compressor comp(1);
    comp.prepare(48000.f);

    analysis::energy_vector ev;
    ev.prepare(48000.f);

    analysis::soundfield_grid grid(1, 16);
    grid.prepare(48000.f);

    planar             io(4, frames);
    std::vector<float> ev_x(frames), ev_y(frames), ev_z(frames);
    float*             ev_out[3] = {ev_x.data(), ev_y.data(), ev_z.data()};

    rt_guard guard;
    dop.process(io.in.data(), io.out.data(), frames);
    comp.process(io.in.data(), io.out.data(), frames);
    ev.process(io.in.data(), ev_out, frames);
    grid.process(io.in.data(), frames);
    EXPECT_EQ(guard.allocations(), 0);
    EXPECT_EQ(guard.frees(), 0);
}

TEST(RtSafety, BinauralRendererProcessIsAllocationFree) {
    constexpr size_t block = 64;

    dsp::binaural_renderer bin(3);
    bin.prepare(block);
    bin.set_head_orientation(0.5f, 0.f, 0.f);
    bin.wait_for_settling();
    bin.set_volume(0.8f);

    planar             in(16, block);
    std::vector<float> left(block), right(block);

    rt_guard guard;
    for (int i = 0; i < 8; ++i) {
        bin.process(in.in.data(), left.data(), right.data(), block);
    }
    EXPECT_EQ(guard.allocations(), 0);
    EXPECT_EQ(guard.frees(), 0);
}
