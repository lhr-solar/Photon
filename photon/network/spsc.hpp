#pragma once

#include <atomic>
#include <cassert>
#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>

template <typename T>
struct SPSCQueue {
explicit SPSCQueue(std::size_t capacity){
  if (capacity == 0){ capacity = 1; }
  capacity_ = capacity + 1;
  storage_ = static_cast<T*>(::operator new(sizeof(T) * capacity_));
}

~SPSCQueue(){
  while (front() != nullptr){ pop(); }
  ::operator delete(static_cast<void*>(storage_));
}

SPSCQueue(const SPSCQueue&) = delete;
SPSCQueue& operator=(const SPSCQueue&) = delete;

template <typename... Args>
void emplace(Args&&... args){
  while (!try_emplace(std::forward<Args>(args)...)){}
}

template <typename... Args>
bool try_emplace(Args&&... args){
  static_assert(std::is_constructible<T, Args&&...>::value, "T must be constructible with the supplied arguments");
  auto write = writeIndex_.load(std::memory_order_relaxed);
  auto next = increment(write);
  if (next == readIndex_.load(std::memory_order_acquire)){ return false; }
  new (slot(write)) T(std::forward<Args>(args)...);
  writeIndex_.store(next, std::memory_order_release);
  return true;
}

void push(const T& value){
  emplace(value);
}

template <typename P, typename = typename std::enable_if<std::is_constructible<T, P&&>::value>::type>
void push(P&& value){
  emplace(std::forward<P>(value));
}

bool try_push(const T& value){
  return try_emplace(value);
}

template <typename P, typename = typename std::enable_if<std::is_constructible<T, P&&>::value>::type>
bool try_push(P&& value){
  return try_emplace(std::forward<P>(value));
}

T* front() noexcept {
  auto read = readIndex_.load(std::memory_order_relaxed);
  if (read == writeIndex_.load(std::memory_order_acquire)){ return nullptr; }
  return slot(read);
}

void pop() noexcept {
  auto read = readIndex_.load(std::memory_order_relaxed);
  assert(read != writeIndex_.load(std::memory_order_acquire) && "pop() must follow a successful front()");
  slot(read)->~T();
  readIndex_.store(increment(read), std::memory_order_release);
}

std::size_t size() const noexcept {
  auto write = writeIndex_.load(std::memory_order_acquire);
  auto read = readIndex_.load(std::memory_order_acquire);
  if (write >= read){ return write - read; }
  return (write + capacity_) - read;
}

bool empty() const noexcept {
  return writeIndex_.load(std::memory_order_acquire) == readIndex_.load(std::memory_order_acquire);
}

std::size_t capacity() const noexcept { return capacity_ - 1; }

private:
T* slot(std::size_t index) noexcept { return storage_ + index; }

std::size_t increment(std::size_t index) const noexcept {
  ++index;
  if (index == capacity_){ index = 0; }
  return index;
}

std::size_t capacity_{0};
T* storage_{nullptr};
alignas(64) std::atomic<std::size_t> writeIndex_{0};
alignas(64) std::atomic<std::size_t> readIndex_{0};
};
