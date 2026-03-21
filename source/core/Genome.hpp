#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 */

#include <cstddef>   // for size_t
#include <cstdint>   // for uint8_t
#include <iterator>  // for std::input_iterator

#include "emp/base/assert.hpp"
#include "emp/base/vector.hpp"

/// @brief A genome with a fixed number of possible states.
template <typename VALUE_T>
class BasicGenome {
public:
  using value_t = VALUE_T;

private:
  using this_t = BasicGenome<VALUE_T>;

  emp::vector<value_t> genome;

  // Create a liberal limit on genome sizes to spot problems if they get much too high.
  static constexpr size_t MAX_SIZE = size_t{1} << 30; 

public:
  BasicGenome() = default;
  BasicGenome(const BasicGenome &) = default;
  BasicGenome(BasicGenome &&) = default;
  explicit BasicGenome(const emp::vector<value_t> & g) : genome(g) { }
  explicit BasicGenome(emp::vector<value_t> && g) : genome(std::move(g)) { }
  explicit BasicGenome(size_t length, value_t default_value = value_t{})
    : genome(length, default_value) { }
  template <typename It> requires std::input_iterator<It> &&
                                  std::convertible_to<std::iter_value_t<It>, value_t>
  BasicGenome(It first, It last) : genome(first, last) { }
  ~BasicGenome() = default;

  auto begin() noexcept { return genome.begin(); }
  auto begin() const noexcept { return genome.begin(); }
  auto cbegin() const noexcept { return genome.cbegin(); }
  auto end() noexcept { return genome.end(); }
  auto end() const noexcept { return genome.end(); }
  auto cend() const noexcept { return genome.cend(); }

  this_t & operator=(const this_t &) = default;
  this_t & operator=(this_t &&) = default;

  [[nodiscard]] bool operator<=>(const this_t &) const = default;

  [[nodiscard]] value_t operator[](size_t pos) const { return genome[pos]; }
  [[nodiscard]] value_t & operator[](size_t pos) { return genome[pos]; }

  [[nodiscard]] bool IsEmpty() const { return genome.empty(); }
  void Clear() { genome.clear(); }

  void Push(value_t val) { genome.push_back(val); }

  [[nodiscard]] size_t size() const { return genome.size(); }
  this_t & Resize(size_t new_size) {
    emp_assert(new_size < MAX_SIZE);
    genome.resize(new_size);
    return *this;
  }

  void Reserve(size_t max_size) {
    emp_assert(max_size < MAX_SIZE);
    genome.reserve(max_size);
  }

  void Insert(size_t pos, value_t value, size_t count=1) {
    emp_assert(pos <= size());
    emp_assert(count < MAX_SIZE - size());
    genome.insert(begin()+pos, count, value);
  }

  void Erase(size_t pos, size_t count=1) {
    emp_assert(pos <= size());
    emp_assert(count <= size() - pos);
    genome.erase(begin() + pos, begin() + (pos + count));
  }

  // Make a copy of a section of this genome and return it.
  [[nodiscard]] this_t Copy(size_t start_pos, size_t count) const {
    emp_assert(start_pos <= size());
    emp_assert(count <= size() - start_pos);
    const size_t end_pos = start_pos + count;
    return this_t{begin()+start_pos, begin()+end_pos};
  }

  // Remove [start_pos, end_pos) from this Genome and return it.
  [[nodiscard]] this_t Extract(size_t start_pos, size_t count) {
    emp_assert(start_pos <= size());
    emp_assert(count <= size() - start_pos);
    this_t out(Copy(start_pos, count));
    Erase(start_pos, count);
    return out;
  }

};

using Genome = BasicGenome<uint8_t>;
