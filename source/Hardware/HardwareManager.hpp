#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 */

#include "emp/base/Ptr.hpp"
#include "emp/base/vector.hpp"

#include "HardwareBase.hpp"

/// A class to handle a particular configuration of hardware.
class HardwareManager {
protected:
  using hw_ptr_t = emp::Ptr<HardwareBase>;
  emp::vector< hw_ptr_t > hw_ptrs;  // Pointers to available hardware.

  virtual hw_ptr_t AllocateNew() = 0;
public:
  hw_ptr_t Allocate() {
    if (hw_ptrs.size()) {
      hw_ptr_t out = hw_ptrs.back();
      hw_ptrs.pop_back();
      return out;
    }
    return AllocateNew();
  }

  void Release(hw_ptr_t ptr) {
    hw_ptrs.push_back(ptr);
  }
};

template <typename VM_T>
class HardwareType : public HardwareManager {
private:
  using HardwareManager::hw_ptr_t;
  hw_ptr_t AllocateNew() override { return emp::NewPtr<VM_T>(); }

public:
};