/// AmbiTap: target-independent ambisonics library
/// Generic worker-thread rebuild + atomic publish utility.
/// Timothy Place
/// Copyright 2026 Timothy Place.

#ifndef AMBITAP_DSP_UTIL_ASYNC_REBUILDER_H
#define AMBITAP_DSP_UTIL_ASYNC_REBUILDER_H

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace ambitap::dsp {

    /// Owns a worker thread that rebuilds an expensive product (decoder matrix,
    /// SH rotation matrix, HRTF projection, …) off the audio thread and
    /// publishes it via an atomic shared_ptr swap.
    ///
    /// Usage: construct with a build callback (runs on the worker thread; reads
    /// whatever configuration the owner snapshots for it), call submit() from
    /// any control thread after changing configuration, and call load() from the
    /// audio thread — wait-free — to get the latest product (nullptr until the
    /// first build completes).
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

        /// @param build       Builds a fresh product. Runs on the worker thread.
        /// @param on_publish  Optional; runs on the worker thread after each
        ///                    successful publish (e.g. to schedule a UI emit).
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

        /// Latest published product, or nullptr before the first build.
        /// Wait-free; safe from the audio thread.
        ///
        /// Uses the shared_ptr atomic free functions rather than C++20's
        /// std::atomic<std::shared_ptr<T>> because Apple's libc++ does not yet
        /// implement the latter (P0718); switch once it lands everywhere.
        std::shared_ptr<Product> load() const {
            return std::atomic_load_explicit(&m_active, std::memory_order_acquire);
        }

        /// True once a product has been published. Cheaper than load() for
        /// fast-path checks.
        bool has_value() const { return m_has_value.load(std::memory_order_acquire); }

        /// Block until every submit() issued so far has been built. For tests
        /// and offline use; the audio path never calls this.
        void wait_for_settling() {
            std::unique_lock<std::mutex> lock(m_mtx);
            m_done_cv.wait(lock, [this] { return m_done_seq >= m_pending_seq; });
        }

        /// Build and publish inline on the calling thread (offline/test use).
        void rebuild_synchronously() {
            submit();
            wait_for_settling();
        }

      private:
        void worker_loop() {
            int last_seen = 0;
            while (true) {
                int seq_to_run;
                {
                    std::unique_lock<std::mutex> lock(m_mtx);
                    m_worker_cv.wait(lock, [this, &last_seen] {
                        return m_exiting || m_pending_seq > last_seen;
                    });
                    if (m_exiting) return;
                    seq_to_run = m_pending_seq;
                }

                auto       fresh     = m_build();
                const bool published = (fresh != nullptr);
                if (published) {
                    std::atomic_store_explicit(&m_active, std::move(fresh),
                                               std::memory_order_release);
                    m_has_value.store(true, std::memory_order_release);
                }

                last_seen = seq_to_run;
                {
                    std::lock_guard<std::mutex> lock(m_mtx);
                    m_done_seq = seq_to_run;
                }
                m_done_cv.notify_all();

                if (published && m_on_publish) m_on_publish();
            }
        }

        build_fn   m_build;
        publish_fn m_on_publish;

        std::shared_ptr<Product> m_active;
        std::atomic<bool>        m_has_value {false};

        std::mutex              m_mtx;
        std::condition_variable m_worker_cv;
        std::condition_variable m_done_cv;
        int                     m_pending_seq {0}; // guarded by m_mtx
        int                     m_done_seq {0};    // guarded by m_mtx
        bool                    m_exiting {false}; // guarded by m_mtx

        std::thread m_worker; // last member: joins before anything else dies
    };

} // namespace ambitap::dsp

#endif // AMBITAP_DSP_UTIL_ASYNC_REBUILDER_H
