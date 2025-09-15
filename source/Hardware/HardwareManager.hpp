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

  static std::string DefaultName() { return "Unnamed"; };

  virtual bool AddCallback(emp::String name, feedback_t fun) = 0;

  [[nodiscard]] virtual emp::String ToSequence(const Genome & genome) const = 0;
  [[nodiscard]] virtual Genome LoadGenome(emp::String filename) = 0;

  virtual void Mutate(emp::Random & random, Genome & genome) const = 0;

  [[nodiscard]] hw_ptr_t Allocate(Organism & org) {
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

// A helper class where a particular hardware implementation can be provided and it will
// automatically build a manager for that hardware.
template <typename VM_T>
class HardwareType : public HardwareManager {
private:
  using HardwareManager::hw_ptr_t;
  hw_ptr_t AllocateNew(Organism & org) override {
    auto out_ptr = emp::NewPtr<VM_T>(*this);
    out_ptr->SetOrganism(org);
    return out_ptr;
  }

  static constexpr double mut_prob{0.0075};
  static constexpr double mut_scale{1.0 / emp::Log2(1.0 - mut_prob)};

  InstSet<VM_T> inst_set;
public:
  HardwareType() : inst_set(VM_T::BuildInstSet()) { }

  static std::string DefaultName() const { return VM_T::HardwareName() + "Manager"; }

  InstSet<VM_T> & GetInstSet() { return inst_set; }
  const InstSet<VM_T> & GetInstSet() const { return inst_set; }

  bool AddCallback(emp::String name, feedback_t fun) override {
    inst_set.AddCallbackInst(name, fun);
    return true;
  }

  [[nodiscard]] emp::String ToSequence(const Genome & genome) const override {
    return inst_set.ToSequence(genome);
  }

  // Load an organism from a file.
  Genome LoadGenome(emp::String filename) override {
    return inst_set.LoadGenome(filename);
  }

  void Mutate(emp::Random & random, Genome & genome) const override {
    size_t mut_pos = static_cast<size_t>(emp::Log2(random.GetDouble()) * mut_scale);
    while (mut_pos < genome.size()) {
      genome[mut_pos] = inst_set.GetRandom(random);
      mut_pos += static_cast<size_t>(emp::Log2(random.GetDouble()) * mut_scale) + 1;
    }
  }

};