#include "bin_program.hpp"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>

#include <cstring>
#include <string>
#include <vector>

using namespace godot;

namespace binblock_godot {

static String view_string(bb_string_view value) {
  return value.data == nullptr ? String() : String::utf8(value.data, static_cast<int64_t>(value.length));
}

static String parameter_property(bb_string_view name) {
  return String("parameters/") + view_string(name);
}

void BinProgram::clear_compilation() {
  bb_program_destroy(program);
  bb_context_destroy(context);
  program = nullptr;
  context = nullptr;
}

BinProgram::~BinProgram() {
  clear_compilation();
}

Variant BinProgram::parameter_default(const bb_program_parameter_info &parameter) const {
  switch (parameter.type) {
    case BB_SEMANTIC_BOOL: return parameter.value.boolean != 0;
    case BB_SEMANTIC_INTEGER: return parameter.value.integer;
    case BB_SEMANTIC_NUMBER:
    case BB_SEMANTIC_DEGREES:
    case BB_SEMANTIC_PERCENTAGE: return parameter.value.number;
    case BB_SEMANTIC_COLOR:
      return Color::from_rgba8(
        parameter.value.color.red,
        parameter.value.color.green,
        parameter.value.color.blue,
        parameter.value.color.alpha
      );
    default: return Variant();
  }
}

bool BinProgram::compile_source() {
  struct OwnedOverride {
    std::string name;
    bb_parameter_override value{};
  };
  std::vector<OwnedOverride> owned;
  std::vector<bb_parameter_override> overrides;
  CharString source_utf8 = source.utf8();
  bb_syntax_tree *syntax = nullptr;
  bb_source_id source_id = BB_SOURCE_ID_NONE;
  bb_compile_options options;
  bb_status status;

  if (program != nullptr) {
    const size_t count = bb_program_parameter_count(program);
    owned.reserve(count);
    for (size_t index = 0; index < count; index += 1) {
      bb_program_parameter_info parameter;
      if (bb_program_parameter(program, index, &parameter) != BB_STATUS_OK) continue;
      const StringName property(parameter_property(parameter.name));
      if (!parameter_values.has(property)) continue;
      OwnedOverride item;
      item.name.assign(parameter.name.data, parameter.name.length);
      item.value.type = parameter.type;
      const Variant value = parameter_values[property];
      if (parameter.type == BB_SEMANTIC_BOOL) item.value.value.boolean = static_cast<bool>(value);
      else if (parameter.type == BB_SEMANTIC_INTEGER) item.value.value.integer = static_cast<int64_t>(value);
      else if (parameter.type == BB_SEMANTIC_NUMBER || parameter.type == BB_SEMANTIC_DEGREES ||
               parameter.type == BB_SEMANTIC_PERCENTAGE)
        item.value.value.number = static_cast<double>(value);
      else if (parameter.type == BB_SEMANTIC_COLOR) {
        const Color color = value;
        item.value.value.color = {
          static_cast<uint8_t>(color.get_r8()),
          static_cast<uint8_t>(color.get_g8()),
          static_cast<uint8_t>(color.get_b8()),
          static_cast<uint8_t>(color.get_a8()),
        };
      } else continue;
      owned.push_back(item);
    }
  }
  overrides.reserve(owned.size());
  for (OwnedOverride &item : owned) {
    item.value.name = {item.name.data(), item.name.size()};
    overrides.push_back(item.value);
  }

  clear_compilation();
  status = bb_context_create(nullptr, &context);
  if (status == BB_STATUS_OK)
    status = bb_context_add_source(
      context,
      {"godot-resource.binscript", sizeof("godot-resource.binscript") - 1},
      {reinterpret_cast<const uint8_t *>(source_utf8.get_data()), static_cast<size_t>(source_utf8.length())},
      &source_id
    );
  if (status == BB_STATUS_OK) status = bb_syntax_parse(context, source_id, &syntax);
  bb_compile_options_init(&options);
  options.parameter_overrides = overrides.data();
  options.parameter_override_count = overrides.size();
  if (status == BB_STATUS_OK) status = bb_program_compile_with_options(context, syntax, &options, &program);
  bb_syntax_tree_destroy(syntax);
  if (status != BB_STATUS_OK) {
    clear_compilation();
    return false;
  }
  notify_property_list_changed();
  emit_changed();
  return !has_errors();
}

void BinProgram::set_source(const String &value) {
  if (source == value) return;
  source = value;
  parameter_values.clear();
  compile_source();
}

String BinProgram::get_source() const {
  return source;
}

bool BinProgram::compile() {
  return compile_source();
}

bool BinProgram::has_errors() const {
  if (program == nullptr) return true;
  for (size_t index = 0; index < bb_program_diagnostic_count(program); index += 1) {
    bb_diagnostic diagnostic;
    if (bb_program_diagnostic(program, index, &diagnostic) == BB_STATUS_OK &&
        diagnostic.severity == BB_DIAGNOSTIC_ERROR) return true;
  }
  return false;
}

Array BinProgram::get_diagnostics() const {
  Array result;
  if (program == nullptr) return result;
  for (size_t index = 0; index < bb_program_diagnostic_count(program); index += 1) {
    bb_diagnostic diagnostic;
    if (bb_program_diagnostic(program, index, &diagnostic) != BB_STATUS_OK) continue;
    Dictionary item;
    item["severity"] = static_cast<int64_t>(diagnostic.severity);
    item["code"] = static_cast<int64_t>(diagnostic.code);
    item["message"] = view_string(diagnostic.message);
    item["source_id"] = diagnostic.primary_span.source_id;
    item["byte_start"] = diagnostic.primary_span.byte_start;
    item["byte_end"] = diagnostic.primary_span.byte_end;
    result.push_back(item);
  }
  return result;
}

Array BinProgram::get_parameter_schema() const {
  Array result;
  if (program == nullptr) return result;
  for (size_t index = 0; index < bb_program_parameter_count(program); index += 1) {
    bb_program_parameter_info parameter;
    if (bb_program_parameter(program, index, &parameter) != BB_STATUS_OK) continue;
    Dictionary item;
    item["name"] = view_string(parameter.name);
    item["type"] = static_cast<int64_t>(parameter.type);
    item["value"] = parameter_default(parameter);
    item["byte_start"] = parameter.span.byte_start;
    item["byte_end"] = parameter.span.byte_end;
    result.push_back(item);
  }
  return result;
}

bool BinProgram::set_parameter(const StringName &name, const Variant &value) {
  if (program == nullptr) return false;
  const String property = String("parameters/") + String(name);
  for (size_t index = 0; index < bb_program_parameter_count(program); index += 1) {
    bb_program_parameter_info parameter;
    if (bb_program_parameter(program, index, &parameter) == BB_STATUS_OK &&
        parameter_property(parameter.name) == property) {
      parameter_values[StringName(property)] = value;
      return compile_source();
    }
  }
  return false;
}

int64_t BinProgram::get_output_count() const {
  return program == nullptr ? 0 : static_cast<int64_t>(bb_program_output_count(program));
}

Dictionary BinProgram::get_output_info(int64_t output_index) const {
  Dictionary result;
  bb_program_output_info output;
  if (program == nullptr || output_index < 0 ||
      bb_program_output(program, static_cast<size_t>(output_index), &output) != BB_STATUS_OK) return result;
  result["item_type"] = static_cast<int64_t>(output.item_type);
  result["cardinality"] = static_cast<int64_t>(output.cardinality);
  result["byte_start"] = output.span.byte_start;
  result["byte_end"] = output.span.byte_end;
  return result;
}

Ref<Image> BinProgram::render_image(int64_t output_index, int64_t item_index) {
  Ref<Image> result;
  bb_surface *surface = nullptr;
  bb_const_image_view view;
  if (program == nullptr || output_index < 0 || item_index < 0 ||
      bb_program_render_output(
        program,
        static_cast<size_t>(output_index),
        static_cast<uint64_t>(item_index),
        &surface
      ) != BB_STATUS_OK || bb_surface_get_const_view(surface, &view) != BB_STATUS_OK) {
    bb_surface_destroy(surface);
    return result;
  }
  PackedByteArray pixels;
  pixels.resize(static_cast<int64_t>(view.data_length));
  if (view.data_length != 0) std::memcpy(pixels.ptrw(), view.data, view.data_length);
  result = Image::create_from_data(
    static_cast<int32_t>(view.desc.width),
    static_cast<int32_t>(view.desc.height),
    false,
    Image::FORMAT_RGBA8,
    pixels
  );
  bb_surface_destroy(surface);
  return result;
}

Ref<ImageTexture> BinProgram::render_texture(int64_t output_index, int64_t item_index) {
  const Ref<Image> image = render_image(output_index, item_index);
  return image.is_valid() ? ImageTexture::create_from_image(image) : Ref<ImageTexture>();
}

bool BinProgram::_set(const StringName &name, const Variant &value) {
  const String property = name;
  if (!property.begins_with("parameters/")) return false;
  parameter_values[name] = value;
  compile_source();
  return true;
}

bool BinProgram::_get(const StringName &name, Variant &value) const {
  const String property = name;
  if (property == "diagnostics") {
    value = get_diagnostics();
    return true;
  }
  if (property == "output_count") {
    value = get_output_count();
    return true;
  }
  if (!property.begins_with("parameters/") || program == nullptr) return false;
  if (parameter_values.has(name)) {
    value = parameter_values[name];
    return true;
  }
  for (size_t index = 0; index < bb_program_parameter_count(program); index += 1) {
    bb_program_parameter_info parameter;
    if (bb_program_parameter(program, index, &parameter) == BB_STATUS_OK &&
        parameter_property(parameter.name) == property) {
      value = parameter_default(parameter);
      return true;
    }
  }
  return false;
}

void BinProgram::_get_property_list(List<PropertyInfo> *properties) const {
  const uint32_t read_only_usage = PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY;
  properties->push_back(PropertyInfo(Variant::ARRAY, "diagnostics", PROPERTY_HINT_NONE, "", read_only_usage));
  properties->push_back(PropertyInfo(Variant::INT, "output_count", PROPERTY_HINT_NONE, "", read_only_usage));
  if (program == nullptr) return;
  for (size_t index = 0; index < bb_program_parameter_count(program); index += 1) {
    bb_program_parameter_info parameter;
    if (bb_program_parameter(program, index, &parameter) != BB_STATUS_OK) continue;
    Variant::Type type = Variant::NIL;
    PropertyHint hint = PROPERTY_HINT_NONE;
    if (parameter.type == BB_SEMANTIC_BOOL) type = Variant::BOOL;
    else if (parameter.type == BB_SEMANTIC_INTEGER) type = Variant::INT;
    else if (parameter.type == BB_SEMANTIC_NUMBER || parameter.type == BB_SEMANTIC_DEGREES ||
             parameter.type == BB_SEMANTIC_PERCENTAGE) type = Variant::FLOAT;
    else if (parameter.type == BB_SEMANTIC_COLOR) type = Variant::COLOR;
    if (type != Variant::NIL)
      properties->push_back(PropertyInfo(type, parameter_property(parameter.name), hint));
  }
}

void BinProgram::_bind_methods() {
  ClassDB::bind_method(D_METHOD("set_source", "source"), &BinProgram::set_source);
  ClassDB::bind_method(D_METHOD("get_source"), &BinProgram::get_source);
  ClassDB::bind_method(D_METHOD("compile"), &BinProgram::compile);
  ClassDB::bind_method(D_METHOD("has_errors"), &BinProgram::has_errors);
  ClassDB::bind_method(D_METHOD("get_diagnostics"), &BinProgram::get_diagnostics);
  ClassDB::bind_method(D_METHOD("get_parameter_schema"), &BinProgram::get_parameter_schema);
  ClassDB::bind_method(D_METHOD("set_parameter", "name", "value"), &BinProgram::set_parameter);
  ClassDB::bind_method(D_METHOD("get_output_count"), &BinProgram::get_output_count);
  ClassDB::bind_method(D_METHOD("get_output_info", "output_index"), &BinProgram::get_output_info);
  ClassDB::bind_method(
    D_METHOD("render_image", "output_index", "item_index"),
    &BinProgram::render_image,
    DEFVAL(0),
    DEFVAL(0)
  );
  ClassDB::bind_method(
    D_METHOD("render_texture", "output_index", "item_index"),
    &BinProgram::render_texture,
    DEFVAL(0),
    DEFVAL(0)
  );
  ADD_PROPERTY(PropertyInfo(Variant::STRING, "source", PROPERTY_HINT_MULTILINE_TEXT), "set_source", "get_source");
}

void BinTexture::set_program(const Ref<BinProgram> &value) {
  program = value;
  emit_changed();
}

Ref<BinProgram> BinTexture::get_program() const {
  return program;
}

void BinTexture::set_output_index(int64_t value) {
  output_index = value < 0 ? 0 : value;
  emit_changed();
}

int64_t BinTexture::get_output_index() const {
  return output_index;
}

void BinTexture::set_item_index(int64_t value) {
  item_index = value < 0 ? 0 : value;
  emit_changed();
}

int64_t BinTexture::get_item_index() const {
  return item_index;
}

Ref<ImageTexture> BinTexture::render() {
  return program.is_valid() ? program->render_texture(output_index, item_index) : Ref<ImageTexture>();
}

void BinTexture::_bind_methods() {
  ClassDB::bind_method(D_METHOD("set_program", "program"), &BinTexture::set_program);
  ClassDB::bind_method(D_METHOD("get_program"), &BinTexture::get_program);
  ClassDB::bind_method(D_METHOD("set_output_index", "output_index"), &BinTexture::set_output_index);
  ClassDB::bind_method(D_METHOD("get_output_index"), &BinTexture::get_output_index);
  ClassDB::bind_method(D_METHOD("set_item_index", "item_index"), &BinTexture::set_item_index);
  ClassDB::bind_method(D_METHOD("get_item_index"), &BinTexture::get_item_index);
  ClassDB::bind_method(D_METHOD("render"), &BinTexture::render);
  ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "program", PROPERTY_HINT_RESOURCE_TYPE, "BinProgram"), "set_program", "get_program");
  ADD_PROPERTY(PropertyInfo(Variant::INT, "output_index", PROPERTY_HINT_RANGE, "0,2147483647,1"), "set_output_index", "get_output_index");
  ADD_PROPERTY(PropertyInfo(Variant::INT, "item_index", PROPERTY_HINT_RANGE, "0,2147483647,1"), "set_item_index", "get_item_index");
}

PackedStringArray BinProgramLoader::_get_recognized_extensions() const {
  PackedStringArray extensions;
  extensions.push_back("binscript");
  extensions.push_back("bbm");
  return extensions;
}

bool BinProgramLoader::_handles_type(const StringName &type) const {
  return type == StringName("BinProgram") || type == StringName("Resource");
}

String BinProgramLoader::_get_resource_type(const String &path) const {
  const String extension = path.get_extension().to_lower();
  return extension == "binscript" ? String("BinProgram") : String();
}

Variant BinProgramLoader::_load(
  const String &path,
  const String &original_path,
  bool use_sub_threads,
  int32_t cache_mode
) const {
  (void)original_path;
  (void)use_sub_threads;
  (void)cache_mode;
  const String loaded_source = FileAccess::get_file_as_string(path);
  Ref<BinProgram> result;
  result.instantiate();
  result->set_source(loaded_source);
  result->set_path(path);
  return result;
}

} // namespace binblock_godot
