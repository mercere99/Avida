#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 */

#include "emp/base/assert.hpp"
#include "emp/base/Ptr.hpp"

#include <cstdint>  // for uint32_t

template <typename POP_T>
class PopPosition {
public:
  using pos_t = uint32_t;
  static constexpr pos_t npos = static_cast<pos_t>(-1);

private:
  emp::Ptr<POP_T> pop_ptr = nullptr;  // Which population is this position in?
  pos_t index = npos;

public:
  PopPosition() = default;
  PopPosition(const PopPosition &) = default;
  PopPosition(POP_T & pop, pos_t index) : pop_ptr(&pop), index(index) { }

  // Assignment operator
  PopPosition & operator=(const PopPosition &) = default;

  [[nodiscard]] POP_T & GetPopulation() noexcept {
    emp_assert(InPopulation());
    return *pop_ptr;
  }
  [[nodiscard]] const POP_T & GetPopulation() const noexcept {
    emp_assert(InPopulation());
    return *pop_ptr;
  }

  // Change to a new population; default to index zero of the population.
  PopPosition & SetPopulation(POP_T & pop) noexcept {
    pop_ptr = &pop;
    index = npos;
    return *this;
  }

  [[nodiscard]] pos_t GetIndex() const noexcept { return index; }
  PopPosition & SetIndex(pos_t in) noexcept {
    emp_assert(InPopulation());
    index = in;
    return *this;
  }

  PopPosition & Set(POP_T & pop, pos_t in) noexcept {
    emp_assert(in < pop.size());
    pop_ptr = &pop;
    index = in;
    return *this;
  }

  PopPosition & Clear() noexcept {
    pop_ptr = nullptr;
    index = npos;
    return *this;
  }

  [[nodiscard]] bool InPopulation() const noexcept { return !pop_ptr.IsNull(); }
  [[nodiscard]] bool InPopulation(const POP_T & test_pop) const  noexcept {
    return pop_ptr == &test_pop;
  }

  explicit operator bool() const noexcept { return InPopulation() && index != npos; }
};
