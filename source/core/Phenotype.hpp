#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) Michigan State University & Dr. Charles Ofria
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

// Retrieve a Phenotype member from a plug-in module.
template <typename T> using pheno_member = typename T::Phenotype;

// Convert a type pack of phenotypes into a single, merged phenotype.
template <typename T> struct merge_from_TypePack;

template <typename... Ts>
struct merge_from_TypePack<emp::TypePack<Ts...>> {
  struct merged_t : Ts... { };
};

#define AVIDA_TRAIT(TYPE, NAME, DESC)                                                       \
  static_assert(requires { typename AVIDA_T; }, "AVIDA_T must be defined to create trait"); \
  TYPE NAME{};                                                                              \
  static inline TraitRegister<TYPE, AVIDA_T> NAME ## _register_                             \
    {#NAME, DESC, __FILE__, __LINE__,                                                       \
     [](AVIDA_T::typename phenotype_t & phenotype) -> TYPE & { return phenotype.NAME; }};


template <typename AVIDA_T>
class TraitRegisterBase {
protected:
  emp::String name;      // Unique name for this trait in the phenotype.
  emp::String desc;      // Full description of this trait.
  emp::String filename;  // Name of file where this trait was defined. 
  size_t line;           // Line number where this trait was defined.

  using phenotype_t = typename AVIDA_T::phenotype_t;

public:
  TraitRegisterBase(const emp::String & name, const emp::String & desc)
    : name(name), desc(desc) { }

  [[nodiscard]] emp::String GetName() const { return name; }
  [[nodiscard]] emp::String GetDesc() const { return desc; }
  [[nodiscard]] emp::String GetFilename() const { return filename; }
  [[nodiscard]] size_t GetLine() const { return line; }
  [[nodiscard]] emp::String GetLocation() const { return emp::MakeString(filename, ':', line); }

  [[nodiscard]] virtual emp::TypeID GetTypeID() const = 0;
  [[nodiscard]] virtual double AsDouble(phenotype_t &) const = 0;
  [[nodiscard]] virtual emp::String AsString(phenotype_t &) const = 0;
};

template <typename TRAIT_T, typename AVIDA_T>
class TraitRegister : public TraitRegisterBase<AVIDA_T> {
private:
  using base_t = TraitRegisterBase<AVIDA_T>;
  using phenotype_t = typename AVIDA_T::phenotype_t;
  using access_fun_t = std::function<TRAIT_T &(phenotype_t &)>;

  access_fun_t access_fun;

public:
  TraitRegister(const emp::String & name, const emp::String & desc,
                const emp::String filename, size_t line, access_fun_t access_fun)
    : base_t(name,desc,filename,line), access_fun(std::move(access_fun))
  {
    AVIDA_T::GetTraitManager().Register(*this);
  }

  [[nodiscard]] TRAIT_T & Get(phenotype_t & p) { return access_fun(p); }
  [[nodiscard]] const access_fun_t & GetAccessFun() const { return access_fun; }

  [[nodiscard]] emp::TypeID GetTypeID() const override { return emp::GetTypeID<TRAIT_T>(); }
  [[nodiscard]] double AsDouble(phenotype_t & p) const override {
    if constexpr (std::convertible_to<TRAIT_T, double>) {
      return static_cast<double>(access_fun(p));
    } else {
      emp::notify::Error("Cannot convert trait '", base_t::name, "' to type double (is type ",
                         GetTypeID(), ")");
    }
  }
  [[nodiscard]] emp::String AsString(phenotype_t & p) const override {
    emp::MakeString(access_fun(p));
  }
};

template <typename AVIDA_T>
class TraitManager {
private:
  using register_base_t = TraitRegisterBase<AVIDA_T>;
  emp::RobinHoodMap<emp::String, emp::Ptr<register_base_t>> trait_map;

public:
  template <typename TRAIT_T>
  void Register(TraitRegister<TRAIT_T, AVIDA_T> & trait_reg) {
    const emp::String & name = trait_reg.GetName();

    // Make sure that we are not redefining a trait.
    if (trait_map.contains(name)) {
      emp::notify::Error("Phenotype trait '", name, "' defined multiple times: ",
                          trait_map[name]->GetLocation(), " and ", trait_reg.GetLocation());
    }
    trait_map[name] = &trait_reg;
  }
};
