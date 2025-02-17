#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 */

#include <utility>

#include "emp/tools/String.hpp"

template <typename DATA_T, size_t STACK_DEPTH>
struct VMStack {
  emp::array<DATA_T, STACK_DEPTH> stack{0};
  size_t stack_pos = 0;

  void Reset() {
    for (auto & entry : stack) entry = 0;
    stack_pos = 0;
  }

  void Push(DATA_T value) {
    stack[stack_pos] = value;
    stack_pos = (stack_pos+1) % STACK_DEPTH;
  }
  DATA_T Pop() {
    --stack_pos;
    if (stack_pos > STACK_DEPTH) stack_pos = STACK_DEPTH-1; // Loop if needed.
    return stack[stack_pos];
  }

  DATA_T Top() const {
    return stack[stack_pos ? (stack_pos - 1) : STACK_DEPTH-1];
  }

  emp::String ToString() const {
    emp::String out;
    for (size_t i = 0; i < STACK_DEPTH; ++i) {
      if (i) out += ',';
      out.Append(stack[(i+stack_pos)%STACK_DEPTH]);
    }
    return out;
  }

};
