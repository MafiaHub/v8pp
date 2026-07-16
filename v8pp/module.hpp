#pragma once

#include <stdexcept>
#include <type_traits>

#include <v8.h>

#include "v8pp/function.hpp"
#include "v8pp/metadata.hpp"
#include "v8pp/property.hpp"

namespace v8pp {

template<typename Function, typename Traits = raw_ptr_traits>
void bind_function(v8::Isolate* isolate, v8::Local<v8::Object> object,
	metadata::symbol& metadata_symbol, std::string_view name, Function&& function,
	metadata::function_options const& options = {})
{
	using function_type = std::decay_t<Function>;
	static_assert(detail::is_callable<function_type>::value, "Function must be callable");
	metadata_symbol.record(metadata::function_of<function_type>(name, options));
	auto context = isolate->GetCurrentContext();
	auto wrapped = wrap_function_template<Function, Traits>(isolate, std::forward<Function>(function));
	object->Set(context, v8pp::to_v8(isolate, name), wrapped->GetFunction(context).ToLocalChecked()).Check();
}

template<typename Data>
void set_global(v8::Isolate* isolate, v8::Local<v8::Object> global,
	metadata::registry& registry, std::string name, v8::Local<Data> value,
	metadata::variable_options options)
{
	bool const readonly = options.readonly;
	registry.variable_(name, { std::move(options.type), {}, false },
		std::move(options.description), readonly);
	auto const attributes = readonly ? v8::ReadOnly : v8::None;
	global->DefineOwnProperty(isolate->GetCurrentContext(), v8pp::to_v8(isolate, name),
		value, attributes).Check();
}

inline void set_global_accessor(v8::Isolate* isolate, v8::Local<v8::Object> global,
	metadata::registry& registry, std::string name, v8::Local<v8::Function> getter,
	metadata::variable_options options)
{
	if (!options.readonly)
	{
		throw std::invalid_argument("getter-only global accessor must be readonly");
	}
	registry.variable_(name, { std::move(options.type), {}, false },
		std::move(options.description), true);
	global->SetAccessorProperty(v8pp::to_v8(isolate, name).As<v8::Name>(), getter,
		v8::Local<v8::Function>{}, v8::ReadOnly);
}

inline void publish(v8::Isolate* isolate, v8::Local<v8::Object> global,
	metadata::symbol const& metadata_symbol, v8::Local<v8::Object> value)
{
	if (metadata_symbol.kind != metadata::symbol_kind::global_object)
	{
		throw std::invalid_argument("v8pp::publish metadata must describe a global object");
	}
	global->Set(isolate->GetCurrentContext(), v8pp::to_v8(isolate, metadata_symbol.name), value).Check();
}

template<typename T, typename Traits>
class class_;

/// Module (similar to v8::ObjectTemplate)
class module
{
public:
	/// Create new module in the specified V8 isolate
	explicit module(v8::Isolate* isolate)
		: isolate_(isolate)
		, obj_(v8::ObjectTemplate::New(isolate))
		, metadata_(nullptr)
	{
	}

	/// Create a module and record its bindings in a metadata object
	explicit module(v8::Isolate* isolate, metadata::symbol& metadata_symbol)
		: isolate_(isolate)
		, obj_(v8::ObjectTemplate::New(isolate))
		, metadata_(&metadata_symbol)
	{
		if (metadata_symbol.kind != metadata::symbol_kind::global_object)
		{
			throw std::invalid_argument("v8pp::module metadata must describe a global object");
		}
	}

	explicit module(v8::Isolate* isolate, metadata::registry& registry,
		std::string name, std::string description = {})
		: module(isolate, registry.global_object(std::move(name), std::move(description)))
	{
	}

	/// Create new module in the specified V8 isolate for existing ObjectTemplate
	explicit module(v8::Isolate* isolate, v8::Local<v8::ObjectTemplate> obj)
		: isolate_(isolate)
		, obj_(obj)
		, metadata_(nullptr)
	{
	}

	/// Create a module for an existing ObjectTemplate and record binding metadata
	explicit module(v8::Isolate* isolate, v8::Local<v8::ObjectTemplate> obj,
		metadata::symbol& metadata_symbol)
		: isolate_(isolate)
		, obj_(obj)
		, metadata_(&metadata_symbol)
	{
		if (metadata_symbol.kind != metadata::symbol_kind::global_object)
		{
			throw std::invalid_argument("v8pp::module metadata must describe a global object");
		}
	}

	module(module const&) = delete;
	module& operator=(module const&) = delete;

	module(module&&) = default;
	module& operator=(module&&) = default;

	/// v8::Isolate where the module belongs
	v8::Isolate* isolate() { return isolate_; }

	/// V8 ObjectTemplate implementation
	v8::Local<v8::ObjectTemplate> impl() const { return obj_; }

	/// Set a V8 value in the module with specified name
	template<typename Data>
	module& value(std::string_view name, v8::Local<Data> value)
	{
		static_assert(!std::is_base_of_v<v8::Object, Data>,
			"Concrete V8 objects must be set on a materialized module instance");
		obj_->Set(v8pp::to_v8(isolate_, name), value);
		return *this;
	}

	/// Set a V8 value and record its property metadata
	template<typename Data>
	module& value(std::string_view name, v8::Local<Data> value,
		metadata::property_options options)
	{
		static_assert(!std::is_base_of_v<v8::Object, Data>,
			"Concrete V8 objects must be set on a materialized module instance");
		bool const readonly = options.readonly;
		document_property(name, std::move(options));
		obj_->Set(v8pp::to_v8(isolate_, name), value, readonly ? v8::ReadOnly : v8::None);
		return *this;
	}

	/// Set a concrete V8 value on an instance created from this module.
	/// Use this overload for values such as Object and Array, which V8 does not allow
	/// to be stored directly in an ObjectTemplate.
	template<typename Data>
	module& value(v8::Local<v8::Object> instance, std::string_view name,
		v8::Local<Data> value)
	{
		static_assert(std::is_base_of_v<v8::Value, Data>,
			"Materialized module instance properties must be V8 values");
		instance->Set(isolate_->GetCurrentContext(), v8pp::to_v8(isolate_, name), value).Check();
		return *this;
	}

	/// Set a concrete V8 value on a module instance and record its property metadata.
	template<typename Data>
	module& value(v8::Local<v8::Object> instance, std::string_view name,
		v8::Local<Data> value, metadata::property_options options)
	{
		static_assert(std::is_base_of_v<v8::Value, Data>,
			"Materialized module instance properties must be V8 values");
		bool const readonly = options.readonly;
		document_property(name, std::move(options));
		instance->DefineOwnProperty(isolate_->GetCurrentContext(), v8pp::to_v8(isolate_, name),
			value, readonly ? v8::ReadOnly : v8::None).Check();
		return *this;
	}

	/// Set submodule in the module with specified name
	module& submodule(std::string_view name, v8pp::module& m)
	{
		return value(name, m.obj_);
	}

	/// Set wrapped C++ class in the module with specified name
	template<typename T, typename Traits>
	module& class_(std::string_view name, v8pp::class_<T, Traits>& cl)
	{
		v8::HandleScope scope(isolate_);

		cl.class_function_template()->SetClassName(v8pp::to_v8(isolate_, name));
		return value(name, cl.js_function_template());
	}

	/// Set a C++ function in the module with specified name
	template<typename Function, typename Traits = raw_ptr_traits>
	module& function(std::string_view name, Function&& func)
	{
		return function<Function, Traits>(name, std::forward<Function>(func), {});
	}

	/// Set a C++ function and record documentation metadata
	template<typename Function, typename Traits = raw_ptr_traits>
	module& function(std::string_view name, Function&& func,
		metadata::function_options const& options)
	{
		using Fun = typename std::decay_t<Function>;
		static_assert(detail::is_callable<Fun>::value, "Function must be callable");
		if (metadata_)
		{
			metadata_->record(metadata::function_of<Fun>(name, options));
		}
		return value(name, wrap_function_template<Function, Traits>(isolate_, std::forward<Function>(func)));
	}

	/// Bind a C++ function using an authoritative metadata descriptor
	template<typename Function, typename Traits = raw_ptr_traits>
	module& function(metadata::function const& binding, Function&& func)
	{
		using Fun = typename std::decay_t<Function>;
		static_assert(detail::is_callable<Fun>::value, "Function must be callable");
		if (metadata_) metadata_->record(binding);
		return value(binding.name,
			wrap_function_template<Function, Traits>(isolate_, std::forward<Function>(func)));
	}

	module& document_property(std::string_view name, metadata::property_options options)
	{
		if (metadata_)
		{
			metadata_->record({ std::string(name), std::move(options.description),
				{ std::move(options.type), {}, false }, options.readonly, options.static_ });
		}
		return *this;
	}

	module& publish(v8::Local<v8::Object> global)
	{
		if (!metadata_) throw std::logic_error("v8pp::module::publish requires metadata");
		auto context = isolate_->GetCurrentContext();
		global->Set(context, v8pp::to_v8(isolate_, metadata_->name), new_instance()).Check();
		return *this;
	}

	module& publish(v8::Local<v8::Object> global, v8::Local<v8::Object> instance)
	{
		if (!metadata_) throw std::logic_error("v8pp::module::publish requires metadata");
		auto context = isolate_->GetCurrentContext();
		global->Set(context, v8pp::to_v8(isolate_, metadata_->name), instance).Check();
		return *this;
	}

	metadata::symbol const* metadata_symbol() const { return metadata_; }

	/// Set a C++ variable in the module with specified name
	template<typename Variable>
	module& var(char const* name, Variable& var)
	{
		static_assert(!detail::is_callable<Variable>::value, "Variable must not be callable");
		v8::HandleScope scope(isolate_);

		v8::Local<v8::Name> v8_name = v8pp::to_v8(isolate_, name);
		v8::AccessorNameGetterCallback getter = &var_get<Variable>;
		v8::AccessorNameSetterCallback setter = &var_set<Variable>;
		v8::Local<v8::Value> data = detail::external_data::set(isolate_, &var);
		obj_->SetNativeDataProperty(v8_name, getter, setter, data, v8::PropertyAttribute::DontDelete);
		return *this;
	}

	/// Set property in the module with specified name and get/set functions
	template<typename GetFunction, typename SetFunction = detail::none>
	module& property(char const* name, GetFunction&& get, SetFunction&& set = {})
	{
		using Getter = typename std::decay_t<GetFunction>;
		using Setter = typename std::decay_t<SetFunction>;

		static_assert(detail::is_callable<Getter>::value, "GetFunction must be callable");
		static_assert(detail::is_callable<Setter>::value || std::same_as<Setter, detail::none>, "SetFunction must be callable");

		using property_type = v8pp::property<Getter, Setter, detail::none, detail::none>;
		using Traits = detail::none;

		v8::HandleScope scope(isolate_);

		v8::Local<v8::Name> v8_name = v8pp::to_v8(isolate_, name);
		v8::AccessorNameGetterCallback getter = property_type::template get<Traits>;
		v8::AccessorNameSetterCallback setter = property_type::is_readonly ? nullptr : property_type::template set<Traits>;
		v8::Local<v8::Value> data = detail::external_data::set(isolate_, property_type(std::move(get), std::move(set)));
		obj_->SetNativeDataProperty(v8_name, getter, setter, data,
			v8::PropertyAttribute(v8::DontDelete | (property_type::is_readonly ? v8::ReadOnly : 0)));
		return *this;
	}

	/// Set a read-only property and record its metadata
	template<typename GetFunction>
	module& property(char const* name, GetFunction&& get, metadata::property_options options)
	{
		options.readonly = true;
		document_property(name, std::move(options));
		return property(name, std::forward<GetFunction>(get));
	}

	/// Set a read/write property and record its metadata
	template<typename GetFunction, typename SetFunction>
	module& property(char const* name, GetFunction&& get, SetFunction&& set,
		metadata::property_options options)
	{
		options.readonly = false;
		document_property(name, std::move(options));
		return property(name, std::forward<GetFunction>(get), std::forward<SetFunction>(set));
	}

	/// Set another module as a read-only property
	module& const_(std::string_view name, module& m)
	{
		v8::HandleScope scope(isolate_);

		obj_->Set(v8pp::to_v8(isolate_, name), m.obj_,
			v8::PropertyAttribute(v8::ReadOnly | v8::DontDelete));
		return *this;
	}

	/// Set a value convertible to JavaScript as a read-only property
	template<typename Value>
	module& const_(std::string_view name, Value const& value)
	{
		v8::HandleScope scope(isolate_);

		obj_->Set(v8pp::to_v8(isolate_, name), to_v8(isolate_, value),
			v8::PropertyAttribute(v8::ReadOnly | v8::DontDelete));
		return *this;
	}

	/// Set a documented read-only value
	template<typename Value>
	module& const_(std::string_view name, Value const& value, metadata::property_options options)
	{
		options.readonly = true;
		document_property(name, std::move(options));
		return const_(name, value);
	}

	/// Create a new module instance in V8
	v8::Local<v8::Object> new_instance()
	{
		return obj_->NewInstance(isolate_->GetCurrentContext()).ToLocalChecked();
	}

private:
	template<typename Variable>
	static void var_get(v8::Local<v8::Name>, v8::PropertyCallbackInfo<v8::Value> const& info)
	{
		v8::Isolate* isolate = info.GetIsolate();

		Variable* var = detail::external_data::get<Variable*>(info.Data());
		info.GetReturnValue().Set(to_v8(isolate, *var));
	}

	template<typename Variable>
	static void var_set(v8::Local<v8::Name>, v8::Local<v8::Value> value, v8::PropertyCallbackInfo<void> const& info)
	{
		v8::Isolate* isolate = info.GetIsolate();

		Variable* var = detail::external_data::get<Variable*>(info.Data());
		*var = v8pp::from_v8<Variable>(isolate, value);
	}

	v8::Isolate* isolate_;
	v8::Local<v8::ObjectTemplate> obj_;
	metadata::symbol* metadata_;
};

} // namespace v8pp
