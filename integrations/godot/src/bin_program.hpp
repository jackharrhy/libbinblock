#ifndef BINBLOCK_GODOT_BIN_PROGRAM_HPP
#define BINBLOCK_GODOT_BIN_PROGRAM_HPP

#include <binblock/program.h>

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/resource_format_loader.hpp>
#include <godot_cpp/templates/list.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

namespace binblock_godot {

class BinProgram : public godot::Resource {
  GDCLASS(BinProgram, godot::Resource)

  godot::String source;
  godot::Dictionary parameter_values;
  bb_context *context = nullptr;
  bb_program *program = nullptr;

  void clear_compilation();
  bool compile_source();
  godot::Variant parameter_default(const bb_program_parameter_info &parameter) const;

protected:
  static void _bind_methods();
  bool _set(const godot::StringName &name, const godot::Variant &value);
  bool _get(const godot::StringName &name, godot::Variant &value) const;
  void _get_property_list(godot::List<godot::PropertyInfo> *properties) const;

public:
  ~BinProgram() override;

  void set_source(const godot::String &value);
  godot::String get_source() const;
  bool compile();
  bool has_errors() const;
  godot::Array get_diagnostics() const;
  godot::Array get_parameter_schema() const;
  bool set_parameter(const godot::StringName &name, const godot::Variant &value);
  int64_t get_output_count() const;
  godot::Dictionary get_output_info(int64_t output_index) const;
  godot::Ref<godot::Image> render_image(int64_t output_index = 0, int64_t item_index = 0);
  godot::Ref<godot::ImageTexture> render_texture(int64_t output_index = 0, int64_t item_index = 0);
};

class BinTexture : public godot::Resource {
  GDCLASS(BinTexture, godot::Resource)

  godot::Ref<BinProgram> program;
  int64_t output_index = 0;
  int64_t item_index = 0;

protected:
  static void _bind_methods();

public:
  void set_program(const godot::Ref<BinProgram> &value);
  godot::Ref<BinProgram> get_program() const;
  void set_output_index(int64_t value);
  int64_t get_output_index() const;
  void set_item_index(int64_t value);
  int64_t get_item_index() const;
  godot::Ref<godot::ImageTexture> render();
};

class BinProgramLoader : public godot::ResourceFormatLoader {
  GDCLASS(BinProgramLoader, godot::ResourceFormatLoader)

protected:
  static void _bind_methods() {}

public:
  godot::PackedStringArray _get_recognized_extensions() const override;
  bool _handles_type(const godot::StringName &type) const override;
  godot::String _get_resource_type(const godot::String &path) const override;
  godot::Variant _load(
    const godot::String &path,
    const godot::String &original_path,
    bool use_sub_threads,
    int32_t cache_mode
  ) const override;
};

} // namespace binblock_godot

#endif
