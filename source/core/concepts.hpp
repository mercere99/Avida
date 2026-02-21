#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 */

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iosfwd>
#include <tuple>
#include <type_traits>
#include <utility>

#include "emp/math/Random.hpp"
#include "emp/tools/String.hpp"

namespace avida::concepts {

  // ===  Helper Concepts  ===

  template <typename T>
  concept StringLike =
    std::convertible_to<T, emp::String> ||
    std::same_as<std::remove_cvref_t<T>, emp::String>;

  // === Signals ===

  // Class reacts to signal: New update has just started.
  template <typename T>
  concept HasOnUpdateStart = requires(T plugin, size_t new_update) {
    { plugin.OnUpdateStart(new_update) } -> std::convertible_to<bool>;
  };

  // Class reacts to signal: Update is ending; new one is about to start
  template <typename T>
  concept HasOnUpdateEnd = requires(T plugin, size_t old_update) {
    { plugin.OnUpdateEnd(old_update) } -> std::convertible_to<bool>;
  };

  // Class reacts to signal: Parent is about to (try to) reproduce.
  template <typename T>
  concept HasBeforeRepro = requires(T plugin, typename T::organism_t & parent) {
    { plugin.BeforeRepro(parent) } -> std::convertible_to<bool>;
  };

  // Class reacts to signal: Offspring is ready to be placed.
  template <typename T>
  concept HasOnOffspringReady = requires(T plugin, typename T::organism_t & offspring,
                                         typename T::organism_t &  parent) {
    { plugin.OnOffspringReady(offspring, parent) } -> std::convertible_to<bool>;
  };

  // Class reacts to signal: Organism to be injected into pop is ready to be placed.
  template <typename T>
  concept HasOnInjectReady = requires(T plugin, typename T::organism_t & inject_org) {
    { plugin.OnInjectReady(inject_org) } -> std::convertible_to<bool>;
  };

  // Class reacts to signal: Placement location has been identified (For birth or inject)
  template <typename T>
  concept HasBeforePlacement = requires(T plugin, typename T::organism_t & org, size_t target_pos, size_t parent_pos) {
    { plugin.BeforePlacement(org, target_pos, parent_pos) } -> std::convertible_to<bool>;
  };

  // Class reacts to signal: New organism has been placed in the population.
  template <typename T>
  concept HasOnPlacement = requires(T plugin, typename T::organism_t & org) {
    { plugin.OnPlacement(org) } -> std::convertible_to<bool>;
  };

  // Class reacts to signal: Mutate is about to run on an organism.
  template <typename T>
  concept HasBeforeMutate = requires(T plugin, typename T::organism_t & org) {
    { plugin.BeforeMutate(org) } -> std::convertible_to<bool>;
  };

  // Class reacts to signal: Organism has had its genome changed due to mutation.
  template <typename T>
  concept HasOnMutate = requires(T plugin, typename T::organism_t & org) {
    { plugin.OnMutate(org) } -> std::convertible_to<bool>;
  };

  // Class reacts to signal: Organism is about to die.
  template <typename T>
  concept HasBeforeDeath = requires(T plugin, typename T::organism_t & org) {
    { plugin.BeforeDeath(org) } -> std::convertible_to<bool>;
  };

  // Class reacts to signal: Run immediately before Avida exits.
  template <typename T>
  concept HasBeforeExit = requires(T plugin) {
    { plugin.BeforeExit() } -> std::convertible_to<bool>;
  };

  // Class reacts to signal: Run when the --help option is called at startup.
  template <typename T>
  concept HasOnHelp = requires(T plugin) {
    { plugin.OnHelp() } -> std::convertible_to<bool>;
  };


  // ===  Genome  ===

  template <typename G>
  concept Genome = requires(G genome, const G const_genome, std::size_t pos, std::size_t len, typename G::value_t id, std::ostream& os) {
    typename G::value_t;

    { const_genome.size() } -> std::convertible_to<std::size_t>;
    { const_genome[pos] } -> std::convertible_to<typename G::value_t>;

    { genome.Push(id) } -> std::same_as<void>;
    { genome.Insert(pos, id) } -> std::same_as<void>;
    { genome.Extract(pos, len) } -> std::same_as<G>;

    // Optional conveniences; keep them required only if you rely on them widely.
    { genome.Clear() } -> std::same_as<void>;
    { const_genome.Print(os) } -> std::same_as<void>;
  };


  // ===  InstSet  ===

  template <typename INST_SET_T>
  concept InstSet = requires(
    const INST_SET_T inst_set,
    typename INST_SET_T::hardware_t & hardware,
    typename INST_SET_T::genome_t & genome,
    const typename INST_SET_T::genome_t & const_genome,
    emp::Random & random,
    emp::String str,
    char symbol,
    std::istream & in,
    std::size_t n
  ) {
    typename INST_SET_T::hardware_t;
    typename INST_SET_T::genome_t;
    typename INST_SET_T::inst_id_t;

    { inst_set.size() } -> std::convertible_to<std::size_t>;

    { inst_set.GetName(n) } -> std::same_as<const emp::String&>;
    { inst_set.GetSymbol(n) } -> std::same_as<char>;

    { inst_set.GetID(str) } -> std::convertible_to<typename INST_SET_T::inst_id_t>;
    { inst_set.GetID(symbol) } -> std::convertible_to<typename INST_SET_T::inst_id_t>;

    { inst_set.Execute(hardware, n) } -> std::same_as<void>;

    { inst_set.BuildGenome(genome, str) } -> std::same_as<void>;
    { inst_set.BuildGenome(str) } -> std::same_as<typename INST_SET_T::genome_t>;
    { inst_set.BuildGenome(genome, n, random, 0.5) } -> std::same_as<void>;

    { inst_set.ToSequence(const_genome) } -> std::same_as<emp::String>;

    { inst_set.LoadGenome(in) } -> std::same_as<std::expected<typename INST_SET_T::genome_t, emp::String>>;
    { inst_set.LoadGenome(str) }  -> std::same_as<std::expected<typename INST_SET_T::genome_t, emp::String>>;
  };


  // ===  Hardware (VM)  ===

  template <typename HARDWARE_T>
  concept Hardware = requires(
      HARDWARE_T hardware,
      const HARDWARE_T const_hardware,
      emp::Random & random,
      std::size_t cycles
    ) {
    typename HARDWARE_T::genome_t;
    typename HARDWARE_T::manager_t;
    typename HARDWARE_T::organism_t;

    // Core execution
    { hardware.Process(cycles) } -> std::same_as<void>;
    { hardware.ProcessStep() } -> std::same_as<void>;

    // Reproduction
    { hardware.DivideGenome(random) } -> std::same_as<typename HARDWARE_T::genome_t>;

    // Organism linkage
    { hardware.SetOrganism(std::declval<typename HARDWARE_T::organism_t&>()) } -> std::same_as<HARDWARE_T&>;
    { hardware.GetOrganism() } -> std::same_as<typename HARDWARE_T::organism_t&>;
    { const_hardware.GetOrganism() } -> std::same_as<const typename HARDWARE_T::organism_t&>;

    // Manager access
    { hardware.GetManager() } -> std::same_as<typename HARDWARE_T::manager_t&>;
    { const_hardware.GetManager() } -> std::same_as<const typename HARDWARE_T::manager_t&>;

    // Health check
    { const_hardware.OK() } -> std::convertible_to<bool>;

    // Static identity + inst set construction
    { HARDWARE_T::HardwareName() } -> StringLike;
    { HARDWARE_T::BuildInstSet() };
  };


  // ===  HardwareManager  ===

  template <typename MANAGER_T>
  concept HardwareManager = requires(
      MANAGER_T hardware_manager,
      const MANAGER_T const_hardware_manager,
      emp::Random & random,
      typename MANAGER_T::genome_t & genome,
      const typename MANAGER_T::genome_t & const_genome,
      emp::String filename
    ) {
    typename MANAGER_T::hardware_t;
    typename MANAGER_T::genome_t;
    typename MANAGER_T::org_t;
    typename MANAGER_T::hw_ptr_t;

    { MANAGER_T::DefaultName() } -> StringLike;

    { hardware_manager.Allocate(std::declval<typename MANAGER_T::org_t&>()) } -> std::same_as<typename MANAGER_T::hw_ptr_t>;
    { hardware_manager.Release(std::declval<typename MANAGER_T::hw_ptr_t>()) } -> std::same_as<void>;

    { hardware_manager.Mutate(random, genome) } -> std::same_as<void>;

    { const_hardware_manager.ToSequence(const_genome) } -> std::same_as<emp::String>;
    { const_hardware_manager.LoadGenome(filename) };

    { hardware_manager.GetInstSet() };
    { const_hardware_manager.GetInstSet() };

    // InstSet type consistency (light check)
    requires InstSet<std::remove_reference_t<decltype(hardware_manager.GetInstSet())>>;
  };


  // ===  Organism  ===

  template <typename ORG_T>
  concept Organism = requires(
      ORG_T org,
      const ORG_T const_org,
      emp::Random & random,
      std::size_t position,
      std::size_t cycles
    ) {
    typename ORG_T::genome_t;
    typename ORG_T::hardware_t;

    { const_org.GetGenome() } -> std::same_as<const typename ORG_T::genome_t&>;
    { const_org.GetGenomeSequence() } -> std::same_as<emp::String>;

    { const_org.GetMetabolicRate() } -> std::convertible_to<double>;
    { const_org.GetGeneration() } -> std::convertible_to<std::uint32_t>;

    { org.SetPosition(position) } -> std::same_as<ORG_T&>;

    { org.Process(cycles) } -> std::same_as<ORG_T&>;
    { org.DivideGenome(random) } -> std::same_as<typename ORG_T::genome_t>;
  };

} // namespace avida::concepts
