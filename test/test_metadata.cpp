#include "v8pp/metadata.hpp"

#include "test.hpp"

#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct screen_position
{
	double x;
	double y;
	bool visible;
};

[[maybe_unused]] std::optional<std::string> lookup(int id, bool cached)
{
	if (cached) return std::to_string(id);
	return std::nullopt;
}

struct metadata_target
{
	int member(double, std::string const&) const { return 0; }
};

void set_environment(char const* name, char const* value)
{
#ifdef _WIN32
	_putenv_s(name, value ? value : "");
#else
	if (value) setenv(name, value, 1);
	else unsetenv(name);
#endif
}

} // namespace

void test_metadata()
{
	v8pp::metadata::registry registry;
	auto& camera = registry.global_object("Camera", "Client camera helpers");
	check_eq("metadata symbol deduplication", &registry.global_object("Camera") == &camera, true);
	auto& player = registry.constructor("Player", "A wrapped player class");
	player.constructor = v8pp::metadata::function_of<void (*)(uint64_t)>("constructor",
		{ .description = "Wraps an existing player",
			.parameters = { { .name = "id", .description = "Network entity identifier" } } });
	registry.data_type("ScreenPosition", "A projection result");
	check_ex<std::invalid_argument>("metadata symbol kind mismatch", [&registry]()
		{ registry.constructor("Camera"); });

	v8pp::metadata::function_options options;
	options.description = "Looks up a projected position";
	options.parameters = {
		{ .name = "id", .description = "Entity identifier" },
		{ .name = "cached", .description = "Use the cached position" }
	};
	options.return_description = "The projected position when available";
	camera.add_function<decltype(&lookup)>("lookup", options);
	camera.add_property("active", v8pp::metadata::type_of<bool>(), "Whether a camera is active", true);
	registry.variable_("MainCamera", { "Camera | null", {}, false }, "The active camera");

	check_eq("metadata symbol count", registry.symbols().size(), std::size_t{ 3 });
	check_eq("metadata symbol name", camera.name, std::string("Camera"));
	check_eq("metadata function name", camera.functions[0].name, std::string("lookup"));
	check_eq("metadata first parameter", camera.functions[0].call_signature.parameters[0].name, std::string("id"));
	check_eq("metadata number mapping", camera.functions[0].call_signature.parameters[0].value_type.name, std::string("number"));
	check_eq("metadata boolean mapping", camera.functions[0].call_signature.parameters[1].value_type.name, std::string("boolean"));
	check_eq("metadata optional result", camera.functions[0].call_signature.return_type.optional, true);
	check_eq("metadata property type", camera.properties[0].value_type.name, std::string("boolean"));
	check_eq("metadata property readonly", camera.properties[0].readonly, true);
	check_eq("metadata variable count", registry.variables().size(), std::size_t{ 1 });
	check_eq("metadata constructor parameter", player.constructor->call_signature.parameters[0].name,
		std::string("id"));
	check_eq("metadata constructor description", player.constructor->description,
		std::string("Wraps an existing player"));

	auto& updated_camera = registry.global_object("Camera", "Updated camera description");
	check_eq("metadata description replacement", updated_camera.description,
		std::string("Updated camera description"));
	updated_camera.add_function<decltype(&lookup)>("lookup", { .description = "Updated lookup" });
	check_eq("metadata function deduplication", updated_camera.functions.size(), std::size_t{ 1 });
	check_eq("metadata function replacement", updated_camera.functions[0].description,
		std::string("Updated lookup"));
	updated_camera.record(v8pp::metadata::function_of<decltype(&lookup)>("lookup", {}, true));
	check_eq("metadata static function overload", updated_camera.functions.size(), std::size_t{ 2 });
	check_eq("metadata static function marker", updated_camera.functions[1].static_, true);

	updated_camera.add_property("active", v8pp::metadata::type_of<std::string>(), "Updated property");
	check_eq("metadata property deduplication", updated_camera.properties.size(), std::size_t{ 1 });
	check_eq("metadata property replacement", updated_camera.properties[0].value_type.name,
		std::string("string"));
	updated_camera.add_property("active", v8pp::metadata::type_of<bool>(), "Static property", true, true);
	check_eq("metadata static property overload", updated_camera.properties.size(), std::size_t{ 2 });
	check_eq("metadata static property marker", updated_camera.properties[1].static_, true);
	updated_camera.add_property("zoom", "number", "Camera zoom");
	check_eq("metadata string property shorthand", updated_camera.properties[2].value_type.name,
		std::string("number"));

	registry.variable_("MainCamera", { "Camera", {}, false }, "Updated variable", false);
	check_eq("metadata variable deduplication", registry.variables().size(), std::size_t{ 1 });
	check_eq("metadata variable replacement", registry.variables()[0].description,
		std::string("Updated variable"));
	check_eq("metadata variable writable", registry.variables()[0].readonly, false);

	auto explicit_signature = v8pp::metadata::signature_of<screen_position, double, double, double>(
		{ { .name = "x" }, { .name = "y" }, { .name = "z" } }, "Screen coordinates");
	check_eq("explicit signature parameter", explicit_signature.parameters[2].name, std::string("z"));
	check_eq("explicit signature result", explicit_signature.return_type.cpp_name.find("screen_position") != std::string::npos, true);

	check_eq("void type mapping", v8pp::metadata::type_of<void>().name, std::string("void"));
	check_eq("string view type mapping", v8pp::metadata::type_of<std::string_view>().name,
		std::string("string"));
	check_eq("C string type mapping", v8pp::metadata::type_of<char const*>().name,
		std::string("string"));
	check_eq("optional type mapping", v8pp::metadata::type_of<std::optional<int>>().optional, true);

	auto member_signature = v8pp::metadata::detail::infer_signature<decltype(&metadata_target::member)>();
	check_eq("member signature hides receiver", member_signature.parameters.size(), std::size_t{ 2 });
	check_eq("member signature return", member_signature.return_type.name, std::string("number"));

	auto documentation = v8pp::metadata::docs("Promise<string>",
		{ v8pp::metadata::param("id", "number", false, "Entity identifier"),
			v8pp::metadata::param("cached", "boolean", true, "Allow cached data") },
		"Looks up an entity", "The entity name", "1.5.0", "Use find instead",
		{ "lookup(1)", "lookup(1, true)" });
	auto documented_function = v8pp::metadata::function_of<decltype(&lookup)>("lookup", documentation, true);
	check_eq("docs return override", documented_function.call_signature.return_type.name,
		std::string("Promise<string>"));
	check_eq("docs optional parameter", documented_function.call_signature.parameters[1].value_type.optional, true);
	check_eq("docs parameter description", documented_function.call_signature.parameters[0].description,
		std::string("Entity identifier"));
	check_eq("docs function description", documented_function.description, std::string("Looks up an entity"));
	check_eq("docs return description", documented_function.call_signature.return_description,
		std::string("The entity name"));
	check_eq("docs since", documented_function.since, std::string("1.5.0"));
	check_eq("docs deprecated", documented_function.deprecated, std::string("Use find instead"));
	check_eq("docs examples", documented_function.examples.size(), std::size_t{ 2 });
	check_eq("docs static marker", documented_function.static_, true);
	updated_camera.record(documented_function);

	auto property_documentation = v8pp::metadata::property_docs("Vector3", "World position");
	check_eq("property docs type", property_documentation.type, std::string("Vector3"));
	check_eq("property docs description", property_documentation.description, std::string("World position"));

	auto& first_catalog = v8pp::metadata::catalog("v8pp-test-metadata");
	auto& same_catalog = v8pp::metadata::catalog("v8pp-test-metadata");
	auto& other_catalog = v8pp::metadata::catalog("v8pp-test-metadata-other");
	check_eq("named catalog identity", &first_catalog == &same_catalog, true);
	check_eq("named catalog isolation", &first_catalog != &other_catalog, true);

	updated_camera.description = "quotes: \" slash: \\ newline:\n carriage:\r tab:\t control:\x01";
	updated_camera.bases = { "BaseEntity" };
	auto const json = v8pp::metadata::to_json(registry);
	check_eq("metadata json schema", json.find("\"schemaVersion\":2") != std::string::npos, true);
	check_eq("metadata json contains runtime shape", json.find("\"kind\":\"globalObject\"") != std::string::npos, true);
	check_eq("metadata json contains constructor", json.find("\"kind\":\"constructor\"") != std::string::npos, true);
	check_eq("metadata json contains constructor signature",
		json.find("\"constructor\":{\"name\":\"constructor\",\"description\":\"Wraps an existing player\"") != std::string::npos, true);
	check_eq("metadata json contains data type", json.find("\"kind\":\"dataType\"") != std::string::npos, true);
	check_eq("metadata json contains symbol", json.find("\"name\":\"Camera\"") != std::string::npos, true);
	check_eq("metadata json contains property", json.find("\"name\":\"active\"") != std::string::npos, true);
	check_eq("metadata json contains variable", json.find("\"name\":\"MainCamera\"") != std::string::npos, true);
	check_eq("metadata json escapes content",
		json.find(R"(quotes: \" slash: \\ newline:\n carriage:\r tab:\t control:\u0001)") != std::string::npos, true);
	check_eq("metadata JSON contains examples",
		json.find("\"examples\":[\"lookup(1)\",\"lookup(1, true)\"]") != std::string::npos, true);
	check_eq("metadata json contains base", json.find("\"bases\":[\"BaseEntity\"]") != std::string::npos, true);
	check_eq("metadata json contains static", json.find("\"static\":true") != std::string::npos, true);
	check_eq("metadata json contains writable variable", json.find("\"readonly\":false") != std::string::npos, true);

	std::vector<v8pp::metadata::symbol> vector_symbols{ updated_camera };
	std::ostringstream vector_json;
	v8pp::metadata::write_json(vector_json, vector_symbols);
	check_eq("vector JSON writer", vector_json.str().find("\"schemaVersion\":2") != std::string::npos, true);
	std::deque<v8pp::metadata::symbol> deque_symbols{ updated_camera };
	std::ostringstream deque_json;
	v8pp::metadata::write_json(deque_json, deque_symbols);
	check_eq("deque JSON writer", deque_json.str().find("\"name\":\"Camera\"") != std::string::npos, true);

	auto const output_path = std::filesystem::temp_directory_path() / "v8pp-metadata-test.json";
	std::filesystem::remove(output_path);
	check_eq("metadata file export", v8pp::metadata::write_json_file(registry, output_path.string()), true);
	std::ifstream input(output_path, std::ios::binary);
	std::string file_json{ std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
	check_eq("metadata file contents", file_json, json + "\n");
	std::filesystem::remove(output_path);
	check_eq("metadata invalid file export",
		v8pp::metadata::write_json_file(registry,
			(output_path / "missing" / "metadata.json").string()), false);

	constexpr char environment_name[] = "V8PP_TEST_METADATA_PATH";
	set_environment(environment_name, nullptr);
	check_eq("disabled environment export",
		v8pp::metadata::export_catalog_from_environment("v8pp-test-metadata", environment_name), true);
	set_environment(environment_name, output_path.string().c_str());
	first_catalog.global_object("EnvironmentExport");
	check_eq("environment file export",
		v8pp::metadata::export_catalog_from_environment("v8pp-test-metadata", environment_name), true);
	check_eq("environment file exists", std::filesystem::exists(output_path), true);
	set_environment(environment_name, nullptr);
	std::filesystem::remove(output_path);
}
