/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 *
 *  This is a starter plug-in module, with all of the signals listed, but none set to do anything.
 */

#include <cstddef>   // for size_t
#include <fstream>
#include <iostream>

#include "../core/Avida.hpp"

template <typename AVIDA_T>
class ModuleTemplate : public ModuleBase<AVIDA_T> {
private:
  AVIDA_T & avida;

  int example_setting = 42;
  int example_setting2 = 91;

public:
  ModuleTemplate(AVIDA_T & avida)
    : ModuleBase<AVIDA_T>("ModuleName", "ModuleType", "Module description text"), avida(avida) { }
  ~ModuleTemplate() { }

  // === General Configuration ===

  struct Phenotype {
    size_t example_trait = 0;
  };

  void RegisterTraits() {
    AVIDA_REGISTER_TRAIT(example_trait, "Describe your trait here");
  }

  void RegisterSettings() {
    avida.AddSetting("example_setting", example_setting, "Description of example setting", 'x');
    avida.AddSetting("example_setting2",
      [this](){ return example_setting2; },
      [this](int in){ example_setting2 = in; },
      "Description of example fuction-based setting", 'y');
  }

  void RegisterCallbacks() {
    avida.AddCallback("DivideCell", [this](size_t biota_id){ avida.DivideOrg(biota_id); });
  }

  // === Signal Listeners ===

  // Triggered: Once a the very beginning of a run, after all configurations have been loaded.
  void OnStart() { }

  // Triggered: Every update at the very beginning of the update.
  void OnUpdateStart([[maybe_unused]] size_t new_update) { }

  // Triggered: Every update at the end of the update.
  void OnUpdateEnd([[maybe_unused]] size_t old_update) { }

  // Triggered: When a parent is about to replicate, before starting to make the new organism.
  template <concepts::Organism ORG_T>
  void BeforeRepro([[maybe_unused]] ORG_T & parent) { }

  // Triggered: Offspring Organism has been identified and supplied genome (do mutations here!)
  template <concepts::Organism ORG_T>
  void OnOffspringInit([[maybe_unused]] ORG_T & org, [[maybe_unused]] ORG_T & parent) { }

  // Triggered: All other aspects of building an offspring are complete; last connection to parent.
  template <concepts::Organism ORG_T>
  void OnOffspringReady([[maybe_unused]] ORG_T & offspring, [[maybe_unused]] ORG_T & parent) { }

  // Triggered: Organism has been build from scratch to be injected into the population.
  template <concepts::Organism ORG_T>
  void OnInjectReady([[maybe_unused]] ORG_T & inject_org) { }

  // Triggered: Organism (either offspring or injected) is about the be placed into a population.
  template <concepts::Organism ORG_T>
  void BeforePlacement([[maybe_unused]] ORG_T & org) { }

  // Triggered: Organism has been placed in the population.
  template <concepts::Organism ORG_T>
  void OnPlacement([[maybe_unused]] ORG_T & org) { }

  // Triggered: Organism is about to die.
  template <concepts::Organism ORG_T>
  void BeforeDeath([[maybe_unused]] ORG_T & org) { }

  // Triggered: Run is about to end.
  void BeforeExit() { }

  // Triggered: User has requested help.
  void OnHelp() { }
};
