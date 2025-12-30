#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 */

#include "emp/base/Ptr.hpp"
#include "emp/math/constants.hpp"

template <typename POP_T>
class PopPosition {
public:
  using pos_t = uint32_t;
  static constexpr pos_t npos = static_cast<pos_t>(-1);

private:
  emp::Ptr<POP_T> pop_ptr = nullptr;  // Which population is this position in?
  pos_t index = npos;

public:
  POP_T & GetPopulation() { return *pop_ptr; }
  const POP_T & GetPopulation() const { return *pop_ptr; }
  PopPosition & SetPopulation(POP_T & pop) { pop_ptr = &pop; return *this;}

  pos_t GetIndex() const { return index; }
  PopPosition & SetIndex(pos_t in) { index = in; return *this; }

  PopPosition & Set(POP_T & pop, pos_t in) {
    pop_ptr = &pop;
    index = in;
    return *this;
  }

  PopPosition & Clear() {
    pop_ptr = nullptr;
    return *this;
  }

  bool InPopulation() const { return !pop_ptr.IsNull(); }
  bool InPopulation(const POP_T & test_pop) const { return pop_ptr == &test_pop; }
};
