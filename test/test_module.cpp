#include "v8pp/module.hpp"
#include "v8pp/context.hpp"
#include "v8pp/property.hpp"

#include "test.hpp"

#include <type_traits>

static std::string var;

static int fun(int x)
{
	return x + 1;
}

static int x = 1;
static int get_x()
{
	return x + 1;
}
static void set_x(int v)
{
	x = v - 1;
}

static void get_answer(v8::FunctionCallbackInfo<v8::Value> const& args)
{
	args.GetReturnValue().Set(42);
}

static_assert(std::is_move_constructible_v<v8pp::module>);
static_assert(std::is_move_assignable_v<v8pp::module>);
static_assert(!std::is_copy_assignable_v<v8pp::module>);
static_assert(!std::is_copy_constructible_v<v8pp::module>);

void test_module()
{
	v8pp::context context;

	v8::HandleScope scope(context.isolate());

	v8pp::module module(context.isolate());
	v8pp::module consts(context.isolate());
	v8pp::metadata::registry metadata;
	v8pp::module documented(context.isolate(), metadata, "documented", "Documented test module");
	auto& documented_api = metadata.global_object("documented");
	check_eq("module metadata symbol", documented.metadata_symbol(), &documented_api);
	check_eq("module isolate", documented.isolate(), context.isolate());
	auto object_template = v8::ObjectTemplate::New(context.isolate());
	auto& template_api = metadata.global_object("templated");
	v8pp::module templated(context.isolate(), object_template, template_api);
	check("module existing template", templated.impl() == object_template);
	check_eq("module existing template metadata", templated.metadata_symbol(), &template_api);
	check_ex<std::invalid_argument>("module rejects constructor metadata", [&context, &metadata]()
		{ v8pp::module invalid(context.isolate(), metadata.constructor("InvalidModule")); });
	check_ex<std::invalid_argument>("existing module rejects constructor metadata",
		[&context, &metadata, object_template]()
			{ v8pp::module invalid(context.isolate(), object_template, metadata.constructor("InvalidTemplateModule")); });
	check_ex<std::logic_error>("undocumented module cannot publish", [&context, &module]()
		{ module.publish(context.isolate()->GetCurrentContext()->Global()); });

	consts
		.const_("bool", true)
		.const_("char", 'Z')
		.const_("int", 100)
		.const_("str", "str")
		.const_("num", 99.9);

	module
		.submodule("consts", consts)
		.const_("readonlyConsts", consts)
		.var("var", var)
		.function("fun", &fun)
		.value("empty", v8::Null(context.isolate()))
		.property("rprop", get_x)
		.property("wprop", get_x, set_x);

	v8pp::metadata::function_options function_docs;
	function_docs.description = "Increment a number";
	function_docs.parameters = { { .name = "value", .description = "Input number" } };
	documented.function("fun", &fun, function_docs);
	auto& descriptor = documented_api.add_function<int(int)>("descriptorFun");
	documented.function(descriptor, &fun);
	documented.document_property("status", { "Module status", "string", true, false });
	documented
		.value("readonlyValue", v8pp::to_v8(context.isolate(), 10),
			{ "Read-only value", "number", true, true })
		.value("writableValue", v8pp::to_v8(context.isolate(), 11),
			{ "Writable value", "number", false, true })
		.const_("constantValue", 12,
			{ "Constant value", "number", false, true })
		.property("documentedReadonlyProperty", &get_x,
			{ "Documented read-only property", "number", false, true })
		.property("documentedWritableProperty", &get_x, &set_x,
			{ "Documented writable property", "number", true, true });
	module.document_property("ignored", { "Not recorded", "string", false, false });

	context.module("module", module);
	auto global = context.isolate()->GetCurrentContext()->Global();
	documented.publish(global);

	v8pp::module supplied_instance(context.isolate(), metadata, "suppliedInstance");
	supplied_instance.function("fun", &fun,
		v8pp::metadata::docs("number", { v8pp::metadata::param("value", "number") }));
	auto instance = supplied_instance.new_instance();
	supplied_instance.value(instance, "marker", v8pp::to_v8(context.isolate(), 7),
		{ "Materialized object value", "number", true, true });
	supplied_instance.publish(global, instance);

	auto raw = v8::Object::New(context.isolate());
	auto& raw_api = metadata.global_object("raw");
	v8pp::bind_function(context.isolate(), raw, raw_api, "fun", &fun,
		v8pp::metadata::docs("number", { v8pp::metadata::param("value", "number") },
			"Increment a raw object value"));
	v8pp::publish(context.isolate(), global, raw_api, raw);
	check_ex<std::invalid_argument>("raw publish rejects constructor metadata",
		[&context, &metadata, global, raw]()
			{ v8pp::publish(context.isolate(), global, metadata.constructor("NotRaw"), raw); });

	v8pp::set_global(context.isolate(), global, metadata, "answer", v8pp::to_v8(context.isolate(), 42),
		{ "number", "The answer", true });
	v8pp::set_global(context.isolate(), global, metadata, "mutableAnswer", v8pp::to_v8(context.isolate(), 1),
		{ "number", "A mutable answer", false });
	auto getter = v8::Function::New(context.isolate()->GetCurrentContext(), &get_answer).ToLocalChecked();
	v8pp::set_global_accessor(context.isolate(), global, metadata, "dynamicAnswer", getter,
		{ "number", "A computed answer", true });
	check_ex<std::invalid_argument>("writable getter-only global rejected",
		[&context, &metadata, global, getter]()
			{ v8pp::set_global_accessor(context.isolate(), global, metadata,
				"invalidAccessor", getter, { "number", "Invalid accessor", false }); });

	check_eq("module.consts.bool",
		run_script<bool>(context, "module.consts.bool"), true);
	check_eq("module.consts.char",
		run_script<char>(context, "module.consts.char"), 'Z');
	check_eq("module.consts.int",
		run_script<char>(context, "module.consts.int"), 100);
	check_eq("module.consts.str",
		run_script<std::string>(context, "module.consts.str"), "str");

	check_eq("module.var", run_script<std::string>(context, "module.var = 'test'; module.var"), "test");
	check_eq("var", var, "test");

	check_eq("module.fun",
		run_script<int>(context, "module.fun(100)"), 101);
	check_eq("documented.fun",
		run_script<int>(context, "documented.fun(100)"), 101);
	check_eq("documented.descriptorFun",
		run_script<int>(context, "documented.descriptorFun(100)"), 101);
	check_eq("documented readonly value", run_script<int>(context,
		"documented.readonlyValue = 1; documented.readonlyValue"), 10);
	check_eq("documented writable value", run_script<int>(context,
		"documented.writableValue = 1; documented.writableValue"), 1);
	check_eq("documented constant value", run_script<int>(context,
		"documented.constantValue = 1; documented.constantValue"), 12);
	check_eq("documented readonly property", run_script<int>(context,
		"documented.documentedReadonlyProperty"), 2);
	check_eq("documented writable property", run_script<int>(context,
		"documented.documentedWritableProperty = 10; documented.documentedWritableProperty"), 10);
	x = 1;
	check_eq("supplied module instance",
		run_script<int>(context, "suppliedInstance.marker"), 7);
	check_eq("supplied module instance function",
		run_script<int>(context, "suppliedInstance.fun(7)"), 8);
	check_eq("supplied module readonly instance value", run_script<int>(context,
		"suppliedInstance.marker = 8; suppliedInstance.marker"), 7);
	check_eq("supplied module instance metadata",
		metadata.global_object("suppliedInstance").properties[0].readonly, true);
	check_eq("raw object function",
		run_script<int>(context, "raw.fun(100)"), 101);
	check_eq("global value", run_script<int>(context, "answer"), 42);
	check_eq("readonly global assignment", run_script<int>(context, "answer = 7; answer"), 42);
	check_ex<std::runtime_error>("strict readonly global assignment", [&context]()
		{ run_script<int>(context, "'use strict'; answer = 7; answer"); });
	check_eq("writable global assignment", run_script<int>(context,
		"mutableAnswer = 7; mutableAnswer"), 7);
	check_eq("global accessor", run_script<int>(context, "dynamicAnswer"), 42);
	check_eq("readonly accessor assignment", run_script<int>(context,
		"dynamicAnswer = 7; dynamicAnswer"), 42);
	check_ex<std::runtime_error>("strict readonly accessor assignment", [&context]()
		{ run_script<int>(context, "'use strict'; dynamicAnswer = 7; dynamicAnswer"); });
	check_eq("documented metadata", documented_api.functions[0].description,
		std::string("Increment a number"));
	check_eq("documented parameter", documented_api.functions[0].call_signature.parameters[0].name,
		std::string("value"));
	check_eq("documented property", documented_api.properties[0].name, std::string("status"));
	check_eq("documented property readonly", documented_api.properties[0].readonly, true);
	check_eq("documented value readonly", documented_api.properties[1].readonly, true);
	check_eq("documented value writable", documented_api.properties[2].readonly, false);
	check_eq("documented constant readonly", documented_api.properties[3].readonly, true);
	check_eq("documented accessor readonly", documented_api.properties[4].readonly, true);
	check_eq("documented accessor writable", documented_api.properties[5].readonly, false);
	for (std::size_t index = 1; index < documented_api.properties.size(); ++index)
	{
		check_eq("module instance property metadata", documented_api.properties[index].static_, false);
	}
	check_eq("supplied module instance property metadata shape",
		metadata.global_object("suppliedInstance").properties[0].static_, false);
	check_eq("raw function metadata", raw_api.functions[0].description,
		std::string("Increment a raw object value"));
	check_eq("global metadata count", metadata.variables().size(), std::size_t{ 3 });
	check_eq("global metadata type", metadata.variables()[0].value_type.name, std::string("number"));
	check_eq("readonly global metadata", metadata.variables()[0].readonly, true);
	check_eq("writable global metadata", metadata.variables()[1].readonly, false);
	check_eq("readonly accessor metadata", metadata.variables()[2].readonly, true);

	check_eq("module.rprop",
		run_script<int>(context, "module.rprop"), 2);
	check_eq("module.wrop",
		run_script<int>(context, "++module.wprop"), 3);
	check_eq("x", x, 2);
}
