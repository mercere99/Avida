#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 */

#include <utility>

#include "emp/tools/String.hpp"

/// Heads point to a position on the genome OR a position in the memory.
struct VMHead {
  size_t pos;
  bool on_genome = true; // If false, head is on memory.

  VMHead & operator++() { ++pos; return *this; }

  void Reset(size_t _pos=0, bool _on_genome=true) { pos = _pos; on_genome = _on_genome; }

  // Make sure head is in a valid position given the size of the buffer it is on.
  void Refresh(size_t buffer_size) {
    if (pos >= buffer_size) pos %= buffer_size;
  }

  template <typename BUFFER_T>
  auto Read(const BUFFER_T & buffer) {
    Refresh(buffer.size());
    return buffer[pos];
  }

  template <typename DATA_T, typename BUFFER_T>
  void Write(DATA_T data, BUFFER_T & buffer) {
    Refresh(buffer.size());
    buffer[pos] = data;
  }

  emp::String ToString() const {
    return emp::MakeString("[", (on_genome ? "genome:" : "memory:"), pos, "]");
  }
};
