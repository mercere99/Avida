#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 * 
 *  This is the handler for tracking per-organism traits and behaviors.
 * 
 *  DEVELOPER NOTES:
 *  - Track a TypeID for each trait to allow for dynamic checking?
 *  - Track a const accessor for each trait?
 *  - Allow trait manager to create a lambda that takes an equation and calculates it from a phenotype.
 */

#include "emp/base/Ptr.hpp"
#include "emp/datastructs/RobinHoodMap.hpp"
#include "emp/meta/TypeID.hpp"
#include "emp/tools/String.hpp"

#define AVIDA_REGISTER_TRAIT(NAME, DESC)                                   \
  avida.template RegisterTrait<decltype(Phenotype::NAME)>(                 \
     #NAME, DESC, __FILE__, __LINE__,                                      \
     [](AVIDA_T::phenotype_t & p) -> auto & { return p.NAME; },            \
     [](const AVIDA_T::phenotype_t & p) -> const auto & { return p.NAME; } \
  );

#define AVIDA_REQUIRE_TRAIT(TYPE, NAME)                              \
static_assert(                                                       \
    requires(const typename AVIDA_T::phenotype_t & p) {              \
        { p.NAME } -> std::convertible_to<TYPE>;                     \
    },                                                               \
    "DriverBasic requires a '" #NAME "' trait of type '" #TYPE ". "  \
    "Add a compatible module to your module list.");

template <typename AVIDA_T>
class TraitBase {
protected:
  emp::String name;      // Unique name for this trait in the phenotype.
  emp::String desc;      // Full description of this trait.
  emp::String filename;  // Name of file where this trait was defined. 
  size_t line;           // Line number where this trait was defined.

  using phenotype_t = typename AVIDA_T::phenotype_t;

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
  [[nodiscard]] virtual double AsDouble(const phenotype_t &) const = 0;
  [[nodiscard]] virtual emp::String AsString(const phenotype_t &) const = 0;
};

template <typename TRAIT_T, typename AVIDA_T>
class Trait : public TraitBase<AVIDA_T> {
private:
  using base_t = TraitBase<AVIDA_T>;
  using phenotype_t = typename AVIDA_T::phenotype_t;
  using get_fun_t = std::function<TRAIT_T &(phenotype_t &)>;
  using cget_fun_t = std::function<const TRAIT_T &(const phenotype_t &)>;

  get_fun_t get_fun;
  cget_fun_t cget_fun;

public:
  using trait_t = TRAIT_T;

  Trait(const emp::String & name, const emp::String & desc,
        const emp::String & filename, size_t line,
        get_fun_t get_fun, cget_fun_t cget_fun)
    : base_t(name,desc,filename,line)
    , get_fun(std::move(get_fun)), cget_fun(std::move(cget_fun)) {}

  [[nodiscard]] TRAIT_T & Get(phenotype_t & p) { return get_fun(p); }
  [[nodiscard]] const TRAIT_T & Get(const phenotype_t & p) { return cget_fun(p); }
  [[nodiscard]] const get_fun_t & GetAccessFun() const { return get_fun; }
  [[nodiscard]] const cget_fun_t & GetConstAccessFun() const { return cget_fun; }

  [[nodiscard]] emp::TypeID GetTypeID() const override { return emp::GetTypeID<TRAIT_T>(); }
  [[nodiscard]] double AsDouble(const phenotype_t & p) const override {
    if constexpr (std::convertible_to<TRAIT_T, double>) {
      return static_cast<double>(cget_fun(p));
    } else {
      emp::notify::Error("Cannot convert trait '", base_t::name, "' to type double (is type ",
                         GetTypeID(), ")");
      return 0.0;
    }
  }
  [[nodiscard]] emp::String AsString(const phenotype_t & p) const override {
    if constexpr (std::formattable<const TRAIT_T, char>) {
      return emp::MakeString(cget_fun(p));
    } else {
      return emp::MakeString("[", emp::GetTypeID<TRAIT_T>().GetName(), "]");
    }
  }
};

template <typename AVIDA_T>
class TraitManager {
private:
  using trait_base_t = TraitBase<AVIDA_T>;
  emp::RobinHoodMap<emp::String, emp::Ptr<trait_base_t>> trait_map;

public:
  ~TraitManager() { Clear(); }

  const trait_base_t & Get(const emp::String & name) const {
    auto trait_ptr = trait_map.FindValue(name, nullptr);
    emp_assert(trait_ptr, "Requesting an invalid trait name", name);
    return *trait_ptr;
  }

  template <typename TRAIT_T>
  void Register(const emp::String & name, const emp::String & desc,
                const emp::String & filename, size_t line,
                auto get_fun, auto cget_fun)
  {
    using reg_t = Trait<TRAIT_T, AVIDA_T>;
    emp::Ptr<reg_t> reg_ptr =
      emp::NewPtr<reg_t>(name, desc, filename, line, std::move(get_fun), std::move(cget_fun));

    // Make sure that we are not redefining a trait.
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
};
