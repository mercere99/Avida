#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 */

#include <cstdint>   // for size_t
#include <algorithm> // for std::copy and std::back_inserter
#include <iterator>
#include <ostream>

#include "emp/base/assert.hpp"
#include "emp/base/vector.hpp"

/// @brief A genome with a fixed number of possible states.
class Genome {
public:
  using value_t = uint8_t;

private:
  emp::vector<value_t> genome;

public:
  Genome() = default;
  Genome(const Genome &) = default;
  Genome(Genome &&) = default;
  explicit Genome(const emp::vector<value_t> & g) : genome(g) { }
  explicit Genome(emp::vector<value_t> && g) : genome(std::move(g)) { }
  explicit Genome(size_t length, value_t default_value=0) : genome(length, default_value) { }
  ~Genome() = default;

  auto begin() { return genome.begin(); }
  auto begin() const { return genome.begin(); }
  auto end() { return genome.end(); }
  auto end() const { return genome.end(); }

  Genome & operator=(const Genome &) = default;
  Genome & operator=(Genome &&) = default;

  [[nodiscard]] bool operator<=>(const Genome &) const = default;

  [[nodiscard]] value_t operator[](size_t pos) const { return genome[pos]; }
  [[nodiscard]] value_t & operator[](size_t pos) { return genome[pos]; }

  void Push(value_t val) { genome.push_back(val); }

  [[nodiscard]] size_t size() const { return genome.size(); }
  Genome & Resize(size_t new_size) {
    emp_assert(new_size < (1 << 30));  // Sizes of genomes should never be in the billions.
    genome.resize(new_size);
    return *this;
  }

  void Reserve(size_t max_size) {
    emp_assert(max_size < (1 << 30));  // Sizes of genomes should never be in the billions.
    genome.reserve(max_size);
  }

  template <typename T>
  void Insert(size_t pos, T && value, size_t count=1) {
    emp_assert(pos <= genome.size());
    genome.insert(genome.begin()+pos, count, std::forward<T>(value));
  }

  void Erase(size_t pos, size_t count=1) {
    emp_assert(pos <= genome.size());
    const size_t end_pos = pos + count;
    genome.erase(genome.begin()+pos, genome.begin()+end_pos);
  }

  // Make a copy of a section of this genome and return it.
  [[nodiscard]] Genome Copy(size_t start_pos, size_t count) {
    emp_assert(start_pos <= size());
    emp_assert(count <= size() - start_pos);
    const size_t end_pos = start_pos + count;
    Genome out;
    out.genome.assign(begin()+start_pos, begin()+end_pos);
    return out;
  }

  // Remove [start_pos, end_pos) from this Vector and return it.
  [[nodiscard]] Genome Extract(size_t start_pos, size_t count) {
    emp_assert(start_pos + count <= genome.size());
    Genome out(Copy(start_pos, count));
    Erase(start_pos, count);
    return out;
  }

};
