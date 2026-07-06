/// AmbiTap: target-independent ambisonics library
/// Generic worker-thread rebuild + wait-free publish utility.
/// Timothy Place
/// Copyright 2025-2026 Timothy Place.

#ifndef AMBITAP_DSP_UTIL_ASYNC_REBUILDER_H
#define AMBITAP_DSP_UTIL_ASYNC_REBUILDER_H

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

#include "rt_published.h"

namespace ambitap::dsp {

    /// Owns a worker thread that rebuilds an expensive product (decoder matrix,
    /// SH rotation matrix, HRTF projection, …) off the audio thread and
    /// publishes it through rt_published — the audio thread reads the latest
    /// product via a wait-free read_lock() and never locks, allocates, or
    /// frees; the worker performs all deallocation.
    ///
    /// Usage: construct with a build callback (runs on the worker thread; reads
    /// whatever configuration the owner snapshots for it), call submit() from
    /// any control thread after changing configuration, and bracket audio-path
    /// access in read_lock() (nullptr until the first build completes). Use
    /// peek()/has_value() from control/UI threads.
    ///
    /// Coalescing: multiple submit() calls while a build is running result in
    /// one further build, not one per call.
    ///
    /// If the build callback returns nullptr the previous product is kept
    /// (used e.g. for "invalid configuration, keep last good matrix").
    ///
    /// The owner must declare its async_rebuilder member *after* every member
    /// the build callback reads, so the worker is joined (in ~async_rebuilder)
    /// before those members are destroyed.
    template <typename Product>
    class async_rebuilder {
      public:
        using build_fn   = std::function<std::shared_ptr<Product>()>;
        using publish_fn = std::function<void()>;
        using read_guard = typename rt_published<Product>::read_guard;

        /// @param build       Builds a fresh product. Runs on the worker thread.
        /// @param on_publish  Optional; runs on the worker thread after each
        ///                    successful publish (e.g. to schedule a UI emit).
        ///                    wait_for_settling() waits for it too.
        explicit async_rebuilder(build_fn build, publish_fn on_publish = {})
            : m_build(std::move(build))
            , m_on_publish(std::move(on_publish))
            , m_worker([this] { worker_loop(); }) {}

        ~async_rebuilder() {
            {
                std::lock_guard<std::mutex> lock(m_mtx);
                m_exiting = true;
            }
            m_worker_cv.notify_one();
            if (m_worker.joinable()) m_worker.join();
        }

        async_rebuilder(const async_rebuilder&)            = delete;
        async_rebuilder& operator=(const async_rebuilder&) = delete;

        /// Request a rebuild. Wakes the worker; safe from any control thread.
        void submit() {
            {
                std::lock_guard<std::mutex> lock(m_mtx);
                ++m_pending_seq;
            }
            m_worker_cv.notify_one();
        }

        /// Enter a wait-free read region on the audio thread; get() yields the
        /// latest product (nullptr before the first build) and stays valid for
        /// the guard's lifetime. Exactly one reader thread; do not nest guards.
        read_guard read_lock() const { return m_published.read_lock(); }

        /// Latest product as a shared_ptr, from any thread EXCEPT the
        /// real-time path (briefly takes a mutex the audio thread never
        /// touches). For UI/control/test use.
        std::shared_ptr<Product> peek() const { return m_published.peek(); }

        /// True once a product has been published. Wait-free, any thread.
        bool has_value() const { return m_published.has_value(); }

        /// Block until every submit() issued so far has been built, published,
        /// and had its on_publish callback run. For tests and offline use; the
        /// audio path never calls this.
        void wait_for_settling() {
            std::unique_lock<std::mutex> lock(m_mtx);
            m_done_cv.wait(lock, [this] { return m_done_seq >= m_pending_seq; });
        }

        /// Submit a rebuild and block until it (and any previously pending
        /// rebuilds) have been published. The build itself still runs on the
        /// worker thread, not the calling thread. Offline/test use.
        void rebuild_synchronously() {
            submit();
            wait_for_settling();
        }

      private:
        void worker_loop() {
            std::uint64_t last_seen = 0;
            while (true) {
                std::uint64_t seq_to_run;
                {
                    std::unique_lock<std::mutex> lock(m_mtx);
                    m_worker_cv.wait(lock, [this, &last_seen] { return m_exiting || m_pending_seq > last_seen; });
                    if (m_exiting) return;
                    seq_to_run = m_pending_seq;
                }

                auto       fresh     = m_build();
                const bool published = (fresh != nullptr);
                if (published) {
                    m_published.publish(std::move(fresh)); // may block for grace
                    if (m_on_publish) m_on_publish();
                }

                last_seen = seq_to_run;
                {
                    std::lock_guard<std::mutex> lock(m_mtx);
                    m_done_seq = seq_to_run;
                }
                m_done_cv.notify_all();
            }
        }

        build_fn   m_build;
        publish_fn m_on_publish;

        rt_published<Product> m_published;

        std::mutex              m_mtx;
        std::condition_variable m_worker_cv;
        std::condition_variable m_done_cv;
        std::uint64_t           m_pending_seq{0}; // guarded by m_mtx
        std::uint64_t           m_done_seq{0};    // guarded by m_mtx
        bool                    m_exiting{false}; // guarded by m_mtx

        std::thread m_worker; // last member: joins before anything else dies
    };

} // namespace ambitap::dsp

#endif // AMBITAP_DSP_UTIL_ASYNC_REBUILDER_H
