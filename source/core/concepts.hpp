#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 */

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>  // std::filesystem::path
#include <iosfwd>
#include <tuple>
#include <type_traits>
#include <utility>

#include "emp/math/Random.hpp"
#include "emp/tools/String.hpp"

namespace concepts {

  // ===  Helper Concepts  ===

  template <typename T>
  concept StringLike =
    std::convertible_to<T, emp::String> ||
    std::same_as<std::remove_cvref_t<T>, emp::String>;


  // ===  Genome  ===

  template <typename G>
  concept Genome = requires(G genome, const G const_genome, std::size_t pos, std::size_t len, typename G::value_t value, std::ostream& os) {
    typename G::value_t;

    { const_genome.size() } -> std::convertible_to<std::size_t>;
    { const_genome[pos] } -> std::convertible_to<typename G::value_t>;

    { genome.Push(value) } -> std::same_as<void>;
    { genome.Insert(pos, value) } -> std::same_as<void>;
    { genome.Extract(pos, len) } -> std::same_as<G>;

    // Optional conveniences; keep them required only if you rely on them widely.
    { genome.Clear() } -> std::same_as<void>;
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
    std::filesystem::path filepath,
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
    { inst_set.LoadGenome(filepath) }  -> std::same_as<std::expected<typename INST_SET_T::genome_t, emp::String>>;
  };


  // ===  Hardware (VM)  ===

  template <typename HARDWARE_T>
  concept Hardware = requires(
      HARDWARE_T hardware,
      const HARDWARE_T const_hardware,
      std::size_t value,
      HARDWARE_T::inst_set_t & inst_set
    ) {
    typename HARDWARE_T::genome_t;

    // Core execution
    { hardware.ProcessStep() } -> std::same_as<void>;

    // Reproduction
    { hardware.DivideGenome() } -> std::same_as<typename HARDWARE_T::genome_t>;

    // Organism linkage
    { hardware.SetBiotaID(value) } -> std::same_as<HARDWARE_T &>;
    { const_hardware.GetBiotaID() } -> std::convertible_to<size_t>;

    // Health check
    { const_hardware.OK() } -> std::convertible_to<bool>;

    // Static identity + inst set construction
    { HARDWARE_T::HardwareName() } -> StringLike;
    { HARDWARE_T::BuildInstSet(inst_set) };
  };


  // ===  Organism  ===

  template <typename ORG_T>
  concept Organism = requires(
      ORG_T org,
      const ORG_T const_org,
      std::size_t id,
      std::size_t cycles
    ) {
    typename ORG_T::genome_t;
    typename ORG_T::hardware_t;

    { const_org.GetGenome() } -> std::same_as<const typename ORG_T::genome_t&>;
    { const_org.GetGenomeSequence() } -> std::same_as<emp::String>;

    { org.SetBiotaID(id) } -> std::same_as<ORG_T&>;
    { org.SetGlobalID(id) } -> std::same_as<ORG_T&>;

    { org.Process(cycles) } -> std::same_as<ORG_T&>;
    { org.DivideGenome() } -> std::same_as<typename ORG_T::genome_t>;
  };

} // namespace concepts
