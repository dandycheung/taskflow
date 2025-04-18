#pragma once

#include <cassert>
#include <atomic>
#include <optional>

#include "os.hpp"

namespace tf {

/**
 * A 'lockless' bounded multi-producer, multi-consumer queue
 *
 * Has the caveat that the queue can *appear* empty even if there are
 * returned items within it as a single thread can block progression
 * of the queue.
 */
template<typename T, size_t LogSize = 10>
class MPMC {

  constexpr static uint64_t BufferSize = 1ull << LogSize;
  constexpr static uint64_t BufferMask = (BufferSize - 1);
  
  static_assert((BufferSize >= 2) && ((BufferSize & (BufferSize - 1)) == 0));

public:

  /**
   * Constructs a bounded multi-producer, multi-consumer queue
   *
   * Note: Due to the algorithm used, buffer_size must be a power
   *       of two and must be greater than or equal to two.
   *
   * @param buffer_size Number of spaces available in the queue.
   */
  explicit MPMC() {
    for (size_t i = 0; i < _buffer.size(); i++) {
      _buffer[i].sequence.store(i, std::memory_order_relaxed);
    }
    _enqueue_pos.store(0, std::memory_order_relaxed);
    _dequeue_pos.store(0, std::memory_order_relaxed);
  }


  /**
   * Enqueues an item into the queue
   *
   * @param data Argument to place into the array
   * @return false if the queue was full (and enqueing failed),
   *         true otherwise
   */
  bool try_enqueue(T data) {
    Cell *cell;
    auto pos = _enqueue_pos.load(std::memory_order_relaxed);
    for (; ;) {
      cell = &_buffer[pos & BufferMask];
      auto seq = cell->sequence.load(std::memory_order_acquire);
      if (seq == pos) {
        if (_enqueue_pos.compare_exchange_weak(pos, pos + 1,
                                               std::memory_order_relaxed)) {
            break;
        }
      } else if (seq < pos) {
          return false;
      } else {
          pos = _enqueue_pos.load(std::memory_order_relaxed);
      }
    }

    cell->data = data;
    cell->sequence.store(pos + 1, std::memory_order_release);

    return true;
  }
  
  void enqueue(T data) {

    Cell *cell;
    auto pos = _enqueue_pos.load(std::memory_order_relaxed);

    for (; ;) {
      cell = &_buffer[pos & BufferMask];
      auto seq = cell->sequence.load(std::memory_order_acquire);
      if (seq == pos) {
        if (_enqueue_pos.compare_exchange_weak(pos, pos + 1,
                                               std::memory_order_relaxed)) {
            break;
        }
      }
      else {
        pos = _enqueue_pos.load(std::memory_order_relaxed);
      }
    }

    cell->data = data;
    cell->sequence.store(pos + 1, std::memory_order_release);
  }

  /**
   * Dequeues an item from the queue
   *
   * @param[out] data Reference to place item into
   * @return false if the queue was empty (and dequeuing failed),
   *         true if successful
   */
  std::optional<T> try_dequeue() {
    Cell *cell;
    auto pos = _dequeue_pos.load(std::memory_order_relaxed);
    for (; ;) {
      cell = &_buffer[pos & BufferMask];
      auto seq = cell->sequence.load(std::memory_order_acquire);
      if (seq == pos + 1) {
        if (_dequeue_pos.compare_exchange_weak(pos, pos + 1,
                                               std::memory_order_relaxed)) {
          break;
        }
      } else if (seq < (pos + 1)) {
        return std::nullopt;
      } else {
        pos = _dequeue_pos.load(std::memory_order_relaxed);
      }
    }

    T data = cell->data;
    cell->sequence.store(pos + BufferMask + 1, std::memory_order_release);

    return data;
  }

  bool empty() const {
    auto beg = _dequeue_pos.load(std::memory_order_relaxed);
    auto end = _enqueue_pos.load(std::memory_order_relaxed);
    return beg >= end;
  }

  size_t capacity() const {
    return BufferSize;
  }

private:

  struct Cell {
    T data;
    std::atomic<uint64_t> sequence;
  };

  //static const size_t cacheline_size = 64;

  alignas(2*TF_CACHELINE_SIZE) std::array<Cell, BufferSize> _buffer;
  alignas(2*TF_CACHELINE_SIZE) std::atomic<uint64_t> _enqueue_pos;
  alignas(2*TF_CACHELINE_SIZE) std::atomic<uint64_t> _dequeue_pos;
};

// ------------------------------------------------------------------------------------------------
// specialization for pointer type
// ------------------------------------------------------------------------------------------------

template<typename T, size_t LogSize>
class MPMC <T*, LogSize> {

  constexpr static uint64_t BufferSize = 1ull << LogSize;
  constexpr static uint64_t BufferMask = (BufferSize - 1);
  
  static_assert((BufferSize >= 2) && ((BufferSize & (BufferSize - 1)) == 0));

public:

  /**
   * Constructs a bounded multi-producer, multi-consumer queue
   *
   * Note: Due to the algorithm used, buffer_size must be a power
   *       of two and must be greater than or equal to two.
   *
   * @param buffer_size Number of spaces available in the queue.
   */
  explicit MPMC() {
    for (size_t i = 0; i < _buffer.size(); i++) {
      _buffer[i].sequence.store(i, std::memory_order_relaxed);
    }
    _enqueue_pos.store(0, std::memory_order_relaxed);
    _dequeue_pos.store(0, std::memory_order_relaxed);
  }


  /**
   * Enqueues an item into the queue
   *
   * @param data Argument to place into the array
   * @return false if the queue was full (and enqueing failed),
   *         true otherwise
   */
  bool try_enqueue(T* data) {
    Cell *cell;
    auto pos = _enqueue_pos.load(std::memory_order_relaxed);
    for (; ;) {
      cell = &_buffer[pos & BufferMask];
      auto seq = cell->sequence.load(std::memory_order_acquire);
      if (seq == pos) {
        if (_enqueue_pos.compare_exchange_weak(pos, pos + 1,
                                               std::memory_order_relaxed)) {
          break;
        }
      } else if (seq < pos) {
        return false;
      } else {
        pos = _enqueue_pos.load(std::memory_order_relaxed);
      }
    }

    cell->data = data;
    cell->sequence.store(pos + 1, std::memory_order_release);

    return true;
  }
  
  void enqueue(T* data) {

    Cell *cell;
    auto pos = _enqueue_pos.load(std::memory_order_relaxed);

    for (; ;) {
      cell = &_buffer[pos & BufferMask];
      auto seq = cell->sequence.load(std::memory_order_acquire);
      if (seq == pos) {
        if (_enqueue_pos.compare_exchange_weak(pos, pos + 1,
                                               std::memory_order_relaxed)) {
          break;
        }
      }
      else {
        pos = _enqueue_pos.load(std::memory_order_relaxed);
      }
    }

    cell->data = data;
    cell->sequence.store(pos + 1, std::memory_order_release);
  }

  /**
   * Dequeues an item from the queue
   *
   * @param[out] data Reference to place item into
   * @return false if the queue was empty (and dequeuing failed),
   *         true if successful
   */
  T* try_dequeue() {
    Cell *cell;
    auto pos = _dequeue_pos.load(std::memory_order_relaxed);
    for (; ;) {
      cell = &_buffer[pos & BufferMask];
      auto seq = cell->sequence.load(std::memory_order_acquire);
      if (seq == pos + 1) {
        if (_dequeue_pos.compare_exchange_weak(pos, pos + 1,
                                               std::memory_order_relaxed)) {
          break;
        }
      } else if (seq < (pos + 1)) {
        return nullptr;
      } else {
        pos = _dequeue_pos.load(std::memory_order_relaxed);
      }
    }

    auto data = cell->data;
    cell->sequence.store(pos + BufferMask + 1, std::memory_order_release);

    return data;
  }

  bool empty() const {
    auto beg = _dequeue_pos.load(std::memory_order_relaxed);
    auto end = _enqueue_pos.load(std::memory_order_relaxed);
    return beg >= end;
  }

  size_t capacity() const {
    return BufferSize;
  }

private:

  struct Cell {
    T* data;
    std::atomic<uint64_t> sequence;
  };

  //static const size_t cacheline_size = 64;

  alignas(2*TF_CACHELINE_SIZE) std::array<Cell, BufferSize> _buffer;
  alignas(2*TF_CACHELINE_SIZE) std::atomic<uint64_t> _enqueue_pos;
  alignas(2*TF_CACHELINE_SIZE) std::atomic<uint64_t> _dequeue_pos;
};

/**
 * RunQueue is a fixed-size, partially non-blocking deque or Work items.
 * Operations on front of the queue must be done by a single thread (owner),
 * operations on back of the queue can be done by multiple threads concurrently.
 *
 * Algorithm outline:
 * All remote threads operating on the queue back are serialized by a mutex.
 * This ensures that at most two threads access state: owner and one remote
 * thread (Size aside). The algorithm ensures that the occupied region of the
 * underlying array is logically continuous (can wraparound, but no stray
 * occupied elements). Owner operates on one end of this region, remote thread
 * operates on the other end. Synchronization between these threads
 * (potential consumption of the last element and take up of the last empty
 * element) happens by means of state variable in each element. States are:
 * empty, busy (in process of insertion of removal) and ready. Threads claim
 * elements (empty->busy and ready->busy transitions) by means of a CAS
 * operation. The finishing transition (busy->empty and busy->ready) are done
 * with plain store as the element is exclusively owned by the current thread.
 *
 * Note: we could permit only pointers as elements, then we would not need
 * separate state variable as null/non-null pointer value would serve as state,
 * but that would require malloc/free per operation for large, complex values
 * (and this is designed to store std::function<()>).
template <typename Work, unsigned kSize>
class RunQueue {
 public:
  RunQueue() : front_(0), back_(0) {
    // require power-of-two for fast masking
    eigen_plain_assert((kSize & (kSize - 1)) == 0);
    eigen_plain_assert(kSize > 2);            // why would you do this?
    eigen_plain_assert(kSize <= (64 << 10));  // leave enough space for counter
    for (unsigned i = 0; i < kSize; i++) array_[i].state.store(kEmpty, std::memory_order_relaxed);
  }

  ~RunQueue() { eigen_plain_assert(Size() == 0); }

  // PushFront inserts w at the beginning of the queue.
  // If queue is full returns w, otherwise returns default-constructed Work.
  Work PushFront(Work w) {
    unsigned front = front_.load(std::memory_order_relaxed);
    Elem* e = &array_[front & kMask];
    uint8_t s = e->state.load(std::memory_order_relaxed);
    if (s != kEmpty || !e->state.compare_exchange_strong(s, kBusy, std::memory_order_acquire)) return w;
    front_.store(front + 1 + (kSize << 1), std::memory_order_relaxed);
    e->w = std::move(w);
    e->state.store(kReady, std::memory_order_release);
    return Work();
  }

  // PopFront removes and returns the first element in the queue.
  // If the queue was empty returns default-constructed Work.
  Work PopFront() {
    unsigned front = front_.load(std::memory_order_relaxed);
    Elem* e = &array_[(front - 1) & kMask];
    uint8_t s = e->state.load(std::memory_order_relaxed);
    if (s != kReady || !e->state.compare_exchange_strong(s, kBusy, std::memory_order_acquire)) return Work();
    Work w = std::move(e->w);
    e->state.store(kEmpty, std::memory_order_release);
    front = ((front - 1) & kMask2) | (front & ~kMask2);
    front_.store(front, std::memory_order_relaxed);
    return w;
  }

  // PushBack adds w at the end of the queue.
  // If queue is full returns w, otherwise returns default-constructed Work.
  Work PushBack(Work w) {
    EIGEN_MUTEX_LOCK lock(mutex_);
    unsigned back = back_.load(std::memory_order_relaxed);
    Elem* e = &array_[(back - 1) & kMask];
    uint8_t s = e->state.load(std::memory_order_relaxed);
    if (s != kEmpty || !e->state.compare_exchange_strong(s, kBusy, std::memory_order_acquire)) return w;
    back = ((back - 1) & kMask2) | (back & ~kMask2);
    back_.store(back, std::memory_order_relaxed);
    e->w = std::move(w);
    e->state.store(kReady, std::memory_order_release);
    return Work();
  }

  // PopBack removes and returns the last elements in the queue.
  Work PopBack() {
    if (Empty()) return Work();
    EIGEN_MUTEX_LOCK lock(mutex_);
    unsigned back = back_.load(std::memory_order_relaxed);
    Elem* e = &array_[back & kMask];
    uint8_t s = e->state.load(std::memory_order_relaxed);
    if (s != kReady || !e->state.compare_exchange_strong(s, kBusy, std::memory_order_acquire)) return Work();
    Work w = std::move(e->w);
    e->state.store(kEmpty, std::memory_order_release);
    back_.store(back + 1 + (kSize << 1), std::memory_order_relaxed);
    return w;
  }

  // PopBackHalf removes and returns half last elements in the queue.
  // Returns number of elements removed.
  unsigned PopBackHalf(std::vector<Work>* result) {
    if (Empty()) return 0;
    EIGEN_MUTEX_LOCK lock(mutex_);
    unsigned back = back_.load(std::memory_order_relaxed);
    unsigned size = Size();
    unsigned mid = back;
    if (size > 1) mid = back + (size - 1) / 2;
    unsigned n = 0;
    unsigned start = 0;
    for (; static_cast<int>(mid - back) >= 0; mid--) {
      Elem* e = &array_[mid & kMask];
      uint8_t s = e->state.load(std::memory_order_relaxed);
      if (n == 0) {
        if (s != kReady || !e->state.compare_exchange_strong(s, kBusy, std::memory_order_acquire)) continue;
        start = mid;
      } else {
        // Note: no need to store temporal kBusy, we exclusively own these
        // elements.
        eigen_plain_assert(s == kReady);
      }
      result->push_back(std::move(e->w));
      e->state.store(kEmpty, std::memory_order_release);
      n++;
    }
    if (n != 0) back_.store(start + 1 + (kSize << 1), std::memory_order_relaxed);
    return n;
  }

  // Size returns current queue size.
  // Can be called by any thread at any time.
  unsigned Size() const { return SizeOrNotEmpty<true>(); }

  // Empty tests whether container is empty.
  // Can be called by any thread at any time.
  bool Empty() const { return SizeOrNotEmpty<false>() == 0; }

  // Delete all the elements from the queue.
  void Flush() {
    while (!Empty()) {
      PopFront();
    }
  }

 private:
  static const unsigned kMask = kSize - 1;
  static const unsigned kMask2 = (kSize << 1) - 1;

  enum State {
    kEmpty,
    kBusy,
    kReady,
  };

  struct Elem {
    std::atomic<uint8_t> state;
    Work w;
  };

  // Low log(kSize) + 1 bits in front_ and back_ contain rolling index of
  // front/back, respectively. The remaining bits contain modification counters
  // that are incremented on Push operations. This allows us to (1) distinguish
  // between empty and full conditions (if we would use log(kSize) bits for
  // position, these conditions would be indistinguishable); (2) obtain
  // consistent snapshot of front_/back_ for Size operation using the
  // modification counters.
  EIGEN_ALIGN_TO_AVOID_FALSE_SHARING std::atomic<unsigned> front_;
  EIGEN_ALIGN_TO_AVOID_FALSE_SHARING std::atomic<unsigned> back_;
  EIGEN_MUTEX mutex_;  // guards `PushBack` and `PopBack` (accesses `back_`)

  EIGEN_ALIGN_TO_AVOID_FALSE_SHARING Elem array_[kSize];

  // SizeOrNotEmpty returns current queue size; if NeedSizeEstimate is false,
  // only whether the size is 0 is guaranteed to be correct.
  // Can be called by any thread at any time.
  template <bool NeedSizeEstimate>
  unsigned SizeOrNotEmpty() const {
    // Emptiness plays critical role in thread pool blocking. So we go to great
    // effort to not produce false positives (claim non-empty queue as empty).
    unsigned front = front_.load(std::memory_order_acquire);
    for (;;) {
      // Capture a consistent snapshot of front/tail.
      unsigned back = back_.load(std::memory_order_acquire);
      unsigned front1 = front_.load(std::memory_order_relaxed);
      if (front != front1) {
        front = front1;
        std::atomic_thread_fence(std::memory_order_acquire);
        continue;
      }
      if (NeedSizeEstimate) {
        return CalculateSize(front, back);
      } else {
        // This value will be 0 if the queue is empty, and undefined otherwise.
        unsigned maybe_zero = ((front ^ back) & kMask2);
        // Queue size estimate must agree with maybe zero check on the queue
        // empty/non-empty state.
        eigen_assert((CalculateSize(front, back) == 0) == (maybe_zero == 0));
        return maybe_zero;
      }
    }
  }

  EIGEN_ALWAYS_INLINE unsigned CalculateSize(unsigned front, unsigned back) const {
    int size = (front & kMask2) - (back & kMask2);
    // Fix overflow.
    if (EIGEN_PREDICT_FALSE(size < 0)) size += 2 * kSize;
    // Order of modification in push/pop is crafted to make the queue look
    // larger than it is during concurrent modifications. E.g. push can
    // increment size before the corresponding pop has decremented it.
    // So the computed size can be up to kSize + 1, fix it.
    if (EIGEN_PREDICT_FALSE(size > static_cast<int>(kSize))) size = kSize;
    return static_cast<unsigned>(size);
  }

  RunQueue(const RunQueue&) = delete;
  void operator=(const RunQueue&) = delete;
};
*/


}  // end of namespace tf -----------------------------------------------------

