#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 */

#include <concepts>
#include <cstddef>   // for size_t
#include <cstdint>   // for uint8_t
#include <iterator>  // for std::input_iterator

#include "emp/base/assert.hpp"
#include "emp/base/vector.hpp"
#include "emp/math/Random.hpp"
#include "emp/math/Range.hpp"

/// @brief A genome with a fixed number of possible states.
template <typename VALUE_T>
class Genome {
public:
  using value_t = VALUE_T;

private:
  using this_t = Genome<VALUE_T>;

  emp::vector<value_t> genome;

  // Genome site value range is [0,max_value);
  value_t max_value = 0;

  // Create a liberal limit on genome sizes to spot problems if they get much too high.
  static constexpr size_t MAX_SIZE = size_t{1} << 30; 

public:
  Genome() = default;
  Genome(const Genome &) = default;
  Genome(Genome &&) = default;
  Genome(value_t max_value, size_t length = 0, value_t default_value = value_t{})
    : genome(length, default_value), max_value(max_value) { }
  Genome(value_t max_value, const emp::vector<value_t> & g)
    : genome(g), max_value(max_value) { }
  Genome(value_t max_value, emp::vector<value_t> && g)
    : genome(std::move(g)), max_value(max_value) { }
  template <typename It> requires std::input_iterator<It> &&
                                  std::convertible_to<std::iter_value_t<It>, value_t>
  Genome(value_t max_value, It first, It last) : genome(first, last), max_value(max_value) { }
  ~Genome() = default;

  void Serialize(emp::SerialPod & pod) {
    pod(genome, max_value);
  }

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
  [[nodiscard]] auto & Values(this auto & self) { return self.genome; }

  [[nodiscard]] bool IsEmpty() const { return genome.empty(); }

  [[nodiscard]] size_t FindMaxID() const {
    return std::ranges::max_element(genome) - genome.begin();
  }
  void Clear() { genome.clear(); }

  value_t RandomValue(emp::Random & random) const {
    return random.GetValue<value_t>(max_value);
  }

  void RandomizeSite(emp::Random & random, size_t pos) {
    genome[pos] = RandomValue(random);
  }

  bool Push(value_t val) {
    emp_assert(val < max_value);
    genome.push_back(val);
    return true;
  }

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
    return this_t{max_value, begin()+start_pos, begin()+end_pos};
  }

  // Remove [start_pos, end_pos) from this Genome and return it.
  [[nodiscard]] this_t Extract(size_t start_pos, size_t count) {
    emp_assert(start_pos <= size());
    emp_assert(count <= size() - start_pos);
    this_t out(Copy(start_pos, count));
    Erase(start_pos, count);
    return out;
  }

  std::string AsString() const { 
    std::stringstream ss;
    for (const auto & val : genome) {
      ss << '[' << val << ']';
    }
    return ss.str();
  }

};
