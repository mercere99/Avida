#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 *
 *  Runtime value types used by Avida's query infrastructure.
 *  
 *  Classed include:
 *   OrgRef - reference to a specific organism in the current population.
 *   OrgSet - reference to a collection of organisms in the current population.
 *   QueryValue - a data value (string, numeric, etc) returned from a query.
 * 
 *  OrgRef and OrgSet contain validation methods to ensure info is still accurate.
 */

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

#include "emp/bits/BitVector.hpp"
#include "emp/tools/String.hpp"

/// A stable, non-owning reference to an organism in a particular Biota.
///
/// A biota slot alone is insufficient because slots are reused.  Keeping the organism's
/// global ID lets IsValid() detect removal and replacement without making a failed lookup fatal.
template <typename BIOTA_T>
class OrgRef {
public:
  using biota_t = BIOTA_T;
  using organism_t = typename biota_t::organism_t;

  static constexpr size_t UNKNOWN_ID = static_cast<size_t>(-1);

private:
  const biota_t * biota = nullptr;
  size_t biota_id = UNKNOWN_ID;
  size_t global_id = UNKNOWN_ID;

public:
  OrgRef() = default;

  /// Capture the organism currently at biota_id, or an invalid reference if none exists.
  OrgRef(const biota_t & in_biota, size_t in_biota_id)
    : biota(&in_biota), biota_id(in_biota_id)
  {
    if (biota_id < biota->GetSize() && biota->IsActive(biota_id)) {
      global_id = (*biota)[biota_id].GetGlobalID();
    }
  }

  [[nodiscard]] const biota_t * GetBiota() const { return biota; }
  [[nodiscard]] size_t GetBiotaID() const { return biota_id; }
  [[nodiscard]] size_t GetGlobalID() const { return global_id; }

  [[nodiscard]] bool IsValid() const {
    return biota
      && global_id != UNKNOWN_ID
      && biota_id < biota->GetSize()
      && biota->IsActive(biota_id)
      && (*biota)[biota_id].GetGlobalID() == global_id;
  }

  [[nodiscard]] explicit operator bool() const { return IsValid(); }

  /// Return the referenced organism, or nullptr if the reference is invalid or stale.
  [[nodiscard]] const organism_t * TryGet() const {
    return IsValid() ? &(*biota)[biota_id] : nullptr;
  }

  [[nodiscard]] bool operator==(const OrgRef &) const = default;
};

/// A compact snapshot of a set of organisms in a particular Biota.
///
/// OrgSet membership is represented by biota slots.  Consequently, sets are valid only for the
/// Biota epoch in which they were created; population changes make old sets stale instead of
/// silently allowing a recycled slot to represent a different organism.
template <typename BIOTA_T>
class OrgSet {
public:
  using biota_t = BIOTA_T;

private:
  const biota_t * biota = nullptr;
  uint64_t epoch = 0;
  emp::BitVector bits{};

  [[nodiscard]] bool IsCompatible(const OrgSet & other) const {
    return IsValid()
      && other.IsValid()
      && biota == other.biota
      && epoch == other.epoch;
  }

  void Invalidate() {
    biota = nullptr;
    epoch = 0;
    bits.Resize(0);
  }

public:
  /// Construct an invalid set.
  OrgSet() = default;

  /// Construct an empty set associated with the current Biota epoch.
  explicit OrgSet(const biota_t & in_biota)
    : biota(&in_biota)
    , epoch(in_biota.GetEpoch())
    , bits(in_biota.GetActiveBits().GetSize())
  { }

  /// Construct a set from bits, discarding any slots that are not currently active.
  OrgSet(const biota_t & in_biota, emp::BitVector in_bits)
    : biota(&in_biota)
    , epoch(in_biota.GetEpoch())
    , bits(std::move(in_bits))
  {
    bits.Resize(biota->GetActiveBits().GetSize());
    bits &= biota->GetActiveBits();
  }

  /// Capture all currently active organisms.
  [[nodiscard]] static OrgSet All(const biota_t & in_biota) {
    return OrgSet(in_biota, in_biota.GetActiveBits());
  }

  [[nodiscard]] const biota_t * GetBiota() const { return biota; }
  [[nodiscard]] uint64_t GetEpoch() const { return epoch; }
  [[nodiscard]] const emp::BitVector & GetBits() const { return bits; }

  [[nodiscard]] bool IsValid() const {
    return biota && epoch == biota->GetEpoch();
  }

  [[nodiscard]] explicit operator bool() const { return IsValid(); }
  [[nodiscard]] size_t GetSize() const { return bits.CountOnes(); }
  [[nodiscard]] size_t size() const { return GetSize(); }
  [[nodiscard]] bool IsEmpty() const { return GetSize() == 0; }
  [[nodiscard]] bool empty() const { return IsEmpty(); }

  [[nodiscard]] bool Contains(size_t biota_id) const {
    return IsValid() && biota_id < bits.GetSize() && bits.Get(biota_id);
  }

  /// Add an active organism to this set.  Return whether the organism is now present.
  bool Insert(size_t biota_id) {
    if (!IsValid()
        || biota_id >= biota->GetSize()
        || !biota->IsActive(biota_id)) return false;
    bits.Set(biota_id);
    return true;
  }

  /// Remove a slot from this set.  Return whether it was previously present.
  bool Erase(size_t biota_id) {
    if (!IsValid() || biota_id >= bits.GetSize() || !bits.Get(biota_id)) return false;
    bits.Clear(biota_id);
    return true;
  }

  OrgSet & operator|=(const OrgSet & other) {
    if (!IsCompatible(other)) { Invalidate(); return *this; }
    bits |= other.bits;
    return *this;
  }

  OrgSet & operator&=(const OrgSet & other) {
    if (!IsCompatible(other)) { Invalidate(); return *this; }
    bits &= other.bits;
    return *this;
  }

  OrgSet & operator^=(const OrgSet & other) {
    if (!IsCompatible(other)) { Invalidate(); return *this; }
    bits ^= other.bits;
    return *this;
  }

  OrgSet & operator-=(const OrgSet & other) {
    if (!IsCompatible(other)) { Invalidate(); return *this; }
    bits &= ~other.bits;
    return *this;
  }

  [[nodiscard]] OrgSet operator|(const OrgSet & other) const {
    OrgSet out = *this;
    return out |= other;
  }

  [[nodiscard]] OrgSet operator&(const OrgSet & other) const {
    OrgSet out = *this;
    return out &= other;
  }

  [[nodiscard]] OrgSet operator^(const OrgSet & other) const {
    OrgSet out = *this;
    return out ^= other;
  }

  [[nodiscard]] OrgSet operator-(const OrgSet & other) const {
    OrgSet out = *this;
    return out -= other;
  }

  /// Complement relative to the active population, never relative to unused capacity.
  [[nodiscard]] OrgSet operator~() const {
    if (!IsValid()) return {};
    return OrgSet(*biota, biota->GetActiveBits() & ~bits);
  }

  [[nodiscard]] bool operator==(const OrgSet & other) const {
    return biota == other.biota && epoch == other.epoch && bits == other.bits;
  }
};

enum class QueryValueType : size_t {
  NULL_VALUE = 0,
  BOOL,
  INT64,
  UINT64,
  DOUBLE,
  STRING,
  ORG_REF,
  ORG_SET
};

/// A type-erased result produced and consumed by compiled Avida queries.
template <typename BIOTA_T>
class QueryValue {
public:
  using org_ref_t = OrgRef<BIOTA_T>;
  using org_set_t = OrgSet<BIOTA_T>;
  using variant_t = std::variant<
    std::monostate,
    bool,
    int64_t,
    uint64_t,
    double,
    emp::String,
    org_ref_t,
    org_set_t
  >;

private:
  variant_t value{};

public:
  QueryValue() = default;
  QueryValue(std::nullptr_t) : value(std::monostate{}) { }
  QueryValue(bool in) : value(in) { }

  template <std::signed_integral T>
    requires (!std::same_as<std::remove_cv_t<T>, bool>)
  QueryValue(T in) : value(static_cast<int64_t>(in)) { }

  template <std::unsigned_integral T>
    requires (!std::same_as<std::remove_cv_t<T>, bool>)
  QueryValue(T in) : value(static_cast<uint64_t>(in)) { }

  template <std::floating_point T>
  QueryValue(T in) : value(static_cast<double>(in)) { }

  QueryValue(emp::String in) : value(std::move(in)) { }
  QueryValue(const std::string & in) : value(emp::String{in}) { }
  QueryValue(const char * in) : value(emp::String{in}) { }
  QueryValue(org_ref_t in) : value(std::move(in)) { }
  QueryValue(org_set_t in) : value(std::move(in)) { }

  [[nodiscard]] QueryValueType GetType() const {
    return static_cast<QueryValueType>(value.index());
  }

  [[nodiscard]] emp::String GetTypeName() const {
    switch (GetType()) {
      case QueryValueType::NULL_VALUE: return "null";
      case QueryValueType::BOOL:       return "bool";
      case QueryValueType::INT64:      return "int64";
      case QueryValueType::UINT64:     return "uint64";
      case QueryValueType::DOUBLE:     return "double";
      case QueryValueType::STRING:     return "string";
      case QueryValueType::ORG_REF:    return "organism";
      case QueryValueType::ORG_SET:    return "organism_set";
    }
    return "unknown";
  }

  [[nodiscard]] bool IsNull() const {
    return std::holds_alternative<std::monostate>(value);
  }

  [[nodiscard]] bool IsNumeric() const {
    return std::holds_alternative<int64_t>(value)
      || std::holds_alternative<uint64_t>(value)
      || std::holds_alternative<double>(value);
  }

  template <typename T>
  [[nodiscard]] bool Is() const { return std::holds_alternative<T>(value); }

  template <typename T>
  [[nodiscard]] auto & Get(this auto & self) { return std::get<T>(self.value); }

  template <typename T>
  [[nodiscard]] auto * GetIf(this auto & self) { return std::get_if<T>(&self.value); }

  [[nodiscard]] auto & GetVariant(this auto & self) { return self.value; }

  template <typename FUN_T>
  decltype(auto) Visit(this auto & self, FUN_T && fun) {
    return std::visit(std::forward<FUN_T>(fun), self.value);
  }

  [[nodiscard]] bool operator==(const QueryValue &) const = default;
};
