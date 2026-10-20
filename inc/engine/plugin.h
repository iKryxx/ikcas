#ifndef PLUGIN_H
#define PLUGIN_H

#include "engine/cas.h"
#include "engine/registry.h"
#include "expr/fwd.h"

#include <stdint.h>

// The plugin interface. A plugin is a descriptor whose init() registers
// types, functions, operators, literals, constants and passes with the
// registry_add_* functions. The same sources either get linked into ikcas
// (listed in plugins.conf) or built as a shared object for cas_load_plugin().
//
// Shared objects look up the host's functions (registry_*, bignum_*, ...)
// when they load, which is why ikcas links with -rdynamic.

#define IKCAS_PLUGIN_API_VERSION 1

/** Describes a plugin to the host. */
typedef struct {
    uint32_t api_version;
    const char *name;
    const char *version;
    cas_status_t (*init)(cas_t *cas);
    void (*shutdown)(cas_t *cas); // may be NULL
} plugin_desc_t;

/**
 * Declares the plugin descriptor. Linked into the binary it is called
 * ikcas_plugin_<id>. With -DIKCAS_PLUGIN_SHARED it becomes the exported
 * symbol "ikcas_plugin" that the loader looks for.
 */
#ifdef IKCAS_PLUGIN_SHARED
#define IKCAS_PLUGIN(id, ver, init_fn, shutdown_fn)                            \
    __attribute__((visibility("default"))) const plugin_desc_t ikcas_plugin = {  \
        IKCAS_PLUGIN_API_VERSION, #id, ver, init_fn, shutdown_fn                 \
    }
#else
#define IKCAS_PLUGIN(id, ver, init_fn, shutdown_fn)                            \
    const plugin_desc_t ikcas_plugin_##id = {                                    \
        IKCAS_PLUGIN_API_VERSION, #id, ver, init_fn, shutdown_fn                 \
    }
#endif

/** Runs desc->init. The descriptor has to outlive the session. */
cas_status_t cas_register_plugin(cas_t *cas, const plugin_desc_t *desc);

/** Loads a plugin shared object and runs its init. */
cas_status_t cas_load_plugin(cas_t *cas, const char *path);

/**
 * Registers every plugin listed in plugins.conf. The build generates this
 * (build/gen/builtin_plugins.c), and only the ikcas executable links it.
 */
cas_status_t cas_load_builtin(cas_t *cas);

#endif // PLUGIN_H
