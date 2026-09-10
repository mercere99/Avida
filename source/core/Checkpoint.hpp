#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 *
 *  Versioned, self-validating checkpoint envelope.  The envelope is deliberately independent of
 *  any concrete Avida module pack; callers supply the compatibility identifiers they require.
 */

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <string_view>

namespace avida_checkpoint {

inline constexpr std::string_view MAGIC{"AVIDA_CHECKPOINT"};
inline constexpr size_t SCHEMA_VERSION = 2;
inline constexpr std::string_view PAYLOAD_ENCODING{"avida-serialpod-text-v2"};

struct Compatibility {
  std::string avida_version;
  std::string module_pack;
  std::string cpu_profile;
  std::string instruction_set;
  std::string population_structure;
  size_t configuration_schema = 0;
};

struct Contents {
  Compatibility compatibility;
  std::string configuration;
  std::string payload;
};

inline uint64_t Hash(std::string_view bytes) {
  uint64_t hash = 14695981039346656037ULL;
  for (const unsigned char byte : bytes) {
    hash ^= byte;
    hash *= 1099511628211ULL;
  }
  return hash;
}

inline std::string HashString(std::string_view bytes) {
  std::ostringstream out;
  out << std::hex << std::setfill('0') << std::setw(16) << Hash(bytes);
  return out.str();
}

inline std::string Build(
  const Compatibility & compatibility,
  std::string_view configuration,
  std::string_view payload
) {
  std::ostringstream out;
  out << MAGIC << '\n'
      << "checkpoint_schema=" << SCHEMA_VERSION << '\n'
      << "avida_version=" << compatibility.avida_version << '\n'
      << "module_pack=" << compatibility.module_pack << '\n'
      << "cpu_profile=" << compatibility.cpu_profile << '\n'
      << "instruction_set=" << compatibility.instruction_set << '\n'
      << "population_structure=" << compatibility.population_structure << '\n'
      << "configuration_schema=" << compatibility.configuration_schema << '\n'
      << "payload_encoding=" << PAYLOAD_ENCODING << '\n'
      << "configuration_length=" << configuration.size() << '\n'
      << "payload_length=" << payload.size() << '\n'
      << "configuration_hash=" << HashString(configuration) << '\n'
      << "payload_hash=" << HashString(payload) << '\n'
      << "END_HEADER\n";
  out.write(configuration.data(), static_cast<std::streamsize>(configuration.size()));
  out.write(payload.data(), static_cast<std::streamsize>(payload.size()));
  return out.str();
}

inline std::expected<size_t, std::string> ParseSize(
  const std::map<std::string, std::string> & fields,
  const std::string & name
) {
  const auto found = fields.find(name);
  if (found == fields.end()) return std::unexpected("Checkpoint is missing '" + name + "'.");
  size_t value = 0;
  const char * begin = found->second.data();
  const char * end = begin + found->second.size();
  const auto [parsed_end, error] = std::from_chars(begin, end, value);
  if (error != std::errc{} || parsed_end != end) {
    return std::unexpected("Checkpoint field '" + name + "' is not a valid size.");
  }
  return value;
}

inline std::expected<Contents, std::string> Parse(std::string_view input) {
  const size_t first_newline = input.find('\n');
  if (first_newline == std::string_view::npos || input.substr(0, first_newline) != MAGIC) {
    return std::unexpected("Not an Avida checkpoint file (magic header is missing)." );
  }

  std::map<std::string, std::string> fields;
  size_t cursor = first_newline + 1;
  while (true) {
    const size_t newline = input.find('\n', cursor);
    if (newline == std::string_view::npos) {
      return std::unexpected("Checkpoint header is truncated.");
    }
    const std::string_view line = input.substr(cursor, newline - cursor);
    cursor = newline + 1;
    if (line == "END_HEADER") break;
    const size_t equals = line.find('=');
    if (equals == std::string_view::npos || equals == 0) {
      return std::unexpected("Checkpoint header contains a malformed field.");
    }
    std::string name{line.substr(0, equals)};
    std::string value{line.substr(equals + 1)};
    if (!fields.emplace(std::move(name), std::move(value)).second) {
      return std::unexpected("Checkpoint header contains a duplicate field.");
    }
  }

  static constexpr std::string_view required[]{
    "checkpoint_schema", "avida_version", "module_pack", "cpu_profile",
    "instruction_set", "population_structure", "configuration_schema",
    "payload_encoding", "configuration_length", "payload_length",
    "configuration_hash", "payload_hash"
  };
  for (const std::string_view name : required) {
    if (!fields.contains(std::string{name})) {
      return std::unexpected("Checkpoint is missing '" + std::string{name} + "'.");
    }
  }
  if (fields.size() != std::size(required)) {
    return std::unexpected("Checkpoint header contains unsupported fields.");
  }

  auto schema = ParseSize(fields, "checkpoint_schema");
  if (!schema) return std::unexpected(schema.error());
  if (*schema != SCHEMA_VERSION) {
    return std::unexpected("Unsupported checkpoint schema version " + std::to_string(*schema) + ".");
  }
  if (fields.at("payload_encoding") != PAYLOAD_ENCODING) {
    return std::unexpected("Unsupported checkpoint payload encoding '" +
      fields.at("payload_encoding") + "'.");
  }

  auto configuration_schema = ParseSize(fields, "configuration_schema");
  if (!configuration_schema) return std::unexpected(configuration_schema.error());
  auto configuration_length = ParseSize(fields, "configuration_length");
  if (!configuration_length) return std::unexpected(configuration_length.error());
  auto payload_length = ParseSize(fields, "payload_length");
  if (!payload_length) return std::unexpected(payload_length.error());
  if (*configuration_length > input.size() - cursor) {
    return std::unexpected("Checkpoint configuration is truncated.");
  }
  const size_t remaining_after_configuration = input.size() - cursor - *configuration_length;
  if (*payload_length > remaining_after_configuration) {
    return std::unexpected("Checkpoint payload is truncated.");
  }
  const size_t expected_end = cursor + *configuration_length + *payload_length;
  if (expected_end != input.size()) {
    // Empirical's FileInput appends one newline to text files.  Accept only that exact artifact.
    if (expected_end + 1 != input.size() || input.back() != '\n') {
      return std::unexpected("Checkpoint contains trailing data.");
    }
  }

  std::string configuration{input.substr(cursor, *configuration_length)};
  cursor += *configuration_length;
  std::string payload{input.substr(cursor, *payload_length)};
  if (HashString(configuration) != fields.at("configuration_hash")) {
    return std::unexpected("Checkpoint configuration failed its integrity check.");
  }
  if (HashString(payload) != fields.at("payload_hash")) {
    return std::unexpected("Checkpoint payload failed its integrity check.");
  }

  return Contents{
    .compatibility = {
      .avida_version = fields.at("avida_version"),
      .module_pack = fields.at("module_pack"),
      .cpu_profile = fields.at("cpu_profile"),
      .instruction_set = fields.at("instruction_set"),
      .population_structure = fields.at("population_structure"),
      .configuration_schema = *configuration_schema
    },
    .configuration = std::move(configuration),
    .payload = std::move(payload)
  };
}

inline std::expected<void, std::string> ValidateCompatibility(
  const Compatibility & actual,
  const Compatibility & expected
) {
  const auto check = [](const std::string & name,
                        const std::string & found,
                        const std::string & required) -> std::expected<void, std::string> {
    if (found == required) return {};
    return std::unexpected("Incompatible " + name + ": checkpoint requires '" + found +
      "', but this build provides '" + required + "'.");
  };
  if (auto result = check("Avida version", actual.avida_version, expected.avida_version); !result) {
    return result;
  }
  if (auto result = check("module pack", actual.module_pack, expected.module_pack); !result) {
    return result;
  }
  if (auto result = check("CPU profile", actual.cpu_profile, expected.cpu_profile); !result) {
    return result;
  }
  if (auto result = check(
        "instruction set", actual.instruction_set, expected.instruction_set
      ); !result) {
    return result;
  }
  if (auto result = check(
        "population structure", actual.population_structure, expected.population_structure
      ); !result) {
    return result;
  }
  if (actual.configuration_schema != expected.configuration_schema) {
    return std::unexpected("Incompatible configuration schema: checkpoint requires " +
      std::to_string(actual.configuration_schema) + ", but this build provides " +
      std::to_string(expected.configuration_schema) + ".");
  }
  return {};
}

} // namespace avida_checkpoint
