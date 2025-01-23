#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 */

#include "emp/base/vector.hpp"
#include "emp/meta/TypePack.hpp"

template <size_t NUM_STATES>
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
  emp::vector<state_t> genome;

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

  bool OK() const {
    // Make sure all states are legal.
    for (auto state : genome) { if (state >= NUM_STATES) return false; }
    return true;
  }
};
