#include "engine/plugin.h"

#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>

/** Remembers a plugin so it can be shut down (and dlclosed) later. */
static cas_status_t slot_add(cas_t *cas, const plugin_desc_t *desc, void *handle)
{
    if (cas->nplugins == cas->plugins_cap) {
        size_t cap = cas->plugins_cap ? cas->plugins_cap * 2 : 4;
        cas_plugin_slot_t *p = realloc(cas->plugins, cap * sizeof(*p));
        if (!p)
            return cas_error(cas, CAS_ERR_NOMEM, "out of memory");
        cas->plugins = p;
        cas->plugins_cap = cap;
    }
    cas->plugins[cas->nplugins++] = (cas_plugin_slot_t){ desc, handle };
    return CAS_OK;
}

/** Checks version and name, then runs init. Shared by the static and dlopen paths. */
static cas_status_t register_plugin(cas_t *cas, const plugin_desc_t *desc, void *handle)
{
    if (desc->api_version != IKCAS_PLUGIN_API_VERSION)
        return cas_error(cas, CAS_ERR_PLUGIN, "plugin '%s' was built for API version %u, host is %u",
                         desc->name, (unsigned)desc->api_version, (unsigned)IKCAS_PLUGIN_API_VERSION);

    for (size_t i = 0; i < cas->nplugins; i++) {
        const plugin_desc_t *d = cas->plugins[i].desc;
        if (d == desc || strcmp(d->name, desc->name) == 0)
            return cas_error(cas, CAS_ERR_PLUGIN, "plugin '%s' is already loaded", desc->name);
    }

    // Add the slot before init. If init fails halfway, the plugin still gets
    // shut down, and its .so stays mapped because the registry already points into it.
    cas_status_t st = slot_add(cas, desc, handle);
    if (st != CAS_OK)
        return st;

    cas->current_plugin = desc->name;
    st = desc->init(cas);
    cas->current_plugin = NULL;
    if (st != CAS_OK && cas->diags.len == 0)
        cas_error(cas, CAS_ERR_PLUGIN, "plugin '%s' failed to initialize", desc->name);
    if (st == CAS_OK)
        CAS_LOG(cas, LOG_INFO, "loaded plugin '%s' %s (%s)", desc->name, desc->version,
                handle ? "shared object" : "built in");
    return st;
}

/** Registers a plugin that is linked into the binary. */
cas_status_t cas_register_plugin(cas_t *cas, const plugin_desc_t *desc)
{
    return register_plugin(cas, desc, NULL);
}

/** dlopens a shared object, finds its descriptor and registers it. */
cas_status_t cas_load_plugin(cas_t *cas, const char *path)
{
    void *handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!handle)
        return cas_error(cas, CAS_ERR_PLUGIN, "cannot load '%s': %s", path, dlerror());

    CAS_LOG(cas, LOG_DEBUG, "dlopen %s", path);
    // The descriptor is data, so dlsym needs no function pointer cast.
    const plugin_desc_t *desc = dlsym(handle, "ikcas_plugin");
    if (!desc) {
        cas_error(cas, CAS_ERR_PLUGIN, "'%s' does not export ikcas_plugin", path);
        dlclose(handle);
        return CAS_ERR_PLUGIN;
    }

    size_t before = cas->nplugins;
    cas_status_t st = register_plugin(cas, desc, handle);
    if (st != CAS_OK && cas->nplugins == before)
        dlclose(handle); // rejected before it registered anything
    return st;
}
