#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 *
 *  Compile and evaluate typed Avida query expressions.  This initial scalar layer supports
 *  registered setting/value names, literals, arithmetic and comparison operators, short-circuit
 *  boolean and ternary operators, scalar math functions, org(id), and the .valid property.
 */

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <type_traits>
#include <utility>

#include "emp/base/vector.hpp"
#include "emp/compiler/Lexer.hpp"
#include "emp/tools/String.hpp"

#include "QueryValue.hpp"

template <typename AVIDA_T>
struct QueryContext {
  const AVIDA_T & avida;
  const typename AVIDA_T::organism_t * organism = nullptr;
  const typename AVIDA_T::org_set_t * collection = nullptr;
};

template <typename AVIDA_T>
class CompiledQuery {
public:
  using value_t = typename AVIDA_T::query_value_t;
  using context_t = QueryContext<AVIDA_T>;
  using eval_fun_t = std::function<value_t(const context_t &)>;

private:
  const AVIDA_T & avida;
  emp::String source;
  QueryValueType result_type = QueryValueType::NULL_VALUE;
  eval_fun_t eval_fun;

public:
  CompiledQuery(const AVIDA_T & in_avida, emp::String in_source,
                QueryValueType in_type, eval_fun_t in_fun)
    : avida(in_avida)
    , source(std::move(in_source))
    , result_type(in_type)
    , eval_fun(std::move(in_fun))
  { }

  [[nodiscard]] const emp::String & GetSource() const { return source; }
  [[nodiscard]] QueryValueType GetType() const { return result_type; }
  [[nodiscard]] bool IsValid() const { return static_cast<bool>(eval_fun); }

  [[nodiscard]] value_t Evaluate() const {
    if (!eval_fun) emp::notify::Error("Cannot evaluate an invalid compiled query.");
    return eval_fun(context_t{avida});
  }

  [[nodiscard]] value_t Evaluate(const context_t & context) const {
    if (!eval_fun) emp::notify::Error("Cannot evaluate an invalid compiled query.");
    if (&context.avida != &avida) {
      emp::notify::Error("Cannot evaluate a compiled query with a different Avida instance.");
    }
    return eval_fun(context);
  }

  [[nodiscard]] value_t operator()() const { return Evaluate(); }
};

template <typename AVIDA_T>
class QueryManager {
public:
  using value_t = typename AVIDA_T::query_value_t;
  using org_ref_t = typename AVIDA_T::org_ref_t;
  using org_set_t = typename AVIDA_T::org_set_t;
  using context_t = QueryContext<AVIDA_T>;
  using compiled_query_t = CompiledQuery<AVIDA_T>;
  using value_getter_t = std::function<value_t(const context_t &)>;

private:
  struct Expression {
    QueryValueType type;
    value_getter_t eval;
  };

  struct ValueInfo {
    QueryValueType type;
    value_getter_t getter;
  };

  using function_t = std::function<value_t(const emp::vector<value_t> &)>;

  struct FunctionInfo {
    size_t arity;
    QueryValueType return_type;
    bool require_numeric;
    bool propagate_null;
    function_t fun;
  };

  AVIDA_T & avida;
  std::map<emp::String, ValueInfo> value_map;
  std::map<emp::String, FunctionInfo> function_map;

  emp::Lexer lexer;
  const int ident_id;
  const int int_id;
  const int double_id;
  const int string_id;
  const int logical_or_id;
  const int logical_and_id;
  const int equal_id;
  const int not_equal_id;
  const int less_equal_id;
  const int greater_equal_id;
  const int power_id;

  template <typename T>
  [[nodiscard]] static consteval QueryValueType GetQueryType() {
    using base_t = std::remove_cvref_t<T>;
    if constexpr (std::same_as<base_t, bool>) return QueryValueType::BOOL;
    else if constexpr (std::signed_integral<base_t>) return QueryValueType::INT64;
    else if constexpr (std::unsigned_integral<base_t>) return QueryValueType::UINT64;
    else if constexpr (std::floating_point<base_t>) return QueryValueType::DOUBLE;
    else if constexpr (std::same_as<base_t, emp::String>
                       || std::same_as<base_t, std::string>) return QueryValueType::STRING;
    else if constexpr (std::same_as<base_t, org_ref_t>) return QueryValueType::ORG_REF;
    else if constexpr (std::same_as<base_t, org_set_t>) return QueryValueType::ORG_SET;
    else static_assert(false, "Unsupported query value type.");
  }

  [[nodiscard]] static bool IsNumericType(QueryValueType type) {
    return type == QueryValueType::INT64
      || type == QueryValueType::UINT64
      || type == QueryValueType::DOUBLE;
  }

  [[nodiscard]] static bool IsConditionType(QueryValueType type) {
    return type == QueryValueType::BOOL || IsNumericType(type) || type == QueryValueType::NULL_VALUE;
  }

  [[nodiscard]] static bool IsNumericOrNull(QueryValueType type) {
    return IsNumericType(type) || type == QueryValueType::NULL_VALUE;
  }

  [[nodiscard]] static double AsDouble(const value_t & value) {
    if (const auto * ptr = value.template GetIf<int64_t>()) return static_cast<double>(*ptr);
    if (const auto * ptr = value.template GetIf<uint64_t>()) return static_cast<double>(*ptr);
    if (const auto * ptr = value.template GetIf<double>()) return *ptr;
    emp::notify::Error("Query expected a numeric value, but found ", value.GetTypeName(), ".");
  }

  [[nodiscard]] static bool AsBool(const value_t & value) {
    if (value.IsNull()) return false;
    if (const auto * ptr = value.template GetIf<bool>()) return *ptr;
    if (value.IsNumeric()) return AsDouble(value) != 0.0;
    emp::notify::Error("Query expected a condition, but found ", value.GetTypeName(), ".");
  }

  [[nodiscard]] static value_t CoerceTo(const value_t & value, QueryValueType type) {
    if (value.IsNull()) return {};
    if (type == QueryValueType::DOUBLE && value.IsNumeric()) return AsDouble(value);
    return value;
  }

  void AddNumericFunction(const emp::String & name, size_t arity, function_t fun) {
    function_map.emplace(
      name,
      FunctionInfo{arity, QueryValueType::DOUBLE, true, true, std::move(fun)}
    );
  }

  void SetupFunctions() {
    AddNumericFunction("abs", 1, [](const auto & args){ return std::abs(AsDouble(args[0])); });
    AddNumericFunction("exp", 1, [](const auto & args){ return std::exp(AsDouble(args[0])); });
    AddNumericFunction("log", 1, [](const auto & args){ return std::log(AsDouble(args[0])); });
    AddNumericFunction("log2", 1, [](const auto & args){ return std::log2(AsDouble(args[0])); });
    AddNumericFunction("log10", 1, [](const auto & args){ return std::log10(AsDouble(args[0])); });
    AddNumericFunction("sqrt", 1, [](const auto & args){ return std::sqrt(AsDouble(args[0])); });
    AddNumericFunction("ceil", 1, [](const auto & args){ return std::ceil(AsDouble(args[0])); });
    AddNumericFunction("floor", 1, [](const auto & args){ return std::floor(AsDouble(args[0])); });
    AddNumericFunction("round", 1, [](const auto & args){ return std::round(AsDouble(args[0])); });
    AddNumericFunction("pow", 2, [](const auto & args){
      return std::pow(AsDouble(args[0]), AsDouble(args[1]));
    });
    AddNumericFunction("min", 2, [](const auto & args){
      return std::min(AsDouble(args[0]), AsDouble(args[1]));
    });
    AddNumericFunction("max", 2, [](const auto & args){
      return std::max(AsDouble(args[0]), AsDouble(args[1]));
    });

    function_map.emplace("org", FunctionInfo{
      1,
      QueryValueType::ORG_REF,
      true,
      false,
      [this](const emp::vector<value_t> & args) -> value_t {
        if (args[0].IsNull()) return org_ref_t{};
        if (const auto * id = args[0].template GetIf<int64_t>()) {
          return *id < 0 ? value_t{org_ref_t{}} : value_t{avida.GetOrgRef(static_cast<size_t>(*id))};
        }
        if (const auto * id = args[0].template GetIf<uint64_t>()) {
          if constexpr (sizeof(size_t) < sizeof(uint64_t)) {
            if (*id > std::numeric_limits<size_t>::max()) return org_ref_t{};
          }
          return avida.GetOrgRef(static_cast<size_t>(*id));
        }
        const double id_value = AsDouble(args[0]);
        if (!std::isfinite(id_value)
            || id_value < 0.0
            || std::trunc(id_value) != id_value
            || id_value >= static_cast<double>(std::numeric_limits<size_t>::max())) {
          return org_ref_t{};
        }
        return avida.GetOrgRef(static_cast<size_t>(id_value));
      }
    });
  }

  class Parser {
  private:
    const QueryManager & manager;
    emp::String source;
    emp::TokenStream tokens;
    emp::TokenStream::Iterator pos;

    [[noreturn]] void Error(const emp::String & message) const {
      const auto & token = pos.Peek();
      if (token.id == 0) {
        emp::notify::Error("Invalid query ", source.AsLiteral(), ": ", message, " at end of input.");
      }
      emp::notify::Error(
        "Invalid query ", source.AsLiteral(), ": ", message,
        " near ", token.lexeme.AsLiteral(), "."
      );
    }

    [[nodiscard]] bool Match(int token_id) {
      if (!pos.Is(token_id)) return false;
      ++pos;
      return true;
    }

    void Require(int token_id, const emp::String & description) {
      if (!Match(token_id)) Error(emp::MakeString("expected ", description));
    }

    [[nodiscard]] static Expression Literal(value_t value) {
      const QueryValueType type = value.GetType();
      return {type, [value=std::move(value)](const context_t &){ return value; }};
    }

    [[nodiscard]] Expression MakeUnary(int op, Expression operand) {
      if (op == '!') {
        if (!IsConditionType(operand.type)) Error("operator '!' requires a condition");
        return {
          QueryValueType::BOOL,
          [operand=std::move(operand)](const context_t & context){
            return !AsBool(operand.eval(context));
          }
        };
      }

      if (!IsNumericOrNull(operand.type)) {
        Error(emp::MakeString("unary '", static_cast<char>(op), "' requires a numeric operand"));
      }
      if (op == '+') return operand;
      return {
        QueryValueType::DOUBLE,
        [operand=std::move(operand)](const context_t & context) -> value_t {
          value_t value = operand.eval(context);
          if (value.IsNull()) return {};
          return -AsDouble(value);
        }
      };
    }

    [[nodiscard]] Expression MakeArithmetic(int op, Expression lhs, Expression rhs) {
      const bool string_concat = op == '+'
        && (lhs.type == QueryValueType::STRING || rhs.type == QueryValueType::STRING)
        && (lhs.type == QueryValueType::STRING || lhs.type == QueryValueType::NULL_VALUE)
        && (rhs.type == QueryValueType::STRING || rhs.type == QueryValueType::NULL_VALUE);
      if (string_concat) {
        return {
          QueryValueType::STRING,
          [lhs=std::move(lhs), rhs=std::move(rhs)](const context_t & context) -> value_t {
            value_t left = lhs.eval(context);
            value_t right = rhs.eval(context);
            if (left.IsNull() || right.IsNull()) return {};
            return left.template Get<emp::String>() + right.template Get<emp::String>();
          }
        };
      }

      if (!IsNumericOrNull(lhs.type) || !IsNumericOrNull(rhs.type)) {
        Error(emp::MakeString("operator '", static_cast<char>(op), "' requires numeric operands"));
      }

      if (lhs.type == QueryValueType::NULL_VALUE && rhs.type == QueryValueType::NULL_VALUE) {
        return Literal(value_t{});
      }

      return {
        QueryValueType::DOUBLE,
        [op, lhs=std::move(lhs), rhs=std::move(rhs)](const context_t & context) -> value_t {
          value_t left = lhs.eval(context);
          value_t right = rhs.eval(context);
          if (left.IsNull() || right.IsNull()) return {};
          const double a = AsDouble(left);
          const double b = AsDouble(right);
          switch (op) {
            case '+': return a + b;
            case '-': return a - b;
            case '*': return a * b;
            case '/': return a / b;
            case '%': return std::fmod(a, b);
          }
          emp::notify::Error("Unknown arithmetic query operator.");
        }
      };
    }

    [[nodiscard]] Expression MakePower(Expression lhs, Expression rhs) {
      if (!IsNumericOrNull(lhs.type) || !IsNumericOrNull(rhs.type)) {
        Error("operator '**' requires numeric operands");
      }
      return {
        QueryValueType::DOUBLE,
        [lhs=std::move(lhs), rhs=std::move(rhs)](const context_t & context) -> value_t {
          value_t left = lhs.eval(context);
          value_t right = rhs.eval(context);
          if (left.IsNull() || right.IsNull()) return {};
          return std::pow(AsDouble(left), AsDouble(right));
        }
      };
    }

    [[nodiscard]] Expression MakeComparison(int op, Expression lhs, Expression rhs) {
      const bool numeric = IsNumericOrNull(lhs.type) && IsNumericOrNull(rhs.type);
      const bool strings = (lhs.type == QueryValueType::STRING || lhs.type == QueryValueType::NULL_VALUE)
        && (rhs.type == QueryValueType::STRING || rhs.type == QueryValueType::NULL_VALUE);
      if (!numeric && !strings) Error("comparison requires two numeric values or two strings");

      return {
        QueryValueType::BOOL,
        [op, numeric, lhs=std::move(lhs), rhs=std::move(rhs)](const context_t & context) -> value_t {
          value_t left = lhs.eval(context);
          value_t right = rhs.eval(context);
          if (left.IsNull() || right.IsNull()) return {};
          if (numeric) {
            const double a = AsDouble(left);
            const double b = AsDouble(right);
            switch (op) {
              case '<': return a < b;
              case '>': return a > b;
              case 1: return a <= b;
              case 2: return a >= b;
            }
          } else {
            const auto & a = left.template Get<emp::String>();
            const auto & b = right.template Get<emp::String>();
            switch (op) {
              case '<': return a < b;
              case '>': return a > b;
              case 1: return a <= b;
              case 2: return a >= b;
            }
          }
          emp::notify::Error("Unknown comparison query operator.");
        }
      };
    }

    [[nodiscard]] Expression MakeEquality(bool is_equal, Expression lhs, Expression rhs) {
      const bool compatible = lhs.type == rhs.type
        || (IsNumericType(lhs.type) && IsNumericType(rhs.type))
        || lhs.type == QueryValueType::NULL_VALUE
        || rhs.type == QueryValueType::NULL_VALUE;
      if (!compatible) Error("equality comparison uses incompatible types");

      return {
        QueryValueType::BOOL,
        [is_equal, lhs=std::move(lhs), rhs=std::move(rhs)](const context_t & context) -> value_t {
          const value_t left = lhs.eval(context);
          const value_t right = rhs.eval(context);
          bool result = false;
          if (left.IsNull() || right.IsNull()) result = left.IsNull() && right.IsNull();
          else if (left.IsNumeric() && right.IsNumeric()) result = AsDouble(left) == AsDouble(right);
          else result = left == right;
          return is_equal ? result : !result;
        }
      };
    }

    [[nodiscard]] Expression MakeLogical(bool is_and, Expression lhs, Expression rhs) {
      if (!IsConditionType(lhs.type) || !IsConditionType(rhs.type)) {
        Error(is_and ? "operator '&&' requires conditions" : "operator '||' requires conditions");
      }
      return {
        QueryValueType::BOOL,
        [is_and, lhs=std::move(lhs), rhs=std::move(rhs)](const context_t & context) -> value_t {
          const bool left = AsBool(lhs.eval(context));
          if (is_and && !left) return false;
          if (!is_and && left) return true;
          return AsBool(rhs.eval(context));
        }
      };
    }

    [[nodiscard]] Expression ParsePrimary() {
      if (Match('(')) {
        Expression out = ParseTernary();
        Require(')', "')'");
        return out;
      }

      if (pos.Is(manager.int_id)) {
        emp::String text = pos.Use().lexeme;
        errno = 0;
        const unsigned long long number = text.PopUnsigned();
        if (errno == ERANGE) Error("integer literal is out of range");
        if (number <= static_cast<unsigned long long>(std::numeric_limits<int64_t>::max())) {
          return Literal(static_cast<int64_t>(number));
        }
        return Literal(static_cast<uint64_t>(number));
      }

      if (pos.Is(manager.double_id)) {
        emp::String text = pos.Use().lexeme;
        errno = 0;
        const double number = text.PopFloat();
        if (errno == ERANGE) Error("floating-point literal is out of range");
        return Literal(number);
      }

      if (pos.Is(manager.string_id)) {
        return Literal(pos.Use().lexeme.ConvertStringFromLiteral("\"'"));
      }

      if (!pos.Is(manager.ident_id)) Error("expected a value");

      emp::vector<emp::String> name_parts{pos.Use().lexeme};
      emp::vector<emp::String> name_paths{name_parts[0]};
      while (pos.Is('.') && pos.Is(manager.ident_id, 1)) {
        pos.Use();
        name_parts.push_back(pos.Use().lexeme);
        name_paths.push_back(name_paths.back() + '.' + name_parts.back());
      }

      size_t path_size = name_paths.size();
      const emp::String & full_name = name_paths.back();
      const bool full_is_function = pos.Is('(') && manager.function_map.contains(full_name);
      if (!manager.value_map.contains(full_name) && !full_is_function) {
        while (path_size > 1 && !manager.value_map.contains(name_paths[path_size - 2])) {
          --path_size;
        }
        if (path_size > 1) {
          --path_size;
          pos.Rewind((name_paths.size() - path_size) * 2);
        } else {
          path_size = name_paths.size();
        }
      }
      const emp::String & name = name_paths[path_size - 1];

      if (name == "true") return Literal(true);
      if (name == "false") return Literal(false);
      if (name == "null") return Literal(value_t{});

      if (Match('(')) {
        emp::vector<Expression> args;
        if (!pos.Is(')')) {
          do { args.push_back(ParseTernary()); } while (Match(','));
        }
        Require(')', "')'");

        auto fun_it = manager.function_map.find(name);
        if (fun_it == manager.function_map.end()) {
          Error(emp::MakeString("unknown scalar function '", name, "'"));
        }
        const FunctionInfo & info = fun_it->second;
        if (args.size() != info.arity) {
          Error(emp::MakeString(
            "function '", name, "' expects ", info.arity,
            " argument", info.arity == 1 ? "" : "s"
          ));
        }
        if (info.require_numeric) {
          for (const Expression & arg : args) {
            if (!IsNumericType(arg.type) && arg.type != QueryValueType::NULL_VALUE) {
              Error(emp::MakeString("function '", name, "' requires numeric arguments"));
            }
          }
        }

        emp::vector<value_getter_t> arg_funs;
        arg_funs.reserve(args.size());
        for (Expression & arg : args) arg_funs.push_back(std::move(arg.eval));
        return {
          info.return_type,
          [arg_funs=std::move(arg_funs),
           propagate_null=info.propagate_null,
           fun=info.fun](const context_t & context) -> value_t {
            emp::vector<value_t> values;
            values.reserve(arg_funs.size());
            for (const auto & arg_fun : arg_funs) values.push_back(arg_fun(context));
            if (propagate_null) {
              for (const value_t & value : values) if (value.IsNull()) return {};
            }
            return fun(values);
          }
        };
      }

      auto value_it = manager.value_map.find(name);
      if (value_it == manager.value_map.end()) {
        Error(emp::MakeString("unknown value '", name, "'"));
      }
      return {value_it->second.type, value_it->second.getter};
    }

    [[nodiscard]] Expression ParsePostfix() {
      Expression out = ParsePrimary();
      while (Match('.')) {
        if (!pos.Is(manager.ident_id)) Error("expected a property name after '.'");
        const emp::String property = pos.Use().lexeme;
        if (property != "valid") {
          Error(emp::MakeString("unknown phase-2 property '", property, "'"));
        }
        if (out.type == QueryValueType::ORG_REF) {
          out = {
            QueryValueType::BOOL,
            [base=std::move(out)](const context_t & context) -> value_t {
              value_t value = base.eval(context);
              if (value.IsNull()) return false;
              return value.template Get<org_ref_t>().IsValid();
            }
          };
        } else if (out.type == QueryValueType::ORG_SET) {
          out = {
            QueryValueType::BOOL,
            [base=std::move(out)](const context_t & context) -> value_t {
              value_t value = base.eval(context);
              if (value.IsNull()) return false;
              return value.template Get<org_set_t>().IsValid();
            }
          };
        } else {
          Error("property '.valid' requires an organism or organism set");
        }
      }
      return out;
    }

    [[nodiscard]] Expression ParseUnary() {
      if (pos.Peek().IsOneOf('!', '+', '-')) {
        const int op = pos.Use().id;
        return MakeUnary(op, ParseUnary());
      }
      return ParsePostfix();
    }

    [[nodiscard]] Expression ParsePower() {
      Expression lhs = ParseUnary();
      if (Match(manager.power_id)) return MakePower(std::move(lhs), ParsePower());
      return lhs;
    }

    [[nodiscard]] Expression ParseMultiplicative() {
      Expression lhs = ParsePower();
      while (pos.Peek().IsOneOf('*', '/', '%')) {
        const int op = pos.Use().id;
        lhs = MakeArithmetic(op, std::move(lhs), ParsePower());
      }
      return lhs;
    }

    [[nodiscard]] Expression ParseAdditive() {
      Expression lhs = ParseMultiplicative();
      while (pos.Peek().IsOneOf('+', '-')) {
        const int op = pos.Use().id;
        lhs = MakeArithmetic(op, std::move(lhs), ParseMultiplicative());
      }
      return lhs;
    }

    [[nodiscard]] Expression ParseComparison() {
      Expression lhs = ParseAdditive();
      while (pos.Peek().IsOneOf('<', '>', manager.less_equal_id, manager.greater_equal_id)) {
        const int token = pos.Use().id;
        const int op = token == manager.less_equal_id ? 1
          : token == manager.greater_equal_id ? 2
          : token;
        lhs = MakeComparison(op, std::move(lhs), ParseAdditive());
      }
      return lhs;
    }

    [[nodiscard]] Expression ParseEquality() {
      Expression lhs = ParseComparison();
      while (pos.Peek().IsOneOf(manager.equal_id, manager.not_equal_id)) {
        const bool is_equal = pos.Use().id == manager.equal_id;
        lhs = MakeEquality(is_equal, std::move(lhs), ParseComparison());
      }
      return lhs;
    }

    [[nodiscard]] Expression ParseLogicalAnd() {
      Expression lhs = ParseEquality();
      while (Match(manager.logical_and_id)) {
        lhs = MakeLogical(true, std::move(lhs), ParseEquality());
      }
      return lhs;
    }

    [[nodiscard]] Expression ParseLogicalOr() {
      Expression lhs = ParseLogicalAnd();
      while (Match(manager.logical_or_id)) {
        lhs = MakeLogical(false, std::move(lhs), ParseLogicalAnd());
      }
      return lhs;
    }

    [[nodiscard]] Expression ParseTernary() {
      Expression condition = ParseLogicalOr();
      if (!Match('?')) return condition;
      if (!IsConditionType(condition.type)) Error("ternary condition is not boolean or numeric");

      Expression if_true = ParseTernary();
      Require(':', "':'");
      Expression if_false = ParseTernary();

      QueryValueType result_type = if_true.type;
      if (if_true.type != if_false.type) {
        if (IsNumericType(if_true.type) && IsNumericType(if_false.type)) {
          result_type = QueryValueType::DOUBLE;
        } else if (if_true.type == QueryValueType::NULL_VALUE) {
          result_type = if_false.type;
        } else if (if_false.type != QueryValueType::NULL_VALUE) {
          Error("ternary branches have incompatible types");
        }
      }

      return {
        result_type,
        [result_type,
         condition=std::move(condition),
         if_true=std::move(if_true),
         if_false=std::move(if_false)](const context_t & context) -> value_t {
          return CoerceTo(
            AsBool(condition.eval(context)) ? if_true.eval(context) : if_false.eval(context),
            result_type
          );
        }
      };
    }

  public:
    Parser(const QueryManager & in_manager, emp::String in_source)
      : manager(in_manager)
      , source(std::move(in_source))
      , tokens(manager.lexer.Tokenize(source))
      , pos(tokens.begin())
    { }

    [[nodiscard]] compiled_query_t Compile() {
      if (pos.None()) Error("query is empty");
      Expression expression = ParseTernary();
      if (pos.Any()) Error("unexpected token");
      return compiled_query_t(manager.avida, source, expression.type, std::move(expression.eval));
    }
  };

public:
  explicit QueryManager(AVIDA_T & in_avida)
    : avida(in_avida)
    , ident_id(lexer.AddToken("identifier", "[a-zA-Z_][a-zA-Z0-9_]*"))
    , int_id(lexer.AddToken("integer", "[0-9]+"))
    , double_id(lexer.AddToken(
        "double", "([0-9]+(\\.[0-9]*)?|\\.[0-9]+)([eE][-+]?[0-9]+)?"
      ))
    , string_id(lexer.AddToken(
        "string", "(\\\"([^\"\\\\]|(\\\\.))*\\\")|(\\'([^'\\\\]|(\\\\.))*\\')"
      ))
    , logical_or_id(lexer.AddToken("logical_or", "\\|\\|"))
    , logical_and_id(lexer.AddToken("logical_and", "&&"))
    , equal_id(lexer.AddToken("equal", "=="))
    , not_equal_id(lexer.AddToken("not_equal", "!="))
    , less_equal_id(lexer.AddToken("less_equal", "<="))
    , greater_equal_id(lexer.AddToken("greater_equal", ">="))
    , power_id(lexer.AddToken("power", "\\*\\*"))
  {
    lexer.IgnoreToken("whitespace", "[ \\t\\r\\n]+");
    lexer.Generate();
    SetupFunctions();
  }

  template <typename GETTER_T,
            typename RESULT_T = std::remove_cvref_t<std::invoke_result_t<GETTER_T>>>
    requires std::invocable<GETTER_T>
  void RegisterValue(const emp::String & name, GETTER_T getter) {
    if (!name.IsIdentifierChain() || name == "true" || name == "false" || name == "null") {
      emp::notify::Error("Invalid query value name '", name, "'.");
    }
    if (value_map.contains(name)) {
      emp::notify::Error("Query value '", name, "' is already registered.");
    }
    value_map.emplace(name, ValueInfo{
      GetQueryType<RESULT_T>(),
      [getter=std::move(getter)](const context_t &){ return value_t(getter()); }
    });
  }

  [[nodiscard]] bool HasValue(const emp::String & name) const {
    return value_map.contains(name);
  }

  [[nodiscard]] compiled_query_t Compile(const emp::String & source) const {
    return Parser(*this, source).Compile();
  }

  [[nodiscard]] value_t Evaluate(const emp::String & source) const {
    return Compile(source).Evaluate();
  }
};
