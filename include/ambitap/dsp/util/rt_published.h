/// AmbiTap: target-independent ambisonics library
/// Wait-free single-reader publication of heap products (RCU-style).
/// Timothy Place
/// Copyright 2026 Timothy Place.

#ifndef AMBITAP_DSP_UTIL_RT_PUBLISHED_H
#define AMBITAP_DSP_UTIL_RT_PUBLISHED_H

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

namespace ambitap::dsp {

    /// Publishes a heap-allocated product from a builder thread to ONE
    /// real-time reader thread, with a genuinely wait-free read side.
    ///
    /// Why not the shared_ptr atomic free functions: they are lock-based on
    /// every mainstream standard library (a global mutex pool shared between
    /// reader and writer — priority inversion on the audio thread), and a
    /// reader's shared_ptr copy can end up freeing the product on the audio
    /// thread when it drops the last reference. This class avoids both: the
    /// reader touches only raw pointers and two relaxed/seq_cst atomic stores,
    /// and all frees happen on the publishing thread (or a peek() holder).
    ///
    /// Scheme (single-reader RCU):
    ///   - The reader brackets each access in a read_guard, which flips an
    ///     epoch counter odd (inside) / even (outside).
    ///   - publish() swaps the raw active pointer, then waits until the reader
    ///     is provably outside any region entered before the swap (epoch even,
    ///     or changed) before releasing the old product. The publisher may
    ///     therefore block for up to one audio callback — by design; the
    ///     reader never blocks, never allocates, never frees.
    ///
    /// Threading contract:
    ///   - read_lock()/read_guard: exactly ONE reader thread (the audio
    ///     thread), guards must not nest or overlap.
    ///   - publish(): any non-reader thread; concurrent publishers are
    ///     serialized internally.
    ///   - peek()/has_value(): any thread EXCEPT the real-time path — peek()
    ///     takes a mutex the reader never touches.
    template <typename Product> class rt_published {
      public:
        /// RAII read region for the reader thread. Wait-free: entry and exit
        /// are one atomic store each. get() may be called any number of times
        /// inside the region; the pointer stays valid until the guard dies.
        class read_guard {
            const rt_published* m_owner;

          public:
            explicit read_guard(const rt_published& owner)
                : m_owner(&owner) {
                const auto e = m_owner->m_epoch.load(std::memory_order_relaxed);
                // seq_cst so this store orders before the get() load below and
                // against publish()'s pointer-store/epoch-load pair.
                m_owner->m_epoch.store(e + 1, std::memory_order_seq_cst);
            }
            ~read_guard() {
                const auto e = m_owner->m_epoch.load(std::memory_order_relaxed);
                m_owner->m_epoch.store(e + 1, std::memory_order_release);
            }
            read_guard(const read_guard&)            = delete;
            read_guard& operator=(const read_guard&) = delete;

            /// Latest product, or nullptr before the first publish.
            Product* get() const { return m_owner->m_active.load(std::memory_order_seq_cst); }
        };

        rt_published() = default;

        /// Readers and publishers must be quiescent by now (owners join their
        /// worker in their destructor and audio must have stopped).
        ~rt_published() = default;

        rt_published(const rt_published&)            = delete;
        rt_published& operator=(const rt_published&) = delete;

        /// Enter a read region on the reader (audio) thread. Wait-free.
        read_guard read_lock() const { return read_guard(*this); }

        /// True once a product has been published. Any thread; wait-free.
        bool has_value() const { return m_has_value.load(std::memory_order_acquire); }

        /// Latest product as a shared_ptr. Any thread EXCEPT the real-time
        /// path (takes a mutex not shared with the reader). Holders may end up
        /// freeing the product when they drop the last reference — never on
        /// the reader thread, which never holds a reference.
        std::shared_ptr<Product> peek() const {
            std::lock_guard<std::mutex> lock(m_mtx);
            return m_latest;
        }

        /// Publish a new product and release the previous one once the reader
        /// can no longer be using it. Null products are ignored. May block for
        /// roughly one reader region (one audio callback).
        void publish(std::shared_ptr<Product> fresh) {
            if (!fresh) return;
            std::shared_ptr<Product> old;
            {
                std::lock_guard<std::mutex> lock(m_mtx);
                old      = std::move(m_latest);
                m_latest = std::move(fresh);
                m_active.store(m_latest.get(), std::memory_order_seq_cst);
            }
            m_has_value.store(true, std::memory_order_release);
            wait_for_grace();
            // `old` drops here: freed on this thread (or later by a peek() holder).
        }

      private:
        void wait_for_grace() const {
            const auto e = m_epoch.load(std::memory_order_seq_cst);
            if ((e & 1u) == 0)
                return; // reader outside: regions entered from
                        // now on will see the new pointer
            while (m_epoch.load(std::memory_order_acquire) == e) {
                std::this_thread::sleep_for(std::chrono::microseconds(50));
            }
        }

        mutable std::atomic<std::uint64_t> m_epoch {0};
        std::atomic<Product*>              m_active {nullptr};
        std::atomic<bool>                  m_has_value {false};

        mutable std::mutex       m_mtx;    // guards m_latest; never touched by the reader
        std::shared_ptr<Product> m_latest; // keep-alive + peek()
    };

} // namespace ambitap::dsp

#endif // AMBITAP_DSP_UTIL_RT_PUBLISHED_H
