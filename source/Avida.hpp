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

#include "Hardware/AvidaVM.hpp"
#include "Hardware/HardwareManager.hpp"
#include "Population.hpp"

/// Main Avida-control object.
class Avida {
private:
  using hw_man_ptr = emp::Ptr<HardwareManager>;
  
  emp::Random random;
  std::unordered_map<emp::String, hw_man_ptr> hw_man_map;
  std::unordered_map<emp::String, Population> pop_map;

  // ===== Helper Functions =====
  void DoDivide(Organism & org) {
    emp_assert(org.OK());
    Genome offspring_genome = org.DivideGenome(random);
    if (offspring_genome.size()) {
      org.GetPopulation().DivideOrg(org, std::move(offspring_genome));
    }  
  }

  void AddCallbacks(HardwareManager & hw_man) {
    hw_man.AddCallback("DivideCell", [this](Organism & org){ DoDivide(org); });
  }

public:
  Avida() { }
  Avida(emp::vector<emp::String> args) {
    size_t arg_id = 1;
    while (arg_id < args.size()) {      
      if (args[arg_id].IsOneOf("-h", "--help")) { PrintHelp(args[0], std::cout); }
    }
  }
  ~Avida() {
    // Close down the populations, moving all of the hardware to the managers for deletion.
    pop_map.clear();

    // Delete all of the hardware managers and the hardware they have.
    for ( auto [name, ptr] : hw_man_map) ptr.Delete();
  }

  HardwareManager & GetHWManager(emp::String name) {
    emp_assert( emp::Has(hw_man_map, name) );
    return *hw_man_map[name];
  }

  template <typename VM_T>
  HardwareManager & AddHardwareManager(emp::String name) {
    emp_assert(!emp::Has(hw_man_map, name));
    return *(hw_man_map[name] = emp::NewPtr<HardwareType<VM_T>>());
  }

  Population & GetPopulation(emp::String name) {
    emp_assert( emp::Has(pop_map, name) );    
    return pop_map.find(name)->second;
  }

  Population & AddPopulation(emp::String name) {
    emp_assert(!emp::Has(pop_map, name));
    auto [it, success] = pop_map.emplace(name, random);
    emp_assert(success);
    return it->second;
  }

  void PrintHelp(emp::String exec_name, std::ostream & os) const {
    os << "Format: " << exec_name << " [flags]\n"
       << "Allowed flags include:\n"
       << "  --help (or -h) : Print this message.\n"
       << std::endl;
    exit(0);
  }

  void Trace(AvidaVM & vm, size_t cpu_cycles=200) {
    for (size_t i = 0; i < cpu_cycles; ++i) {
      std::cout << "STEP " << i << ":\n" << vm.StatusString() << std::endl;
      vm.ProcessInst();
    }
    std::cout << "STEP " << cpu_cycles << ":\n" << vm.StatusString() << std::endl;
  }

  // Generate many random genomes of a given size and try running them.
  void Test(const size_t genome_size = 256, const size_t num_trials = 5000000, size_t run_time=200) {
    // Create the AvidaVM hardware manager.
    HardwareManager & hw_manager = AddHardwareManager<AvidaVM>("AvidaVM");
    auto inst_set = AvidaVM::BuildInstSet();

    // Load in the default ancestor genome.
    Genome genome = inst_set.LoadGenome("../config/ancestor.org");
    AvidaVM org(hw_manager, genome);
    for (size_t trial = 0; trial < num_trials; ++trial) {
      if (trial % 100000 == 0) std::cout << "Trial: " << trial << std::endl; 
      // Build a random genome and run it.
      inst_set.BuildGenome(genome, genome_size, random);
      org.Process(run_time);
    }
  }

  // Initialize the hardware manager (with AvidVM) and the population (with the default) ancestor.
  void Setup() {
    // Default to the AvidaVM hardware manager.
    HardwareManager & hw_man = AddHardwareManager<AvidaVM>("AvidaVM");
    AddCallbacks(hw_man);

    // Create a population called "main" and inject a single individual of the default ancestor.
    Population & pop = AddPopulation("main");
    pop.SetMaxSize(10000);
    pop.Inject(hw_man, "../config/ancestor.org");
  }

  void Run(Population & pop) {
    int64_t total_cycles = 0;
    for (size_t ud = 0; ud < 10000; ++ud) {
      if (ud % 100 == 0) {
        std::cout << "UD:" << ud
                  << "  Pop Size:" << pop.size()
                  << "  Generation: " << pop.GetAveGeneration()
                  << "  Genome0:[" << pop[0].GetGenomeSequence() << "]"
                  << std::endl;
      }
      total_cycles += pop.ProcessUpdate();
    }
    std::cout << "Final Pop Size = " << pop.size() << std::endl;
    std::cout << "Total Cycles = " << total_cycles << std::endl;
  }

  void Run() {
    Population & pop = AddPopulation("main");
    Run(pop);
  }
};