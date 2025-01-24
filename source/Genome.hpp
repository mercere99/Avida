#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 */

#include "emp/base/array.hpp"
#include "emp/base/assert.hpp"
#include "emp/base/vector.hpp"
#include "emp/meta/TypePack.hpp"

/// @brief A genome with a fixed number of possible states.
/// @param NUM_STATES a count of how many states are possible at each position.
/// @param GENOME_SIZE a fixed number of sites in the genome (zero = variable size)
template <size_t NUM_STATES=256, size_t GENOME_SIZE=0>
class StateGenome {
public:
  // Determine the type of each state given the number of possible states.
  constexpr auto StateType() {
    if constexpr (num_states <= 256) return uint8_t{0};
    else if constexpr (num_states <= 65536) return uint16_t{1};
    else if constexpr (num_states <= 4294967296) return uint32_t{2};
    else return uint64_t{3};
  }
  using state_t = decltype(StateType());

private:
  using this_t = StateGenome<NUM_STATES, MAX_SIZE>;
  using var_genome_t = emp::vector<state_t>;
  using array_genome_t = emp::array<state_t, MAX_SIZE>;
  using genome_t = std::conditional_t<MAX_SIZE, array_genome_t, var_genome_t>;

  genome_t genome;

public:
  StateGenome() = default;
  StateGenome(const StateGenome &) = default;
  StateGenome(StateGenome &&) = default;
  StateGenome(size_t length, state_t default_value) : genome(length, default_value) { }
  ~StateGenome() { }

  StateGenome & operator=(const StateGenome &) = default;
  StateGenome & operator=(StateGenome &&) = default;

  bool operator<=>(const StateGenome &) const = default;

  state_t operator[](size_t pos) const { return genome[pos]; }
  state_t & operator[](size_t pos) { return genome[pos]; }

  size_t size() const { return genome.size(); }
  this_t & resize(size_t new_size) {
    emp_assert(GENOME_SIZE == 0, "Must have a variable genome size (0) to change size.");
    genome.resize(new_size);
    return *this;
  }

  // Remove [start_pos, end_pos) from this genome and return it.
  this_t Extract(size_t start_pos, end_pos) {
    emp_assert(start_pos <= end_pos && end_pos <= genome.size());
    this_t out;
    if constexpr (MAX_SIZE) {
      std::copy(genome.begin() + start_pos, genome.begin() + end_pos, out.genome.begin());
    }
    else { // Variable size genomes
      out.genome.reserve(end_pos - start_pos);
      std::copy(genome.begin() + start_pos, genome.begin() + end_pos, std::back_inserter(out.genome));
    }
    return out_genome;
  }

  bool OK() const {
    // Make sure all states are legal.
    for (auto state : genome) { if (state >= NUM_STATES) return false; }
    return true;
  }
};
