#pragma once
/*
 * DSLoader plugin API — stable C ABI between DSLoader (version.dll) and plugins in dscoop/plugins/.
 *
 * A plugin exports three functions:
 *   int  DSPlugin_Query(DSPluginInfo* info);      // fill name/version/api_version, return 1 to be loaded
 *   int  DSPlugin_Load(const DSLoaderApi* api);   // return 1 on success
 *   void DSPlugin_Unload(void);
 */
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DSLOADER_API_VERSION 1u

#ifdef _WIN32
#define DSPLUGIN_EXPORT __declspec(dllexport)
#else
#define DSPLUGIN_EXPORT __attribute__((visibility("default")))
#endif

typedef enum DSLogLevel { DSLOG_TRACE = 0, DSLOG_DEBUG = 1, DSLOG_INFO = 2, DSLOG_WARN = 3, DSLOG_ERROR = 4 } DSLogLevel;

typedef struct DSPluginInfo {
    uint32_t struct_size;      /* sizeof(DSPluginInfo), set by loader before Query */
    uint32_t api_version;      /* plugin fills: DSLOADER_API_VERSION it was built against */
    const char* name;          /* plugin fills, static storage */
    const char* version;       /* plugin fills, static storage */
} DSPluginInfo;

typedef struct DSGameModule {
    void* base;                /* image base of the game exe */
    size_t size;               /* SizeOfImage */
    uint32_t build_id;         /* fnv1a(TimeDateStamp, SizeOfImage, CheckSum) — same value both peers compare in Hello */
    uint32_t timestamp;        /* PE TimeDateStamp */
    const char* exe_path_utf8; /* full path of the game exe */
    void* text_begin; size_t text_size;   /* .text */
    void* data_begin; size_t data_size;   /* .data */
    void* rdata_begin; size_t rdata_size; /* .rdata */
} DSGameModule;

typedef struct DSLoaderApi {
    uint32_t struct_size;
    uint32_t api_version;
    const char* loader_version;        /* "0.1.0" */
    const char* root_dir_utf8;         /* <game>/dscoop */
    const DSGameModule* game;

    /* Logging goes to dscoop/logs/dsloader.log with the plugin name as prefix. */
    void (*log)(DSLogLevel level, const char* plugin, const char* message);

    /* Address of a signature from data/offsets.json by name, or NULL when unresolved. */
    void* (*resolve)(const char* name);
    /* Raw text of offsets.json so plugins can read their own sections ("pointers", ...). */
    const char* (*offsets_json)(void);

    /* MinHook wrappers. Return 0 on success, MH_STATUS otherwise. */
    int (*hook_create)(void* target, void* detour, void** original);
    int (*hook_enable)(void* target);
    int (*hook_disable)(void* target);
    int (*hook_remove)(void* target);

    /* Window of the game once detected (HWND), or NULL. */
    void* (*game_window)(void);
    /* Safe memory read within this process (returns 0 on an unmapped/unreadable range). */
    int (*safe_read)(const void* src, void* dst, size_t size);
    /* Copies logs + configs + offsets + dumps into dscoop/reports/report_<ts>/; returns path or NULL. */
    const char* (*collect_report)(void);

    void* reserved[8];
} DSLoaderApi;

typedef int (*DSPlugin_QueryFn)(DSPluginInfo*);
typedef int (*DSPlugin_LoadFn)(const DSLoaderApi*);
typedef void (*DSPlugin_UnloadFn)(void);

#ifdef __cplusplus
}
#endif
