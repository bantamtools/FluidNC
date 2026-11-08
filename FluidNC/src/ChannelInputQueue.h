// Copyright (c) 2026 Bantam Tools
// Use of this source code is governed by a GPLv3 license (see LICENSE).

// ChannelInputQueue is Channel::_queue: an unbounded FIFO of input bytes with the
// std::queue<uint8_t> subset Channel and its subclasses use (size/empty/front/pop/push),
// but whose storage is allocated lazily on the first push.
//
// Why: libstdc++'s std::deque default constructor allocates its map (8 pointers) plus
// one 512 B node, so a std::queue<uint8_t> member cost every Channel ~560 B of heap even
// if it never queued a byte -- and most never do: every FileStream/InputFile (each job
// file, each upload) is a Channel. Under low heap that constructor was also where
// std::bad_alloc escaped during a Wi-Fi file upload . Here a
// never-pushed queue holds only a null pointer and reads as empty without allocating.
//
// Header-only and std-only on purpose, so host tests can exercise it without dragging
// in Channel.cpp's dependencies.
//
// Concurrency: no locking of its own. The lazy allocation (writing _q) is a structural
// op exactly like a deque node allocation, so it is covered by the same rule as before:
// WSChannel wraps every access in lockInput()/unlockInput(); all other channels touch
// their queue from the polling task only .
//
// Allocation failure: push() throws std::bad_alloc, as std::deque::push_back always has.
// Lazy allocation does not add a new class of failure: a deque already allocates a fresh
// 512 B node on push every time its cumulative input crosses a node boundary, so every
// push site already had to cope with bad_alloc. The sites that can run at the OOM floor
// keep their existing handlers: WSChannel::push (wifi_task) catches, unlocks and marks
// the channel dead; Channel::pollLine catches and drops the byte. On failure _q stays
// null (the new-expression frees the outer object if the deque constructor throws), so
// a later push simply retries.
//
// The storage is NOT released when the queue drains: a channel that has queued once
// (USB/UART/WS/telnet/macros) will queue again, and freeing then re-allocating ~600 B
// per burst would churn and fragment a heap that is already the scarce resource. It is
// freed with the channel.

#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>

class ChannelInputQueue {
public:
    ChannelInputQueue() = default;  // never allocates

    ChannelInputQueue(const ChannelInputQueue&)            = delete;
    ChannelInputQueue& operator=(const ChannelInputQueue&) = delete;

    size_t size() const { return _q ? _q->size() : 0; }
    bool   empty() const { return !_q || _q->empty(); }

    // Precondition (as for std::queue): !empty().
    uint8_t front() const { return _q->front(); }
    void    pop() { _q->pop_front(); }

    // Unbounded, like the std::queue it replaces. May throw std::bad_alloc (see above).
    void push(uint8_t c) {
        if (!_q) {
            _q.reset(new std::deque<uint8_t>());
        }
        _q->push_back(c);
    }

    // True once the first push has allocated the storage. Diagnostics / tests only.
    bool allocated() const { return bool(_q); }

private:
    std::unique_ptr<std::deque<uint8_t>> _q;
};
