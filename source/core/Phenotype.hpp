#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 *
 *  This is the handler for tracking per-organism traits and behaviors.
 */

#include "emp/base/Ptr.hpp"
#include "emp/datastructs/RobinHoodMap.hpp"
#include "emp/meta/TypeID.hpp"
#include "emp/tools/String.hpp"

// Register a phenotype member as a named trait accessible by other modules.
// The generated accessors operate on organism_t so callers never need .GetPhenotype().
#define AVIDA_REGISTER_TRAIT(NAME, DESC)                                          \
  avida.template RegisterTrait<decltype(Phenotype::NAME)>(                        \
     #NAME, DESC, __FILE__, __LINE__,                                             \
     [](AVIDA_T::organism_t & o) -> auto & { return o.GetPhenotype().NAME; },     \
     [](const AVIDA_T::organism_t & o) -> const auto & { return o.GetPhenotype().NAME; } \
  );

#define AVIDA_REQUIRE_TRAIT(TYPE, NAME)                              \
static_assert(                                                       \
    requires(const typename AVIDA_T::phenotype_t & p) {              \
        { p.NAME } -> std::convertible_to<TYPE>;                     \
    },                                                               \
    "DriverBasic requires a '" #NAME "' trait of type '" #TYPE ". "  \
    "Add a compatible module to your module list.");

// Base class for all traits; provides generic (virtual) access by name.
// For performance in hot loops, prefer TraitManager::GetTyped<T>() to avoid virtual dispatch.
template <typename AVIDA_T>
class TraitBase {
protected:
  emp::String name;      // Unique name for this trait in the phenotype.
  emp::String desc;      // Full description of this trait.
  emp::String filename;  // Name of file where this trait was defined.
  size_t line;           // Line number where this trait was defined.

  using organism_t = typename AVIDA_T::organism_t;

public:
  TraitBase(const emp::String & name, const emp::String & desc, emp::String filename, size_t line)
    : name(name), desc(desc), filename(filename), line(line) { }
  virtual ~TraitBase() = default;

  [[nodiscard]] emp::String GetName() const { return name; }
  [[nodiscard]] emp::String GetDesc() const { return desc; }
  [[nodiscard]] emp::String GetFilename() const { return filename; }
  [[nodiscard]] size_t GetLine() const { return line; }
  [[nodiscard]] emp::String GetLocation() const { return emp::MakeString(filename, ':', line); }

  [[nodiscard]] virtual emp::TypeID GetTypeID() const = 0;
  [[nodiscard]] virtual double AsDouble(const organism_t &) const = 0;
  [[nodiscard]] virtual emp::String AsString(const organism_t &) const = 0;
  [[nodiscard]] virtual std::span<double> AsSpan(const organism_t &) const = 0;

  virtual void SerializeOrg(emp::SerialPod & pod, organism_t & org) = 0;
};

// Typed trait: stores direct organism-level accessors, avoiding virtual dispatch.
// Retrieve via TraitManager::GetTyped<TRAIT_T>() for use in performance-sensitive loops.
template <typename TRAIT_T, typename AVIDA_T>
class Trait : public TraitBase<AVIDA_T> {
private:
  using base_t = TraitBase<AVIDA_T>;
  using organism_t = typename AVIDA_T::organism_t;
  using get_fun_t  = std::function<TRAIT_T &(organism_t &)>;
  using cget_fun_t = std::function<const TRAIT_T &(const organism_t &)>;

  get_fun_t  get_fun;
  cget_fun_t cget_fun;

public:
  using trait_t = TRAIT_T;

  Trait(const emp::String & name, const emp::String & desc,
        const emp::String & filename, size_t line,
        get_fun_t get_fun, cget_fun_t cget_fun)
    : base_t(name, desc, filename, line)
    , get_fun(std::move(get_fun)), cget_fun(std::move(cget_fun)) {}

  [[nodiscard]] TRAIT_T &       Get(organism_t & o)       { return get_fun(o); }
  [[nodiscard]] const TRAIT_T & Get(const organism_t & o) const { return cget_fun(o); }
  [[nodiscard]] const get_fun_t  & GetAccessFun()      const { return get_fun; }
  [[nodiscard]] const cget_fun_t & GetConstAccessFun() const { return cget_fun; }

  [[nodiscard]] emp::TypeID GetTypeID() const override { return emp::GetTypeID<TRAIT_T>(); }

  [[nodiscard]] double AsDouble(const organism_t & o) const override {
    if constexpr (std::convertible_to<TRAIT_T, double>) {
      return static_cast<double>(cget_fun(o));
    } else {
      emp::notify::Error("Cannot convert trait '", base_t::name, "' to type double (is type ",
                         GetTypeID(), ")");
      return 0.0;
    }
  }

  [[nodiscard]] emp::String AsString(const organism_t & o) const override {
    if constexpr (std::formattable<const TRAIT_T, char>) {
      return emp::MakeString(cget_fun(o));
    } else {
      return emp::MakeString("[", emp::GetTypeID<TRAIT_T>().GetName(), "]");
    }
  }

  [[nodiscard]] std::span<double> AsSpan(const organism_t & o) const override {
    if constexpr (std::convertible_to<TRAIT_T, std::span<double>>) {
      return cget_fun(o);
    } else {
      emp::notify::Error("Cannot convert trait '", base_t::name, "' to span<double> (is type ",
                         GetTypeID(), ")");
      return std::span<double>{};
    }
  }

  void SerializeOrg(emp::SerialPod & pod, organism_t & org) override {
    pod(get_fun(org));
  }
};

template <typename AVIDA_T>
class TraitManager {
private:
  using trait_base_t = TraitBase<AVIDA_T>;
  using organism_t = typename AVIDA_T::organism_t;
  emp::RobinHoodMap<emp::String, emp::Ptr<trait_base_t>> trait_map;

public:
  ~TraitManager() { Clear(); }

  // Generic lookup — use for AsDouble/AsString/AsSpan (virtual, safe for any type).
  const trait_base_t & Get(const emp::String & name) const {
    auto trait_ptr = trait_map.FindValue(name, nullptr);
    emp_assert(trait_ptr, "Requesting an invalid trait name", name);
    return *trait_ptr;
  }

  // Typed lookup — returns a Trait<TRAIT_T> reference so callers can use Get() or
  // GetAccessFun() without virtual dispatch. Use this in performance-sensitive loops.
  template <typename TRAIT_T>
  const Trait<TRAIT_T, AVIDA_T> & GetTyped(const emp::String & name) const {
    const trait_base_t & base = Get(name);
    emp_assert(base.GetTypeID() == emp::GetTypeID<TRAIT_T>(),
               "trait type mismatch for '", name, "': expected ",
               emp::GetTypeID<TRAIT_T>(), " but got ", base.GetTypeID());
    return static_cast<const Trait<TRAIT_T, AVIDA_T> &>(base);
  }

  template <typename TRAIT_T>
  void Register(const emp::String & name, const emp::String & desc,
                const emp::String & filename, size_t line,
                auto get_fun, auto cget_fun)
  {
    using reg_t = Trait<TRAIT_T, AVIDA_T>;
    emp::Ptr<reg_t> reg_ptr =
      emp::NewPtr<reg_t>(name, desc, filename, line, std::move(get_fun), std::move(cget_fun));

    if (trait_map.contains(name)) {
      emp::notify::Error("Phenotype trait '", name, "' defined multiple times: ",
                          trait_map[name]->GetLocation(), " and ", reg_ptr->GetLocation());
    }
    trait_map[name] = reg_ptr;
  }

  void Clear() {
    for (auto [name, ptr] : trait_map) ptr.Delete();
    trait_map.clear();
  }

  void SerializeOrg(emp::SerialPod & pod, organism_t & org) {
    for (auto [_, trait_ptr] : trait_map) {
      trait_ptr->SerializeOrg(pod, org);
    }
  }
};
