#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 */

// Hardware managers build new hardware for Organisms and recycle old hardware
// to speed up organism construction.

#include "emp/base/Ptr.hpp"
#include "emp/base/vector.hpp"
#include "emp/tools/String.hpp"

#include "HardwareBase.hpp"
#include "InstSet.hpp"

// Pre-declarations
class Organism;

/// A class to handle a particular configuration of hardware.
class HardwareManager {
protected:
  using hw_ptr_t = emp::Ptr<HardwareBase>;
  using feedback_t = std::function<void(Organism & /*org*/)>;
  emp::vector< hw_ptr_t > hw_ptrs;  // Pointers to available hardware.

  virtual hw_ptr_t AllocateNew(Organism & org) = 0;
public:
  virtual ~HardwareManager() { Clear(); }

  virtual bool AddCallback(emp::String name, feedback_t fun) = 0;

  virtual Genome LoadGenome(emp::String filename) = 0;

  hw_ptr_t Allocate(Organism & org) {
    if (hw_ptrs.size()) {
      hw_ptr_t out = hw_ptrs.back();
      hw_ptrs.pop_back();
      out->SetOrganism(org);
      return out;
    }
    return AllocateNew(org);
  }

  void Release(hw_ptr_t ptr) {
    hw_ptrs.push_back(ptr);
  }

  // Remove current inventory of hardware.
  void Clear() {
    for (auto ptr : hw_ptrs) ptr.Delete();
    hw_ptrs.resize(0);
  }
};

template <typename VM_T>
class HardwareType : public HardwareManager {
private:
  using HardwareManager::hw_ptr_t;
  hw_ptr_t AllocateNew(Organism & org) override {
    auto out_ptr = emp::NewPtr<VM_T>(*this);
    out_ptr->SetOrganism(org);
    return out_ptr;
}

  InstSet<VM_T> inst_set;
public:
  HardwareType() : inst_set(VM_T::BuildInstSet()) { }

  InstSet<VM_T> & GetInstSet() { return inst_set; }
  const InstSet<VM_T> & GetInstSet() const { return inst_set; }

  bool AddCallback(emp::String name, feedback_t fun) override {
    inst_set.AddCallbackInst(name, fun);
    return true;
  }

  // Load an organism from a file.
  Genome LoadGenome(emp::String filename) override {
    return inst_set.LoadGenome(filename);
  }
};