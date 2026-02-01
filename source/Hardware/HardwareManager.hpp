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

#include "InstSet.hpp"

// A helper class where a particular hardware implementation can be provided and it will
// automatically build a manager for that hardware.
template <typename HW_T>
class HardwareManager {
  using hardware_t = HW_T;
  using genome_t = hardware_t::genome_t;
  using org_t = Organism<hardware_t>;

private:
  using hw_ptr_t = emp::Ptr<hardware_t>;
  using feedback_t = std::function<void(org_t & /*org*/)>;

  emp::vector< hw_ptr_t > hw_ptrs;  // Pointers to available hardware.

  static constexpr double mut_prob{0.0075};
  static constexpr double mut_scale{1.0 / emp::Log2(1.0 - mut_prob)};

  InstSet<hardware_t> inst_set;


  // --- Helper functions --

  hw_ptr_t AllocateNew(org_t & org) {
    auto out_ptr = emp::NewPtr<hardware_t>(*this);
    out_ptr->SetOrganism(org);
    return out_ptr;
  }

public:
  HardwareManager() : inst_set(hardware_t::BuildInstSet()) { }
  ~HardwareManager() { Clear(); }

  static std::string DefaultName() { return hardware_t::HardwareName() + "Manager"; }

  InstSet<hardware_t> & GetInstSet() { return inst_set; }
  const InstSet<hardware_t> & GetInstSet() const { return inst_set; }

  [[nodiscard]] hw_ptr_t Allocate(org_t & org) {
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

  bool AddCallback(emp::String name, feedback_t fun) {
    inst_set.AddCallbackInst(name, fun);
    return true;
  }

  [[nodiscard]] emp::String ToSequence(const genome_t & genome) const {
    return inst_set.ToSequence(genome);
  }

  // Load an organism from a file.
  genome_t LoadGenome(emp::String filename) {
    return inst_set.LoadGenome(filename);
  }

  void Mutate(emp::Random & random, genome_t & genome) const {
    size_t mut_pos = static_cast<size_t>(emp::Log2(random.GetDouble()) * mut_scale);
    while (mut_pos < genome.size()) {
      genome[mut_pos] = inst_set.GetRandom(random);
      mut_pos += static_cast<size_t>(emp::Log2(random.GetDouble()) * mut_scale) + 1;
    }
  }

};