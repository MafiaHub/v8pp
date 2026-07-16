#pragma once

#include <cstddef>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <optional>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <unordered_map>
#include <vector>

#include "v8pp/type_info.hpp"
#include "v8pp/utility.hpp"

namespace v8pp::metadata {

enum class symbol_kind
{
	global_object,
	constructor,
	data_type
};

struct type
{
	std::string name;
	std::string cpp_name;
	bool optional = false;
};

struct parameter
{
	std::string name;
	type value_type;
	std::string description;
};

struct signature
{
	std::vector<parameter> parameters;
	type return_type;
	std::string return_description;
};

struct parameter_options
{
	std::string name;
	std::string description;
	std::string type;
	std::optional<bool> optional;
};

struct function_options
{
	std::string description;
	std::vector<parameter_options> parameters;
	std::string return_description;
	std::optional<signature> explicit_signature;
	std::string since;
	std::string deprecated;
	std::vector<std::string> examples;
};

struct function
{
	std::string name;
	std::string description;
	signature call_signature;
	bool static_ = false;
	std::string since;
	std::string deprecated;
	std::vector<std::string> examples;
};

struct property
{
	std::string name;
	std::string description;
	type value_type;
	bool readonly = false;
	bool static_ = false;
};

struct property_options
{
	std::string description;
	std::string type;
	bool readonly = false;
	bool static_ = false;
};

struct variable
{
	std::string name;
	std::string description;
	type value_type;
	bool readonly = true;
};

struct variable_options
{
	std::string type;
	std::string description;
	bool readonly = true;
};

template<typename Function>
function function_of(std::string_view name, function_options const& options = {}, bool static_ = false);

struct symbol
{
	std::string name;
	std::string description;
	symbol_kind kind = symbol_kind::global_object;
	std::optional<function> constructor;
	std::vector<function> functions;
	std::vector<property> properties;
	std::vector<std::string> bases;

	function& record(function value)
	{
		for (auto& existing : functions)
		{
			if (existing.name == value.name && existing.static_ == value.static_)
			{
				existing = std::move(value);
				return existing;
			}
		}
		functions.push_back(std::move(value));
		return functions.back();
	}

	property& record(property value)
	{
		for (auto& existing : properties)
		{
			if (existing.name == value.name && existing.static_ == value.static_)
			{
				existing = std::move(value);
				return existing;
			}
		}
		properties.push_back(std::move(value));
		return properties.back();
	}

	template<typename Signature>
	function& add_function(std::string_view function_name, function_options const& options = {})
	{
		return record(function_of<Signature>(function_name, options));
	}

	property& add_property(std::string_view property_name, type property_type,
		std::string description = {}, bool readonly = false, bool static_ = false)
	{
		return record({ std::string(property_name), std::move(description),
			std::move(property_type), readonly, static_ });
	}

	property& add_property(std::string_view property_name, std::string type_name,
		std::string description = {}, bool readonly = false, bool static_ = false)
	{
		return add_property(property_name, type{ std::move(type_name), {}, false },
			std::move(description), readonly, static_);
	}
};

class registry
{
public:
	symbol& global_object(std::string name, std::string description = {})
	{
		return add(symbol_kind::global_object, std::move(name), std::move(description));
	}

	symbol& constructor(std::string name, std::string description = {})
	{
		return add(symbol_kind::constructor, std::move(name), std::move(description));
	}

	symbol& data_type(std::string name, std::string description = {})
	{
		return add(symbol_kind::data_type, std::move(name), std::move(description));
	}

	variable& variable_(std::string name, type value_type,
		std::string description = {}, bool readonly = true)
	{
		for (auto& existing : variables_)
		{
			if (existing.name == name)
			{
				existing = { std::move(name), std::move(description), std::move(value_type), readonly };
				return existing;
			}
		}
		variables_.push_back({ std::move(name), std::move(description),
			std::move(value_type), readonly });
		return variables_.back();
	}

	std::deque<symbol> const& symbols() const { return symbols_; }
	std::deque<variable> const& variables() const { return variables_; }

private:
	symbol& add(symbol_kind kind, std::string name, std::string description)
	{
		for (auto& existing : symbols_)
		{
			if (existing.name == name)
			{
				if (existing.kind != kind) throw std::invalid_argument("metadata symbol kind mismatch");
				if (!description.empty()) existing.description = std::move(description);
				return existing;
			}
		}
		symbols_.push_back({ std::move(name), std::move(description), kind, {}, {}, {}, {} });
		return symbols_.back();
	}

	std::deque<symbol> symbols_;
	std::deque<variable> variables_;
};

inline registry& catalog(std::string_view name)
{
	static std::mutex mutex;
	static std::unordered_map<std::string, std::unique_ptr<registry>> catalogs;
	std::lock_guard lock(mutex);
	auto& result = catalogs[std::string(name)];
	if (!result) result = std::make_unique<registry>();
	return *result;
}

namespace detail {

template<typename T>
struct optional_value
{
	using type = T;
	static constexpr bool value = false;
};

template<typename T>
struct optional_value<std::optional<T>>
{
	using type = T;
	static constexpr bool value = true;
};

template<typename T>
type describe_type()
{
	using raw_type = std::remove_cvref_t<T>;
	using optional = optional_value<raw_type>;
	using value_type = typename optional::type;
	using plain_type = std::remove_cvref_t<value_type>;

	type result;
	result.cpp_name = std::string(v8pp::detail::type_id<T>().name());
	result.optional = optional::value;

	if constexpr (std::is_void_v<plain_type>)
	{
		result.name = "void";
	}
	else if constexpr (std::is_same_v<plain_type, bool>)
	{
		result.name = "boolean";
	}
	else if constexpr (std::is_arithmetic_v<plain_type>)
	{
		result.name = "number";
	}
	else if constexpr (std::is_same_v<plain_type, std::string> || std::is_same_v<plain_type, std::string_view> || (std::is_pointer_v<plain_type> && std::is_same_v<std::remove_cv_t<std::remove_pointer_t<plain_type>>, char>))
	{
		result.name = "string";
	}
	else
	{
		result.name = std::string(v8pp::detail::type_id<plain_type>().name());
	}
	return result;
}

template<typename Tuple, std::size_t Offset, std::size_t... I>
std::vector<parameter> describe_parameters(std::index_sequence<I...>)
{
	std::vector<parameter> result;
	result.reserve(sizeof...(I));
	(result.push_back({ "arg" + std::to_string(I), describe_type<std::tuple_element_t<I + Offset, Tuple>>(), {} }), ...);
	return result;
}

template<typename Function>
signature infer_signature()
{
	using function_type = std::decay_t<Function>;
	using traits = v8pp::detail::function_traits<function_type>;
	using arguments = typename traits::arguments;
	constexpr bool member = std::is_member_function_pointer_v<function_type>;
	constexpr std::size_t offset = member ? 1 : 0;
	constexpr std::size_t argument_count = std::tuple_size_v<arguments> - offset;

	return {
		describe_parameters<arguments, offset>(std::make_index_sequence<argument_count>{}),
		describe_type<typename traits::return_type>(),
		{}
	};
}

inline void apply_options(signature& result, function_options const& options)
{
	for (std::size_t index = 0; index < options.parameters.size() && index < result.parameters.size(); ++index)
	{
		auto const& source = options.parameters[index];
		auto& target = result.parameters[index];
		if (!source.name.empty()) target.name = source.name;
		if (!source.description.empty()) target.description = source.description;
		if (!source.type.empty()) target.value_type.name = source.type;
		if (source.optional) target.value_type.optional = *source.optional;
	}
	if (!options.return_description.empty()) result.return_description = options.return_description;
}

} // namespace detail

template<typename T>
type type_of()
{
	return detail::describe_type<T>();
}

template<typename Return, typename... Args>
signature signature_of(std::vector<parameter_options> parameters = {}, std::string return_description = {})
{
	signature result{
		detail::describe_parameters<std::tuple<Args...>, 0>(std::index_sequence_for<Args...>{}),
		detail::describe_type<Return>(),
		std::move(return_description)
	};
	detail::apply_options(result, function_options{ .parameters = std::move(parameters) });
	return result;
}

inline parameter_options param(std::string name, std::string type,
	bool optional = false, std::string description = {})
{
	return { std::move(name), std::move(description), std::move(type), optional };
}

inline function_options docs(std::string return_type,
	std::initializer_list<parameter_options> parameters = {},
	std::string description = {}, std::string return_description = {},
	std::string since = {}, std::string deprecated = {},
	std::vector<std::string> examples = {})
{
	function_options result;
	result.description = std::move(description);
	result.since = std::move(since);
	result.deprecated = std::move(deprecated);
	result.examples = std::move(examples);
	result.explicit_signature = signature{
		{}, { std::move(return_type), {}, false }, std::move(return_description)
	};
	result.explicit_signature->parameters.reserve(parameters.size());
	for (auto const& parameter : parameters)
	{
		result.explicit_signature->parameters.push_back({ parameter.name,
			{ parameter.type, {}, parameter.optional.value_or(false) }, parameter.description });
	}
	return result;
}

inline property_options property_docs(std::string type, std::string description = {})
{
	return { std::move(description), std::move(type), false, false };
}

template<typename Function>
function function_of(std::string_view name, function_options const& options, bool static_)
{
	auto call_signature = options.explicit_signature.value_or(detail::infer_signature<Function>());
	detail::apply_options(call_signature, options);
	return {
		std::string(name),
		options.description,
		std::move(call_signature),
		static_,
		options.since,
		options.deprecated,
		options.examples
	};
}

namespace detail {

inline std::string escape_json(std::string_view value)
{
	std::string result;
	result.reserve(value.size());
	for (char ch : value)
	{
		switch (ch)
		{
		case '\\':
			result += "\\\\";
			break;
		case '"':
			result += "\\\"";
			break;
		case '\n':
			result += "\\n";
			break;
		case '\r':
			result += "\\r";
			break;
		case '\t':
			result += "\\t";
			break;
		default:
			if (static_cast<unsigned char>(ch) < 0x20)
			{
				constexpr char hex[] = "0123456789abcdef";
				result += "\\u00";
				result += hex[(static_cast<unsigned char>(ch) >> 4) & 0x0f];
				result += hex[static_cast<unsigned char>(ch) & 0x0f];
			}
			else
			{
				result += ch;
			}
			break;
		}
	}
	return result;
}

inline void write_string(std::ostream& output, std::string_view value)
{
	output << '"' << escape_json(value) << '"';
}

inline void write_type(std::ostream& output, type const& value)
{
	output << "{\"name\":";
	write_string(output, value.name);
	output << ",\"cppName\":";
	write_string(output, value.cpp_name);
	output << ",\"optional\":" << (value.optional ? "true" : "false") << '}';
}

inline void write_function(std::ostream& output, function const& value)
{
	output << "{\"name\":";
	write_string(output, value.name);
	output << ",\"description\":";
	write_string(output, value.description);
	output << ",\"static\":" << (value.static_ ? "true" : "false") << ",\"parameters\":[";
	for (std::size_t index = 0; index < value.call_signature.parameters.size(); ++index)
	{
		if (index) output << ',';
		auto const& parameter = value.call_signature.parameters[index];
		output << "{\"name\":";
		write_string(output, parameter.name);
		output << ",\"description\":";
		write_string(output, parameter.description);
		output << ",\"type\":";
		write_type(output, parameter.value_type);
		output << '}';
	}
	output << "],\"returns\":{\"description\":";
	write_string(output, value.call_signature.return_description);
	output << ",\"type\":";
	write_type(output, value.call_signature.return_type);
	output << "},\"since\":";
	write_string(output, value.since);
	output << ",\"deprecated\":";
	write_string(output, value.deprecated);
	output << ",\"examples\":[";
	for (std::size_t index = 0; index < value.examples.size(); ++index)
	{
		if (index) output << ',';
		write_string(output, value.examples[index]);
	}
	output << "]}";
}

inline void write_property(std::ostream& output, property const& value)
{
	output << "{\"name\":";
	write_string(output, value.name);
	output << ",\"description\":";
	write_string(output, value.description);
	output << ",\"type\":";
	write_type(output, value.value_type);
	output << ",\"readonly\":" << (value.readonly ? "true" : "false")
		<< ",\"static\":" << (value.static_ ? "true" : "false") << '}';
}

inline void write_variable(std::ostream& output, variable const& value)
{
	output << "{\"name\":";
	write_string(output, value.name);
	output << ",\"description\":";
	write_string(output, value.description);
	output << ",\"type\":";
	write_type(output, value.value_type);
	output << ",\"readonly\":" << (value.readonly ? "true" : "false") << '}';
}

inline void write_symbol(std::ostream& output, symbol const& value)
{
	output << "{\"name\":";
	write_string(output, value.name);
	output << ",\"description\":";
	write_string(output, value.description);
	output << ",\"kind\":";
	write_string(output, value.kind == symbol_kind::constructor ? "constructor"
		: value.kind == symbol_kind::data_type ? "dataType" : "globalObject");
	if (value.kind == symbol_kind::constructor)
	{
		output << ",\"constructor\":";
		if (value.constructor)
		{
			write_function(output, *value.constructor);
		}
		else
		{
			output << "null";
		}
	}
	output << ",\"functions\":[";
	for (std::size_t index = 0; index < value.functions.size(); ++index)
	{
		if (index) output << ',';
		write_function(output, value.functions[index]);
	}
	output << "],\"properties\":[";
	for (std::size_t index = 0; index < value.properties.size(); ++index)
	{
		if (index) output << ',';
		write_property(output, value.properties[index]);
	}
	output << "],\"bases\":[";
	for (std::size_t index = 0; index < value.bases.size(); ++index)
	{
		if (index) output << ',';
		write_string(output, value.bases[index]);
	}
	output << "]}";
}

} // namespace detail

template<typename Symbols>
void write_json_symbols(std::ostream& output, Symbols const& symbols)
{
	output << "{\"schemaVersion\":2,\"symbols\":[";
	for (std::size_t symbol_index = 0; symbol_index < symbols.size(); ++symbol_index)
	{
		if (symbol_index) output << ',';
		detail::write_symbol(output, symbols[symbol_index]);
	}
	output << "]}";
}

inline void write_json(std::ostream& output, std::vector<symbol> const& symbols)
{
	write_json_symbols(output, symbols);
}

inline void write_json(std::ostream& output, std::deque<symbol> const& symbols)
{
	write_json_symbols(output, symbols);
}

inline void write_json(std::ostream& output, registry const& value)
{
	output << "{\"schemaVersion\":2,\"symbols\":[";
	for (std::size_t symbol_index = 0; symbol_index < value.symbols().size(); ++symbol_index)
	{
		if (symbol_index) output << ',';
		detail::write_symbol(output, value.symbols()[symbol_index]);
	}
	output << "],\"variables\":[";
	for (std::size_t index = 0; index < value.variables().size(); ++index)
	{
		if (index) output << ',';
		detail::write_variable(output, value.variables()[index]);
	}
	output << "]}";
}

inline std::string to_json(registry const& value)
{
	std::ostringstream output;
	write_json(output, value);
	return output.str();
}

inline bool write_json_file(registry const& value, std::string const& path)
{
	std::ofstream output(path, std::ios::binary | std::ios::trunc);
	if (!output) return false;
	write_json(output, value);
	output << '\n';
	return static_cast<bool>(output);
}

inline bool export_catalog_from_environment(std::string_view catalog_name,
	char const* environment_variable)
{
	auto const* path = std::getenv(environment_variable);
	return !path || !*path || write_json_file(catalog(catalog_name), path);
}

} // namespace v8pp::metadata
