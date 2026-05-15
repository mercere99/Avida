#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 */

#include <concepts>
#include <cstddef>   // for size_t
#include <cstdint>   // for uint8_t
#include <iterator>  // for std::input_iterator

#include "emp/base/assert.hpp"
#include "emp/base/vector.hpp"
#include "emp/math/Random.hpp"
#include "emp/math/Range.hpp"

template <typename VALUE_T> class Genome;

// Controls properties of genomes, including size and mutation handling.
template <typename VALUE_T>
class GenomeManager {
public:
  using value_t = VALUE_T;

private:
  emp::Range<size_t> size_range{0, emp::MAX_SIZE_T};
  emp::Range<value_t> value_range{value_t{}, std::numeric_limits<value_t>::max()};

public:
  [[nodiscard]] const emp::Range<size_t> & SizeRange() const { return size_range; }
  [[nodiscard]] size_t MinSize() const { return size_range.GetLower(); }
  [[nodiscard]] size_t MaxSize() const { return size_range.GetUpper(); }

  void SetSizeRange(size_t in_min, size_t in_max) { size_range.Set(in_min, in_max); }
  void SetMinSize(size_t in_min) { size_range.SetLower(in_min); }
  void SetMaxSize(size_t in_max) { size_range.SetUpper(in_max); }

  [[nodiscard]] const emp::Range<value_t> & ValueRange() const { return size_range; }
  [[nodiscard]] value_t MinValue() const { return value_range.GetLower(); }
  [[nodiscard]] value_t MaxValue() const { return value_range.GetUpper(); }
  [[nodiscard]] bool HasValue(value_t val) const { return value_range.Has(val); }

  void SetValueRange(value_t in_min, value_t in_max) { value_range.Set(in_min, in_max); }
  void SetMinValue(value_t in_min) { value_range.SetLower(in_min); }
  void SetMaxValue(value_t in_max) { value_range.SetUpper(in_max); }

  [[nodiscard]] value_t RandomValue(emp::Random & random) const { return random.GetValue<value_t>(value_range); }

  Genome<value_t> MakeGenome(size_t length, emp::Random & random) const {
    emp_assert(size_range.Has(length));
    Genome<value_t> genome(length);
    for (auto & locus : genome) locus = RandomValue(random);
    return genome;
  }
};

/// @brief A genome with a fixed number of possible states.
template <typename VALUE_T>
class Genome {
public:
  using value_t = VALUE_T;
  using manager_t = GenomeManager<VALUE_T>;

private:
  using this_t = Genome<VALUE_T>;

  emp::Ptr<const manager_t> manager_ptr = nullptr;
  emp::vector<value_t> genome;

  // Create a liberal limit on genome sizes to spot problems if they get much too high.
  static constexpr size_t MAX_SIZE = size_t{1} << 30; 

public:
  Genome() = default;
  Genome(const Genome &) = default;
  Genome(Genome &&) = default;
  Genome(const manager_t & man, size_t length = 0, value_t default_value = value_t{})
    : manager_ptr(&man), genome(length, default_value) { }
  Genome(const manager_t & man, const emp::vector<value_t> & g)
    : manager_ptr(&man), genome(g) { }
  Genome(const manager_t & man, emp::vector<value_t> && g)
    : manager_ptr(&man), genome(std::move(g)) { }
  template <typename It> requires std::input_iterator<It> &&
                                  std::convertible_to<std::iter_value_t<It>, value_t>
  Genome(const manager_t & man, It first, It last) : manager_ptr(&man), genome(first, last) { }
  ~Genome() = default;

  auto begin() noexcept { return genome.begin(); }
  auto begin() const noexcept { return genome.begin(); }
  auto cbegin() const noexcept { return genome.cbegin(); }
  auto end() noexcept { return genome.end(); }
  auto end() const noexcept { return genome.end(); }
  auto cend() const noexcept { return genome.cend(); }

  this_t & operator=(const this_t &) = default;
  this_t & operator=(this_t &&) = default;

  [[nodiscard]] bool operator<=>(const this_t &) const = default;

  [[nodiscard]] value_t operator[](size_t pos) const { return genome[pos]; }
  [[nodiscard]] value_t & operator[](size_t pos) { return genome[pos]; }
  [[nodiscard]] auto & Values(this auto & self) { return self.genome; }

  [[nodiscard]] bool HasManager() const { return !manager_ptr.IsNull(); }
  [[nodiscard]] auto & Manager(this auto self) {
    emp_assert(self.HasManager());
    return *self.manager_ptr;
  }

  [[nodiscard]] bool IsEmpty() const { return genome.empty(); }

  [[nodiscard]] size_t FindMaxID() const {
    return std::ranges::max_element(genome) - genome.begin();
  }
  void Clear() { genome.clear(); }

  void RandomizeSite(emp::Random & random, size_t pos) {
    emp_assert(HasManager());
    genome[pos] = manager_ptr->RandomValue(random);
  }

  bool Push(value_t val) {
    emp_assert(HasManager());
    emp_assert(manager_ptr->HasValue(val));
    if (Manager().MaxSize() == genome.size()) return false;
    genome.push_back(val);
    return true;
  }

  [[nodiscard]] size_t size() const { return genome.size(); }
  this_t & Resize(size_t new_size) {
    emp_assert(new_size < MAX_SIZE);
    genome.resize(new_size);
    return *this;
  }

  void Reserve(size_t max_size) {
    emp_assert(max_size < MAX_SIZE);
    genome.reserve(max_size);
  }

  void Insert(size_t pos, value_t value, size_t count=1) {
    emp_assert(pos <= size());
    emp_assert(count < MAX_SIZE - size());
    genome.insert(begin()+pos, count, value);
  }

  void Erase(size_t pos, size_t count=1) {
    emp_assert(pos <= size());
    emp_assert(count <= size() - pos);
    genome.erase(begin() + pos, begin() + (pos + count));
  }

  // Make a copy of a section of this genome and return it.
  [[nodiscard]] this_t Copy(size_t start_pos, size_t count) const {
    emp_assert(start_pos <= size());
    emp_assert(count <= size() - start_pos);
    const size_t end_pos = start_pos + count;
    return this_t{Manager(), begin()+start_pos, begin()+end_pos};
  }

  // Remove [start_pos, end_pos) from this Genome and return it.
  [[nodiscard]] this_t Extract(size_t start_pos, size_t count) {
    emp_assert(start_pos <= size());
    emp_assert(count <= size() - start_pos);
    this_t out(Copy(start_pos, count));
    Erase(start_pos, count);
    return out;
  }

  std::string AsString() const { 
    std::stringstream ss;
    for (const auto & val : genome) {
      ss << '[' << val << ']';
    }
    return ss.str();
  }

};
