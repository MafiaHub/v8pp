#include "v8pp/metadata.hpp"

#include "test.hpp"

#include <optional>
#include <string>

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

} // namespace

void test_metadata()
{
	v8pp::metadata::registry registry;
	auto& camera = registry.global_object("Camera", "Client camera helpers");
	check_eq("metadata symbol deduplication", &registry.global_object("Camera") == &camera, true);
	registry.constructor("Player", "A wrapped player class");
	registry.data_type("ScreenPosition", "A projection result");

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

	auto explicit_signature = v8pp::metadata::signature_of<screen_position, double, double, double>(
		{ { .name = "x" }, { .name = "y" }, { .name = "z" } }, "Screen coordinates");
	check_eq("explicit signature parameter", explicit_signature.parameters[2].name, std::string("z"));
	check_eq("explicit signature result", explicit_signature.return_type.cpp_name.find("screen_position") != std::string::npos, true);
	auto const json = v8pp::metadata::to_json(registry);
	check_eq("metadata json schema", json.find("\"schemaVersion\":2") != std::string::npos, true);
	check_eq("metadata json contains runtime shape", json.find("\"kind\":\"globalObject\"") != std::string::npos, true);
	check_eq("metadata json contains constructor", json.find("\"kind\":\"constructor\"") != std::string::npos, true);
	check_eq("metadata json contains data type", json.find("\"kind\":\"dataType\"") != std::string::npos, true);
	check_eq("metadata json contains symbol", json.find("\"name\":\"Camera\"") != std::string::npos, true);
	check_eq("metadata json contains property", json.find("\"name\":\"active\"") != std::string::npos, true);
	check_eq("metadata json contains variable", json.find("\"name\":\"MainCamera\"") != std::string::npos, true);
}
