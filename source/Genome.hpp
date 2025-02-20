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
/// @param NUM_STATES a count of how many states are possible at each position.
template <size_t NUM_STATES=256>
class StateGenome {
public:
  using state_t = emp::min_uint_type<NUM_STATES>;

private:
  using this_t = StateGenome<NUM_STATES>;
  using genome_t = emp::vector<state_t>;

  genome_t genome;

public:
  StateGenome() = default;
  StateGenome(const StateGenome &) = default;
  StateGenome(StateGenome &&) = default;
  StateGenome(const genome_t & g) : genome(g) { }
  StateGenome(genome_t && g) : genome(std::move(g)) { }
  StateGenome(size_t length, state_t default_value=0) : genome(length, default_value) { }
  ~StateGenome() { }

  auto begin() { return genome.begin(); }
  auto begin() const { return genome.begin(); }
  auto end() { return genome.end(); }
  auto end() const { return genome.end(); }

  StateGenome & operator=(const StateGenome &) = default;
  StateGenome & operator=(StateGenome &&) = default;

  [[nodiscard]] bool operator<=>(const StateGenome &) const = default;

  [[nodiscard]] state_t operator[](size_t pos) const { return genome[pos]; }
  [[nodiscard]] state_t & operator[](size_t pos) { return genome[pos]; }

  [[nodiscard]] state_t Get(size_t pos) const { return genome[pos]; }
  void Set(size_t pos, state_t val) {
    emp_assert(val < NUM_STATES);
    genome[pos] = val;
  }

  void Push(state_t val) { genome.push_back(val); }

  [[nodiscard]] size_t size() const { return genome.size(); }
  this_t & Resize(size_t new_size) {
    genome.resize(new_size);
    return *this;
  }

  template <typename T>
  void Insert(size_t pos, T && value, size_t count=1) {
    genome.insert(genome.begin()+pos, count, std::forward<T>(value));
  }

  template <typename T>
  void Erase(size_t pos, size_t count=1) {
    genome.erase(genome.begin()+pos, count);
  }

  // Remove [start_pos, end_pos) from this Vector and return it.
  [[nodiscard]] this_t Copy(size_t start_pos, size_t count) {
    const size_t end_pos = start_pos + count;
    emp_assert(start_pos <= size() && count <= size() && end_pos <= size());
    this_t out;
    out.reserve(count);
    std::copy(begin() + start_pos, begin() + end_pos, std::back_inserter(out));
    return out;
  }

  // Remove [start_pos, end_pos) from this Vector and return it.
  [[nodiscard]] this_t Extract(size_t start_pos, size_t count) {
    this_t out(Copy(start_pos, count));
    Erase(start_pos, count);
    return out;
  }

  bool OK() const {
    // Make sure all states are legal.
    for (auto state : genome) { if (state >= NUM_STATES) return false; }
    return true;
  }
};
