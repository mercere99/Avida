#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 */

#include "emp/base/array.hpp"
#include "emp/base/assert.hpp"
#include "emp/base/vector.hpp"
#include "emp/meta/type_traits.hpp"
#include "emp/meta/TypePack.hpp"

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
  Genome(const emp::vector<value_t> & g) : genome(g) { }
  Genome(emp::vector<value_t> && g) : genome(std::move(g)) { }
  Genome(size_t length, value_t default_value=0) : genome(length, default_value) { }
  ~Genome() { }

  auto begin() { return genome.begin(); }
  auto begin() const { return genome.begin(); }
  auto end() { return genome.end(); }
  auto end() const { return genome.end(); }

  Genome & operator=(const Genome &) = default;
  Genome & operator=(Genome &&) = default;

  [[nodiscard]] bool operator<=>(const Genome &) const = default;

  [[nodiscard]] value_t operator[](size_t pos) const { return genome[pos]; }
  [[nodiscard]] value_t & operator[](size_t pos) { return genome[pos]; }

  [[nodiscard]] value_t Get(size_t pos) const { return genome[pos]; }
  void Set(size_t pos, value_t val) { genome[pos] = val; }

  void Push(value_t val) { genome.push_back(val); }

  [[nodiscard]] size_t size() const { return genome.size(); }
  Genome & Resize(size_t new_size) {
    genome.resize(new_size);
    return *this;
  }

  void Reserve(size_t max_size) { genome.reserve(max_size); }

  template <typename T>
  void Insert(size_t pos, T && value, size_t count=1) {
    genome.insert(genome.begin()+pos, count, std::forward<T>(value));
  }

  void Erase(size_t pos, size_t count=1) {
    const size_t end_pos = pos + count;
    genome.erase(genome.begin()+pos, genome.begin()+end_pos);
  }

  // Remove [start_pos, end_pos) from this Vector and return it.
  [[nodiscard]] Genome Copy(size_t start_pos, size_t count) {
    const size_t end_pos = start_pos + count;
    emp_assert(start_pos <= size() && count <= size() && end_pos <= size());
    Genome out;
    out.Reserve(count);
    std::copy(begin() + start_pos, begin() + end_pos, std::back_inserter(out.genome));
    return out;
  }

  // Remove [start_pos, end_pos) from this Vector and return it.
  [[nodiscard]] Genome Extract(size_t start_pos, size_t count) {
    Genome out(Copy(start_pos, count));
    Erase(start_pos, count);
    return out;
  }
};
