#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 * 
 *  This is the main controller class for Avida.
 * 
 *  DEVELOPER notes:
 *  - Move signals to their own class?
 *  - Shift to a byte-sized occupied "flag" (with other info?)
 *  - Allow hardware variant to form special type of organism that can use any listed hardware.
 */

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

namespace avida {

  template <concepts::Hardware HW_T> struct hardware_filter { };

  /// Main Avida-control object.
  template <typename HARDWARE_T, template <typename> typename... PLUG_IN_Ts>
  class Avida {
  public:
    static_assert(sizeof...(PLUG_IN_Ts) > 0, "At least one Avida plug-in required to manage run.");
    using this_t = Avida<HARDWARE_T, PLUG_IN_Ts...>;

    using hardware_t = HARDWARE_T;                  // Type of general hardware for all organisms
    using manager_t = HardwareManager<hardware_t>;  // Type of manager to recycle hardware
    using organism_t = Organism<hardware_t>;        // Type for each individual organism
    using biota_t = emp::vector<organism_t>;        // Collection of all current organisms in Avida
    using genome_t = hardware_t::genome_t;          // Type of genomes used in organisms

  private:    
    std::tuple<PLUG_IN_Ts<this_t>...> plug_ins;

    size_t update = 0;          // Times update was run on this population
    emp::Random random;         // Central random number generator
    biota_t biota{};            // Collection of all current organisms
    emp::BitVector occupied{};  // Which organisms in biota are active?
    size_t org_count = 0;       // Current number of active organisms

    std::unordered_map< emp::String, emp::Ptr<manager_t> > hw_man_map{};

    // ===== Helper Functions =====
    void PlaceOrganism(organism_t && org_to_place) {
      emp_assert(occupied.size() == biota.size());
      emp_assert(org_count.CountOnes() == org_count);

      Signal_BeforePlacement(org_to_place);

      size_t index = occupied.FindZero();    // Find empty position in biota.
      if (index == emp::BitVector::npos) {   // If no empty position available, add one.
        index = occupied.PushBack(true);
        biota.emplace_back(std::move(org_to_place));
      } else {
        occupied.Set(index);
        biota[index] = std::move(org_to_place);
      }

      ++org_count;
      organism_t & placed_org = biota[index];  // org_to_place was moved out; grab new location.
      placed_org.SetPosition(index);

      Signal_OnPlacement(placed_org);
    }

    void AddCallbacks(manager_t & hw_man) {
      hw_man.AddCallback("DivideCell", [this](organism_t & org){ DivideOrg(org); });
    }


    // === Signals ===
    void TriggerSignal(auto signal_fun) {
      std::apply([&signal_fun](auto &... p){ (signal_fun(p), ...); }, plug_ins);
    }

    #define AVIDA_SIGNAL_DEF(TRIGGER, ...)                                 \
      TriggerSignal([__VA_ARGS__](auto & plug_in) {                        \
        if constexpr (requires { plug_in.TRIGGER; }) { plug_in.TRIGGER; }  \
      });

    void Signal_OnUpdateStart(size_t update) { AVIDA_SIGNAL_DEF(OnUpdateStart(update), update); }
    void Signal_OnUpdateEnd(size_t update) { AVIDA_SIGNAL_DEF(OnUpdateEnd(update), update); }

    void Signal_BeforeRepro(organism_t & parent) { AVIDA_SIGNAL_DEF(BeforeRepro(parent), &parent); }

    void Signal_OnOffspringReady(organism_t & offspring, organism_t & parent) {
      AVIDA_SIGNAL_DEF(OnOffspringReady(offspring, parent), &offspring, &parent);
    }

    void Signal_OnInjectReady(organism_t & org) { AVIDA_SIGNAL_DEF(OnInjectReady(org), &org); }
    void Signal_BeforePlacement(organism_t & org) { AVIDA_SIGNAL_DEF(BeforePlacement(org), &org); }
    void Signal_OnPlacement(organism_t & org) { AVIDA_SIGNAL_DEF(OnPlacement(org), &org); }
    void Signal_BeforeDeath(organism_t & org) { AVIDA_SIGNAL_DEF(BeforeDeath(org), &org); }

    // @CAO TODO: Call these
    void Signal_BeforeMutate(organism_t & org) { AVIDA_SIGNAL_DEF(BeforeMutate(org), &org); }
    void Signal_OnMutate(organism_t & org) { AVIDA_SIGNAL_DEF(OnMutate(org), &org); }

    void Signal_BeforeExit() { AVIDA_SIGNAL_DEF(BeforeExit(), ); }
    void Signal_OnHelp() { AVIDA_SIGNAL_DEF(OnHelp(), ); }

  public:
    Avida() : plug_ins(PLUG_IN_Ts<this_t>{*this}...) { }
    Avida(emp::vector<emp::String> args) : Avida() {
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
    ~Avida() { Exit(); }

    // === Basic Accessors ===

    [[nodiscard]] int32_t GetNumOrgs() const { return org_count; }

    [[nodiscard]] emp::Random & GetRandom() { return random; }

    template <typename FUN_T>
    [[nodiscard]] double GetAveTrait(const FUN_T & fun) const {
      double total = 0.0;    
      for (size_t index : occupied) total += fun(biota[index]);
      return total / GetNumOrgs();
    }

    [[nodiscard]] double GetAveGeneration() const {
      return GetAveTrait([](const organism_t & org){ return org.GetGeneration(); });
    }

    template <typename PLUG_IN_T>
    [[nodiscard]] PLUG_IN_T & GetPlugIn() { return std::get<PLUG_IN_T>(plug_ins); }

    template <template <typename> typename PLUG_IN_T>
    [[nodiscard]] PLUG_IN_T<this_t> & GetPlugIn() { return std::get<PLUG_IN_T<this_t>>(plug_ins); }

    template <size_t INDEX>
    [[nodiscard]] auto & GetPlugIn() { return std::get<INDEX>(plug_ins); }

    manager_t & GetHWManager(const emp::String & name) {
      emp_assert( emp::Has(hw_man_map, name) );
      return *hw_man_map[name];
    }

    template <typename VM_T>
    manager_t & AddHardwareManager(const emp::String & name) {
      emp_assert(!emp::Has(hw_man_map, name));
      return *(hw_man_map[name] = emp::NewPtr<HardwareManager<VM_T>>());
    }

    void PrintHelp(const emp::String & exec_name, std::ostream & os) {
      os << "Format: " << exec_name << " [flags]\n"
        << "Allowed flags include:\n"
        << "  --help (or -h) : Print this message.\n"
        << std::endl;
      Signal_OnHelp();  // Allow plug-ins to provide help information.
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


    // ====== Configuration Management ======

    // Initialize the hardware manager (with AvidVM) and the biota (with the default) ancestor.
    void Setup() {
      // Default to the AvidaVM hardware manager.
      manager_t & hw_man = AddHardwareManager<AvidaVM>("AvidaVM");
      AddCallbacks(hw_man);

      // Inject a single individual of the default ancestor.
      Inject(hw_man, "../config/ancestor.org");
    }


    // ====== Organism Management ======

    [[nodiscard]] auto & GetOrg(this auto & self, size_t id) { return self.biota[id]; }
    [[nodiscard]] auto & GetFirstOrg(this auto & self) { return self.biota[self.occupied.FindOne()]; }

    void Inject(manager_t & hw_man, genome_t && genome) {
      organism_t inject_org{hw_man, std::move(genome)};
      Signal_OnInjectReady(inject_org);
      return PlaceOrganism(std::move(inject_org));
    }
    
    /// @brief Inject an organism using a genome loaded from a file.
    /// @param hw_man - The hardware manager for this CPU type.
    /// @param filename - The name of the file with the genome information.
    void Inject(manager_t & hw_man, const emp::String & filename) {
      auto exp_genome = hw_man.LoadGenome(filename);
      if (!exp_genome) {
        emp::notify::Error("Failed to inject from file '", filename, "'.");
      }

      return Inject(hw_man, std::move(*exp_genome));
    }

    // Collect an offspring from a designated parent organism.
    void DivideOrg(organism_t & parent_org) {
      emp_assert(parent_org.OK());
      Signal_BeforeRepro(parent_org);
      genome_t offspring_genome = parent_org.DivideGenome(random);
      if (offspring_genome.size()) {
        organism_t offspring{parent_org, std::move(offspring_genome)};
        Signal_OnOffspringReady(offspring, parent_org);
        PlaceOrganism( std::move(offspring) );
      }
    }

    // Delete an organism at a specific position in the biota.
    void DeleteOrg(size_t delete_id) {
      emp_assert(delete_id < biota.size());

      Signal_BeforeDeath(biota[delete_id]); // Notify plug-ins of impending death.
      biota[delete_id].SignalDeath();       // Notify organism before deletion.
      occupied.Clear(delete_id);
      --org_count;
    }

    // Delete a random (occupied) organism position.
    void DeleteOrg() {
      emp_assert(GetNumOrgs() > 0); // Must have something to delete!
      size_t delete_id = random.GetUInt32(biota.size());
      if (occupied[delete_id]) {
        DeleteOrg(delete_id);
      }
      else DeleteOrg(); // Our random choice did not work... try again!
    }


    // ====== Run Management ======

    // Process a single update for Avida
    void DoUpdate() {
      emp_assert(GetNumOrgs() > 0, "Running DoUpdate() with no organisms.");
      Signal_OnUpdateEnd(update);
      ++update;
      Signal_OnUpdateStart(update);
    }

    void ProcessOrg(size_t id, int32_t num_cycles) {
      biota[id].Process(num_cycles);
    }

    void Run() { while (true) DoUpdate(); }

    void Exit() {
      Signal_BeforeExit();  // Notify plug-ins of deletion.
      biota.clear();

      // Delete all of the hardware managers and the hardware they have.
      for ( auto & [name, ptr] : hw_man_map) ptr.Delete();
      hw_man_map.clear();
    }
  };

} // namespace avida