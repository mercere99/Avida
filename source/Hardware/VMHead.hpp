#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 */

#include <utility>

#include "emp/base/concepts.hpp"
#include "emp/tools/String.hpp"

/// Heads point to a position on the genome OR a position in the memory.
struct VMHead {
  size_t pos;
  bool on_genome = true; // If false, head is on memory.

  VMHead & operator++() { ++pos; return *this; }

  void Reset(size_t _pos=0, bool _on_genome=true) { pos = _pos; on_genome = _on_genome; }

  // Read in the symbol at the head position.  Return a 0 if head is out of range.
  template <typename BUFFER_T>
  [[nodiscard]] auto Read(const BUFFER_T & buffer) const {
    return pos < buffer.size() ? buffer[pos] : 0;
  }

  // If in memory, write the value at the current head position.
  // If on the genome, insert the value at the current head position (or at end)
  template <typename DATA_T, typename BUFFER_T>
  void Write(DATA_T data, BUFFER_T & buffer) {
    if constexpr (std::same_as<BUFFER_T, Genome>) {
      emp_assert(on_genome);
      if (pos < buffer.size()) buffer.Insert(pos, data);
      else buffer.Push(data);
    } else { // In memory!
      emp_assert(!on_genome);
      if (pos < buffer.size()) buffer[pos] = data;
      // If out of range, nothing is written.
    }
  }

  [[nodiscard]] emp::String ToString() const {
    return emp::MakeString("[", (on_genome ? "genome:" : "memory:"), pos, "]");
  }
};
