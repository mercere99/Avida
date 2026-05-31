#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 */

#include <utility>

#include "emp/tools/String.hpp"

template <typename DATA_T, size_t STACK_DEPTH>
struct VMStack {
  static_assert((STACK_DEPTH & (STACK_DEPTH - 1)) == 0, "STACK_DEPTH must be a power of two");
  static constexpr size_t STACK_MASK = STACK_DEPTH - 1;
  
  emp::array<DATA_T, STACK_DEPTH> stack{0};
  size_t stack_pos = 0;

  void Serialize(emp::SerialPod & pod) {
    pod(stack, stack_pos);
  }

  void Reset() {
    for (auto & entry : stack) entry = 0;
    stack_pos = 0;
  }

  void Push(DATA_T value) {
    stack[stack_pos] = value;
    stack_pos = (stack_pos+1) & STACK_MASK;
  }
  DATA_T Pop() {
    stack_pos = (stack_pos - 1) & STACK_MASK;  // Loop stack if needed.
    return stack[stack_pos];
  }

  [[nodiscard]] DATA_T Top() const {
    return stack[stack_pos ? (stack_pos - 1) : STACK_DEPTH-1];
  }

  [[nodiscard]] emp::String ToString() const {
    emp::String out;
    bool printing = false;
    for (size_t i = 0; i < STACK_DEPTH; ++i) {
      if (printing) out += ',';
      auto value = stack[(i+stack_pos) & STACK_MASK];
      if (printing || value != 0) {
        printing = true;
        out.Append(value);
      }
    }
    return out;
  }

};
