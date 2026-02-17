#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 */

// Main Avida controller class.

#include "emp/base/Ptr.hpp"
#include "emp/base/vector.hpp"
#include "emp/config/command_line.hpp"
#include "emp/datastructs/map_utils.hpp"
#include "emp/meta/TypePack.hpp"

#include "../Hardware/AvidaVM.hpp"
#include "../Hardware/HardwareManager.hpp"
#include "concepts.hpp"
#include "Population.hpp"

template <avida::concepts::Hardware HW_T> struct hardware_filter { };

/// Main Avida-control object.
// @CAO TODO: Allow for multiple HW types in the same Avida run.
template <typename... PLUG_IN_Ts>
class Avida {
public:
  // Tease apart types in PLUG_IN_Ts...
  using plug_in_pack = emp::TypePack<PLUG_IN_Ts...>;
  using hardware_pack = plug_in_pack::template filter<hardware_filter>;
  static_assert(hardware_pack::GetSize() == 1, "Currently, only one hardware type allowed.");

  using hardware_t = hardware_pack::first_t;
  using manager_t = HardwareManager<hardware_t>;
  using organism_t = Organism<hardware_t>;
  using population_t = Population<organism_t>;
  using genome_t = hardware_t::genome_t;

private:
  using hw_man_ptr = emp::Ptr<manager_t>;
  
  emp::Random random;
  population_t population{random};
  std::unordered_map<emp::String, hw_man_ptr> hw_man_map;

  // ===== Helper Functions =====
  void DoDivide(organism_t & org) {
    emp_assert(org.OK());
    genome_t offspring_genome = org.DivideGenome(random);
    if (offspring_genome.size()) {
      org.GetPopulation().DivideOrg(org, std::move(offspring_genome));
    }  
  }

  void AddCallbacks(manager_t & hw_man) {
    hw_man.AddCallback("DivideCell", [this](organism_t & org){ DoDivide(org); });
  }

public:
  Avida() { }
  Avida(emp::vector<emp::String> args) {
    for (size_t i = 1; i < args.size(); ++i) {
      if (args[i] == "-h" || args[i] == "--help") { PrintHelp(args[0], std::cout); }
      else if (args[i] == "-s" || args[i] == "--seed") {
        // Collect the seed.
        ++i;
        if (i >= args.size()) { emp::notify::Error("Must specify a random number seed."); }
        if (!args[i].OnlyDigits()) { emp::notify::Error("Random seed must be set to a whole number."); }
        random.ResetSeed(args[i].ConvertFromLiteral<uint64_t>());
      }
      else {
        emp::notify::Error("Unknown argument: ", args[i]);
      }
    }
  }
  ~Avida() {
    population.Clear();

    // Delete all of the hardware managers and the hardware they have.
    for ( auto & [name, ptr] : hw_man_map) ptr.Delete();
    hw_man_map.clear();
  }

  manager_t & GetHWManager(const emp::String & name) {
    emp_assert( emp::Has(hw_man_map, name) );
    return *hw_man_map[name];
  }

  template <typename VM_T>
  manager_t & AddHardwareManager(const emp::String & name) {
    emp_assert(!emp::Has(hw_man_map, name));
    return *(hw_man_map[name] = emp::NewPtr<HardwareManager<VM_T>>());
  }

  void PrintHelp(const emp::String & exec_name, std::ostream & os) const {
    os << "Format: " << exec_name << " [flags]\n"
       << "Allowed flags include:\n"
       << "  --help (or -h) : Print this message.\n"
       << std::endl;
    exit(0);
  }

  void Trace(AvidaVM & vm, size_t cpu_cycles=200) {
    for (size_t i = 0; i < cpu_cycles; ++i) {
      std::cout << "STEP " << i << ":\n" << vm.StatusString() << std::endl;
      vm.ProcessStep();
    }
    std::cout << "STEP " << cpu_cycles << ":\n" << vm.StatusString() << std::endl;
  }

  // Generate many random genomes of a given size and try running them.
  template <typename MANAGER_T=AvidaVM>
  bool Test(const size_t genome_size = 256, const size_t num_trials = 5000000, size_t run_time=200) {
    // Create the hardware manager.
    manager_t & hw_manager = AddHardwareManager<MANAGER_T>(MANAGER_T::DefaultName());
    auto inst_set = MANAGER_T::BuildInstSet();

    // Load in the default ancestor genome.
    auto exp_genome = inst_set.LoadGenome("../config/ancestor.org");
    if (!exp_genome) return false;
    organism_t org(hw_manager, *exp_genome);
    for (size_t trial = 0; trial < num_trials; ++trial) {
      if (trial % 100000 == 0) std::cout << "Trial: " << trial << std::endl; 
      // Build a random genome and run it.
      inst_set.BuildGenome(*exp_genome, genome_size, random);
      org.Process(run_time);
    }
    return true;
  }

  // Initialize the hardware manager (with AvidVM) and the population (with the default) ancestor.
  void Setup() {
    // Default to the AvidaVM hardware manager.
    manager_t & hw_man = AddHardwareManager<AvidaVM>("AvidaVM");
    AddCallbacks(hw_man);

    // Create a population called "main" and inject a single individual of the default ancestor.
    population.SetMaxSize(10000);
    population.Inject(hw_man, "../config/ancestor.org");
  }

  void DoUpdate() { population.ProcessUpdate(); }

  void Run(size_t ud_count=10000) {
    for (size_t ud = 0; ud < ud_count; ++ud) {
      if (ud % 100 == 0) {
        std::cout << "UD:" << ud
                  << "  Pop Size:" << population.size()
                  << "  Generation: " << population.GetAveGeneration()
                  << "  Genome0:[" << population[0].GetGenomeSequence() << "]"
                  << std::endl;
      }
      DoUpdate();
    }
    std::cout << "Final Pop Size = " << population.size() << std::endl;
    std::cout << "Total Cycles = " << population.GetCyclesExecuted() << std::endl;
  }
};