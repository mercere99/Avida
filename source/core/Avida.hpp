#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 * 
 *  DEVELOPER notes:
 *  - consider keeping occupied count (pop size) separate from occupied BitVector.
 *  - consider an open cells stack to check for empty positions.
 *  - if both of the above can be done, we can shift to a byte-sized occupied "flag" (with other info?)
 */

// Main Avida controller class.

#include "emp/base/Ptr.hpp"
#include "emp/base/vector.hpp"
#include "emp/bits/BitVector.hpp"
#include "emp/config/command_line.hpp"
#include "emp/datastructs/map_utils.hpp"
#include "emp/datastructs/UnorderedIndexMap.hpp"
#include "emp/meta/TypePack.hpp"

#include "../Hardware/AvidaVM.hpp"
#include "../Hardware/HardwareManager.hpp"
#include "concepts.hpp"

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
  using population_t = emp::vector<organism_t>;
  using genome_t = hardware_t::genome_t;

private:
  using hw_man_ptr = emp::Ptr<manager_t>;
  
  emp::Random random;
  population_t population{};
  emp::BitVector occupied{};
  std::unordered_map<emp::String, hw_man_ptr> hw_man_map;

  //======> @CAO - move this block to plug-in?
  emp::UnorderedIndexMap speed_map;                 // Relative speed of each virtual machine.
  int32_t pop_cap = 10000;                          // Population size limit (default: 10,000 orgs)
  static constexpr int32_t ave_cycles_per_org = 30; // Total cycles to execute per org per update.
  static constexpr int32_t CPU_chunk_size = 10;     // Num cycles executed each time org is picked.
  int64_t cycles_executed = 0;                      // How many CPU cycles have been run so far?

  // ===== Helper Functions =====
  // Delete an organism at a specific position from the population.
  void DeleteOrg(size_t delete_id) {
    emp_assert(delete_id < population.size());

    population[delete_id].SignalDeath();  // Signal org prior to deletion.
    speed_map.Set(delete_id, 0.0);   // Set old index speed to 0; don't shrink speed_map
    occupied.Clear(delete_id);
  }

  // Delete a random (occupied) organism position.
  void DeleteOrg() {
    emp_assert(GetNumOrgs() > 0); // Must have something to delete!
    size_t delete_id = random.GetUInt32(population.size());
    if (occupied[delete_id]) DeleteOrg(delete_id);
    else DeleteOrg(); // Our random choice did not work... try again!
  }

  void PlaceOrganism(organism_t && org) {
    emp_assert(occupied.size() == population.size());

    // See if we must delete an organism before we can add a new one.
    if (GetNumOrgs() == pop_cap) DeleteOrg();

    size_t index = occupied.FindZero();
    if (index == emp::BitVector::npos) {
      index = occupied.PushBack(true);
      population.emplace_back(std::move(org));
    } else {
      occupied.Set(index);
      population[index] = std::move(org);
    }

    population[index].SetPosition(index);
    speed_map.Set(index, population[index].GetMetabolicRate());
  }

  void DoDivide(organism_t & parent_org) {
    emp_assert(parent_org.OK());
    genome_t offspring_genome = parent_org.DivideGenome(random);
    if (offspring_genome.size()) {
      PlaceOrganism( organism_t{parent_org, std::move(offspring_genome)} );
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
    population.clear();

    // Delete all of the hardware managers and the hardware they have.
    for ( auto & [name, ptr] : hw_man_map) ptr.Delete();
    hw_man_map.clear();
  }

  [[nodiscard]] int32_t GetNumOrgs() const { return occupied.CountOnes(); }

  template <typename FUN_T>
  [[nodiscard]] double GetAveTrait(const FUN_T & fun) const {
    double total = 0.0;    
    for (size_t index : occupied) total += fun(population[index]);
    return total / GetNumOrgs();
  }

  [[nodiscard]] double GetAveGeneration() const {
    return GetAveTrait([](const organism_t & org){ return org.GetGeneration(); });
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

    // Inject a single individual of the default ancestor.
    pop_cap = 10000;
    Inject(hw_man, "../config/ancestor.org");
  }

  void Inject(manager_t & hw_man, genome_t && genome) {
    return PlaceOrganism(organism_t{hw_man, std::move(genome)});
  }
  
  /// @brief Inject an organism using a genome loaded from a file.
  /// @param hw_man - The hardware manager for this CPU type.
  /// @param filename - The name of the file with the genome information.
  /// @return A reference to this population object.
  void Inject(manager_t & hw_man, const emp::String & filename) {
    auto exp_genome = hw_man.LoadGenome(filename);
    if (!exp_genome) {
      emp::notify::Error("Failed to inject from file '", filename, "'.");
    }

    return Inject(hw_man, std::move(*exp_genome));
  }

  // Process a single update for Avida
  void DoUpdate() {
    emp_assert(GetNumOrgs() > 0, "Running DoUpdate() with no organisms.");
    const int32_t cycles = GetNumOrgs() * ave_cycles_per_org;
    for (int32_t rounds = cycles / CPU_chunk_size; rounds; --rounds) {
      const size_t id = speed_map.Index(random.GetDouble(speed_map.GetWeight()));
      population[id].Process(CPU_chunk_size);
    }
    cycles_executed += cycles;
  }

  void Run(size_t ud_count=10000) {
    for (size_t ud = 0; ud < ud_count; ++ud) {
      if (ud % 100 == 0) {
        std::cout << "UD:" << ud
                  << "  Pop Size:" << GetNumOrgs()
                  << "  Generation: " << GetAveGeneration()
                  << "  Genome0:[" << population[0].GetGenomeSequence() << "]"
                  << std::endl;
      }
      DoUpdate();
    }
    std::cout << "Final Pop Size = " << population.size() << std::endl;
    std::cout << "Total Cycles = "   << cycles_executed << std::endl;
  }
};