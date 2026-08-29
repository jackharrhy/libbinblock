#include "bin_program.hpp"

#include <gdextension_interface.h>

#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>

using namespace godot;

namespace binblock_godot {

static Ref<BinProgramLoader> loader;

static void initialize(ModuleInitializationLevel level) {
  if (level != MODULE_INITIALIZATION_LEVEL_SCENE) return;
  GDREGISTER_CLASS(BinProgram);
  GDREGISTER_CLASS(BinTexture);
  GDREGISTER_CLASS(BinProgramLoader);
  loader.instantiate();
  ResourceLoader::get_singleton()->add_resource_format_loader(loader, true);
}

static void uninitialize(ModuleInitializationLevel level) {
  if (level != MODULE_INITIALIZATION_LEVEL_SCENE) return;
  if (loader.is_valid()) ResourceLoader::get_singleton()->remove_resource_format_loader(loader);
  loader.unref();
}

} // namespace binblock_godot

extern "C" GDExtensionBool GDE_EXPORT binblock_godot_init(
  GDExtensionInterfaceGetProcAddress get_proc_address,
  GDExtensionClassLibraryPtr library,
  GDExtensionInitialization *initialization
) {
  godot::GDExtensionBinding::InitObject init(get_proc_address, library, initialization);
  init.register_initializer(binblock_godot::initialize);
  init.register_terminator(binblock_godot::uninitialize);
  init.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);
  return init.init();
}
