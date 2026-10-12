// Armada second-screen provider for Chained Echoes (native Linux, IL2CPP): a preloaded
// library that runs on the game's main thread and sends its state to the HTML view.
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
// run on the oldest C library a game runtime may carry
__asm__(".symver dlopen,dlopen@GLIBC_2.2.5");
__asm__(".symver dlsym,dlsym@GLIBC_2.2.5");
__asm__(".symver pthread_create,pthread_create@GLIBC_2.2.5");

#define HELLO "{\"game\":\"Chained Echoes\",\"appid\":1229240,\"provider\":\"chained-echoes-party\",\"view\":\"html\",\"version\":1}"

static char logpath[600];
static void say(const char *fmt, ...) {
    FILE *f = logpath[0] ? fopen(logpath, "a") : NULL; if (!f) return;
    va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a); fputc('\n', f); fclose(f);
}

// ---- IL2CPP API ----
static void *(*il2cpp_domain_get)(void);
static void *(*il2cpp_thread_attach)(void *);
static void **(*il2cpp_domain_get_assemblies)(void *, size_t *);
static void *(*il2cpp_assembly_get_image)(void *);
static const char *(*il2cpp_image_get_name)(void *);
static void *(*il2cpp_class_from_name)(void *, const char *, const char *);
static void *(*il2cpp_class_get_field_from_name)(void *, const char *);
static void *(*il2cpp_class_get_method_from_name)(void *, const char *, int);
static void (*il2cpp_field_static_get_value)(void *, void *);
static size_t (*il2cpp_field_get_offset)(void *);
static void *(*il2cpp_object_get_class)(void *);
static void *(*il2cpp_runtime_invoke)(void *, void *, void **, void **);
static void (*il2cpp_runtime_class_init)(void *);
static void *(*il2cpp_class_get_fields)(void *, void **);
static const char *(*il2cpp_field_get_name)(void *);
static void *(*il2cpp_field_get_type)(void *);
static int (*il2cpp_field_get_flags)(void *);
static int (*il2cpp_type_get_type)(void *);
static char *(*il2cpp_type_get_name)(void *);
static const char *(*il2cpp_class_get_name)(void *);
static void *(*il2cpp_class_get_parent)(void *);
static void *(*il2cpp_method_get_return_type)(void *);
static void *(*il2cpp_object_unbox)(void *);
static void *(*il2cpp_class_get_methods)(void *, void **);
static const char *(*il2cpp_method_get_name)(void *);
static unsigned (*il2cpp_method_get_param_count)(void *);
static void *(*il2cpp_class_get_element_class)(void *);
static unsigned char (*il2cpp_class_is_valuetype)(void *);
static int (*il2cpp_class_array_element_size)(void *);
static void *(*il2cpp_class_get_type)(void *);
static void *(*il2cpp_type_get_object)(void *);
static void *(*il2cpp_method_get_param)(void *, unsigned);
static void *(*il2cpp_object_new)(void *);
static void *(*il2cpp_string_new)(const char *);
static uint32_t (*il2cpp_gchandle_new)(void *, int);
static void *(*il2cpp_gchandle_get_target)(uint32_t);
static void (*il2cpp_gchandle_free)(uint32_t);
static void *images[256]; static int nimages;
static void *image;

static int resolve(void *lib) {
#define R(n) if (!(n = dlsym(lib, #n))) { say("missing " #n); return 0; }
    R(il2cpp_domain_get) R(il2cpp_thread_attach) R(il2cpp_domain_get_assemblies) R(il2cpp_assembly_get_image)
    R(il2cpp_image_get_name) R(il2cpp_class_from_name) R(il2cpp_class_get_field_from_name)
    R(il2cpp_class_get_method_from_name) R(il2cpp_field_static_get_value) R(il2cpp_field_get_offset)
    R(il2cpp_object_get_class) R(il2cpp_runtime_invoke) R(il2cpp_runtime_class_init)
    R(il2cpp_class_get_fields) R(il2cpp_field_get_name) R(il2cpp_field_get_type) R(il2cpp_field_get_flags)
    R(il2cpp_type_get_type) R(il2cpp_type_get_name) R(il2cpp_class_get_name) R(il2cpp_class_get_parent)
    R(il2cpp_method_get_return_type) R(il2cpp_object_unbox) R(il2cpp_class_get_methods) R(il2cpp_method_get_name)
    R(il2cpp_method_get_param_count) R(il2cpp_class_get_element_class) R(il2cpp_class_is_valuetype) R(il2cpp_class_array_element_size)
    R(il2cpp_class_get_type) R(il2cpp_type_get_object) R(il2cpp_method_get_param) R(il2cpp_object_new) R(il2cpp_string_new) R(il2cpp_gchandle_new) R(il2cpp_gchandle_get_target) R(il2cpp_gchandle_free)
#undef R
    return 1;
}

static void *klass(const char *name) {
    void *k = il2cpp_class_from_name(image, "", name);
    if (k) il2cpp_runtime_class_init(k);
    return k;
}

// Field offsets by (class, name), looked up once.
static struct { void *k; const char *name; long off; } cache[256];
static int ncache;
static long offset(void *obj, const char *name) {
    void *k = il2cpp_object_get_class(obj);
    for (int i = 0; i < ncache; i++) if (cache[i].k == k && cache[i].name == name) return cache[i].off;
    void *f = il2cpp_class_get_field_from_name(k, name);
    long off = f ? (long)il2cpp_field_get_offset(f) : -1;
    if (ncache < 256) { cache[ncache].k = k; cache[ncache].name = name; cache[ncache++].off = off; }
    if (off < 0) say("no field %s", name);
    return off;
}
static int geti(void *obj, const char *name) { long o = offset(obj, name); return o < 0 ? 0 : *(int *)((char *)obj + o); }
static int getb(void *obj, const char *name) { long o = offset(obj, name); return o < 0 ? 0 : *(unsigned char *)((char *)obj + o); }
static void *getp(void *obj, const char *name) { long o = offset(obj, name); return o < 0 ? NULL : *(void **)((char *)obj + o); }

static int sgeti(void *k, const char *name) { int v = 0; void *f = il2cpp_class_get_field_from_name(k, name); if (f) il2cpp_field_static_get_value(f, &v); return v; }
static void *sgetp(void *k, const char *name) { void *v = NULL; void *f = il2cpp_class_get_field_from_name(k, name); if (f) il2cpp_field_static_get_value(f, &v); return v; }

// A List<T> of references: element count and item i.
static int list_len(void *list) { return list ? geti(list, "_size") : 0; }
static void *list_at(void *list, int i) { void *items = getp(list, "_items"); return items ? ((void **)((char *)items + 32))[i] : NULL; }

static void *call(void *k, const char *method) {
    void *m = il2cpp_class_get_method_from_name(k, method, 0), *exc = NULL;
    if (!m) return NULL;
    void *r = il2cpp_runtime_invoke(m, NULL, NULL, &exc);
    return exc ? NULL : r;
}

// A class by name from any assembly, trying the common namespaces.
static void *anyclass(const char *name) {
    static const char *ns[] = { "", "UnityEngine", "UnityEngine.UI", "TMPro", "System", "Com.LuisPedroFonseca.ProCamera2D" };
    for (int n = 0; n < 6; n++) for (int i = 0; i < nimages; i++) {
        void *k = il2cpp_class_from_name(images[i], ns[n], name);
        if (k) { il2cpp_runtime_class_init(k); return k; }
    }
    return NULL;
}
static void *invoke(void *k, void *obj, const char *method, int argc, void **args) {
    void *m = il2cpp_class_get_method_from_name(k, method, argc), *exc = NULL;
    if (!m) return NULL;
    void *r = il2cpp_runtime_invoke(m, obj, args, &exc);
    return exc ? NULL : r;
}
// A method by name and the type names of its parameters ("System.Type,System.Boolean"), for overloads.
static void *method_sig(void *k, const char *name, const char *sig) {
    for (; k; k = il2cpp_class_get_parent(k)) {
        void *it = NULL, *m;
        while ((m = il2cpp_class_get_methods(k, &it))) {
            if (strcmp(il2cpp_method_get_name(m), name)) continue;
            char have[512] = ""; unsigned n = il2cpp_method_get_param_count(m);
            for (unsigned i = 0; i < n; i++) { char *tn = il2cpp_type_get_name(il2cpp_method_get_param(m, i)); if (i) strncat(have, ",", 511 - strlen(have)); strncat(have, tn, 511 - strlen(have)); }
            if (!strcmp(have, sig)) return m;
        }
    }
    return NULL;
}
static void *invoke_sig(void *k, void *obj, const char *name, const char *sig, void **args) {
    void *m = method_sig(k, name, sig), *exc = NULL;
    if (!m) { say("no method %s(%s)", name, sig); return NULL; }
    void *r = il2cpp_runtime_invoke(m, obj, args, &exc);
    return exc ? NULL : r;
}
static void *typeobj(void *k) { return il2cpp_type_get_object(il2cpp_class_get_type(k)); }

// ---- JSON output ----
static char *out; static size_t len, cap;
static void put(const char *s, size_t n) {
    if (len + n + 1 > cap) { cap = (cap + n) * 2 + 4096; out = realloc(out, cap); }
    memcpy(out + len, s, n); len += n; out[len] = 0;
}
static void puts_(const char *s) { put(s, strlen(s)); }
static void putf(const char *fmt, ...) { char b[256]; va_list a; va_start(a, fmt); int n = vsnprintf(b, sizeof b, fmt, a); va_end(a); put(b, n); }
// A managed string as a JSON string.
static void putstr(void *s) {
    puts_("\"");
    if (s) {
        int n = *(int *)((char *)s + 16); uint16_t *c = (uint16_t *)((char *)s + 20);
        for (int i = 0; i < n && i < 2000; i++) {
            unsigned u = c[i]; char b[8];
            if (u == '"' || u == '\\') { b[0] = '\\'; b[1] = u; put(b, 2); }
            else if (u < 32) put(" ", 1);
            else if (u < 0x80) { b[0] = u; put(b, 1); }
            else if (u < 0x800) { b[0] = 0xC0 | u >> 6; b[1] = 0x80 | (u & 63); put(b, 2); }
            else if (u >= 0xD800 && u < 0xE000) put("?", 1);
            else { b[0] = 0xE0 | u >> 12; b[1] = 0x80 | ((u >> 6) & 63); b[2] = 0x80 | (u & 63); put(b, 3); }
        }
    }
    puts_("\"");
}
static void key(const char *k) { putf("\"%s\":", k); }
static void kint(const char *k, int v) { putf("\"%s\":%d,", k, v); }
static void kstr(const char *k, void *s) { key(k); putstr(s); puts_(","); }
static void close_obj(void) { if (out[len - 1] == ',') len--; puts_("}"); }
static void close_arr(void) { if (out[len - 1] == ',') len--; puts_("]"); }

// ---- game state ----
static void *k_gm, *k_db, *k_data, *k_obj, *k_go, *k_tr, *k_comp, *k_cam, *k_rt, *k_tex, *k_sr;
static char datadir[512];

static int unbox_int(void *o) { return o ? *(int *)il2cpp_object_unbox(o) : 0; }
static int alive(void *o) { return o && *(void **)((char *)o + 16); }
static void *get0(void *obj, const char *method) { return invoke(il2cpp_object_get_class(obj), obj, method, 0, NULL); }
static void vec3(void *obj, const char *method, float *out) {
    void *r = get0(obj, method);
    if (r) memcpy(out, il2cpp_object_unbox(r), 12); else memset(out, 0, 12);
}
static void *component(void *go, void *k) { void *a[1] = { typeobj(k) }; return invoke_sig(il2cpp_object_get_class(go), go, "GetComponentInChildren", "System.Type", a); }
static void set(void *obj, const char *method, void *arg) { void *a[1] = { arg }; invoke(il2cpp_object_get_class(obj), obj, method, 1, a); }

static void *find_by_id(void *list, const char *idField, int id) {
    for (int i = 0, n = list_len(list); i < n; i++) { void *o = list_at(list, i); if (o && geti(o, idField) == id) return o; }
    return NULL;
}

// Saves a render texture region as a PNG in the mod's data folder, where the view reads it.
static int save_png(void *rt, int x, int y, int w, int h, const char *name) {
    if (!rt) return 0;
    void *prev = invoke(k_rt, NULL, "get_active", 0, NULL);
    void *a1[1] = { rt }; invoke(k_rt, NULL, "set_active", 1, a1);
    void *tex = il2cpp_object_new(k_tex); int fmt = 4; unsigned char mips = 0;
    void *ca[4] = { &w, &h, &fmt, &mips };
    invoke_sig(k_tex, tex, ".ctor", "System.Int32,System.Int32,UnityEngine.TextureFormat,System.Boolean", ca);
    float rect[4] = { x, y, w, h }; int zero = 0;
    void *ra[3] = { rect, &zero, &zero };
    invoke_sig(k_tex, tex, "ReadPixels", "UnityEngine.Rect,System.Int32,System.Int32", ra);
    void *ea[1] = { tex };
    void *png = invoke_sig(anyclass("ImageConversion"), NULL, "EncodeToPNG", "UnityEngine.Texture2D", ea);
    void *pa[1] = { prev }; invoke(k_rt, NULL, "set_active", 1, pa);
    void *da[1] = { tex }; invoke_sig(k_obj, NULL, "Destroy", "UnityEngine.Object", da);
    if (!png) return 0;
    char path[700], tmp[720]; snprintf(path, sizeof path, "%s/art/%s", datadir, name); snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb"); if (!f) return 0;
    fwrite((char *)png + 32, 1, *(long *)((char *)png + 24), f); fclose(f);
    return rename(tmp, path) == 0;
}
static void *temp_rt(int w, int h, int depth) {
    int fmt = 0;
    void *a[4] = { &w, &h, &depth, &fmt };
    return invoke_sig(k_rt, NULL, "GetTemporary", "System.Int32,System.Int32,System.Int32,UnityEngine.RenderTextureFormat", a);
}
static void release_rt(void *rt) { void *a[1] = { rt }; invoke(k_rt, NULL, "ReleaseTemporary", 1, a); }

// One sprite cut out of its texture, saved once per session.
static int save_sprite(void *sprite, const char *name) {
    void *tex = get0(sprite, "get_texture"), *rb = get0(sprite, "get_textureRect");
    if (!tex || !rb) { say("sprite %s: no texture", name); return 0; }
    float *r = il2cpp_object_unbox(rb); int w = r[2], h = r[3];
    float tw = unbox_int(get0(tex, "get_width")), th = unbox_int(get0(tex, "get_height"));
    if (w < 1 || h < 1 || tw < 1 || th < 1) { say("sprite %s: %d %d %g %g", name, w, h, tw, th); return 0; }
    void *rt = temp_rt(tw, th, 0);
    void *a[2] = { tex, rt };
    invoke_sig(anyclass("Graphics"), NULL, "Blit", "UnityEngine.Texture,UnityEngine.RenderTexture", a);
    int ok = save_png(rt, r[0], r[1], w, h, name);
    if (!ok) say("sprite %s: not saved, rt %p", name, rt);
    release_rt(rt);
    return ok;
}

// Portraits the game keeps for its turn order bar, by party member id.
static unsigned char portrait[64];
static void portraits(void *party) {
    void *battle = anyclass("Battle"); if (!battle) return;
    void *arr = sgetp(battle, "partyCTBSprites"); if (!arr) { static int once; if (!once++) say("no portrait sprites yet"); return; }
    long n = *(long *)((char *)arr + 24);
    for (int i = 0, pn = list_len(party); i < pn; i++) {
        void *m = list_at(party, i); if (!m) continue;
        int id = geti(m, "memberID");
        if (id < 0 || id >= 64 || id >= n || portrait[id] || !getb(m, "available") || geti(m, "posInParty") >= 20) continue;
        void *s = ((void **)((char *)arr + 32))[id]; char name[32]; snprintf(name, sizeof name, "portrait-%d.png", id);
        if (!s || !alive(s)) continue;   // the game fills these in a little after a save loads
        portrait[id] = save_sprite(s, name) ? 1 : 2;
    }
}

// ---- the area map ----
// Each area's map is an object in the scene that the game only looks at while its map is
// open, so a separate camera photographs it once per change.
static void *cam_go, *cam, *map_rt, *shot_map, *hidden_icon;
static int map_rev, map_sig, shot_sig, shooting;
static float map_rect[4];   // world x, y of the bottom left corner, width, height
static char map_file[64];
static uint32_t player_h, map_h;

static void *held(uint32_t h) { void *o = h ? il2cpp_gchandle_get_target(h) : NULL; return alive(o) ? o : NULL; }
static void hold(uint32_t *h, void *o) { if (*h) il2cpp_gchandle_free(*h); *h = o ? il2cpp_gchandle_new(o, 0) : 0; }

static void *child(void *go, const char *name) {
    void *tr = get0(go, "get_transform"), *a[1] = { il2cpp_string_new(name) };
    void *c = invoke(k_tr, tr, "Find", 1, a);
    return c ? get0(c, "get_gameObject") : NULL;
}
static int active(void *go) { void *r = get0(go, "get_activeSelf"); return r && *(unsigned char *)il2cpp_object_unbox(r); }

// Counts what is switched on in the map, so a newly uncovered part triggers a new picture.
static int signature(void *map) {
    int sig = 0; const char *groups[] = { "Masks", "MapIcons" };
    for (int g = 0; g < 2; g++) {
        void *go = child(map, groups[g]); if (!go) continue;
        void *tr = get0(go, "get_transform"); int n = unbox_int(get0(tr, "get_childCount"));
        for (int i = 0; i < n && i < 200; i++) {
            void *a[1] = { &i }, *c = invoke(k_tr, tr, "GetChild", 1, a);
            if (c && active(get0(c, "get_gameObject"))) sig = sig * 31 + i + 1 + g * 1000;
        }
    }
    return sig;
}

static void map_begin(void *map) {
    void *paper = child(map, "Map"), *main = invoke(k_cam, NULL, "get_main", 0, NULL);
    void *sr = paper ? component(paper, k_sr) : NULL;
    if (!sr || !main) return;
    float b[6]; void *bb = get0(sr, "get_bounds"); if (!bb) return;
    memcpy(b, il2cpp_object_unbox(bb), 24);
    int w = b[3] * 2, h = b[4] * 2;
    if (w < 64 || h < 64 || w > 2048 || h > 2048) return;
    if (!alive(cam_go)) {
        cam_go = il2cpp_object_new(k_go); void *na[1] = { il2cpp_string_new("ArmadaSecondScreenCam") };
        invoke_sig(k_go, cam_go, ".ctor", "System.String", na);
        void *da[1] = { cam_go }; invoke_sig(k_obj, NULL, "DontDestroyOnLoad", "UnityEngine.Object", da);
        void *ta[1] = { typeobj(k_cam) }; cam = invoke_sig(k_go, cam_go, "AddComponent", "System.Type", ta);
        il2cpp_gchandle_new(cam_go, 0); il2cpp_gchandle_new(cam, 0);
    }
    if (!alive(cam)) return;
    void *ca[1] = { main }; invoke(k_cam, cam, "CopyFrom", 1, ca);
    invoke(k_cam, cam, "ResetWorldToCameraMatrix", 0, NULL); invoke(k_cam, cam, "ResetProjectionMatrix", 0, NULL);
    map_rt = temp_rt(w, h, 16);
    if (!map_rt) return;
    float size = b[4], aspect = (float)w / h, pos[3] = { b[0], b[1], -10 }; unsigned char yes = 1; float depth = -50;
    set(cam, "set_orthographicSize", &size); set(cam, "set_aspect", &aspect); set(cam, "set_targetTexture", map_rt);
    set(cam, "set_depth", &depth); set(get0(cam, "get_transform"), "set_position", pos); set(cam, "set_enabled", &yes);
    // the game's own marker stays out of the picture
    hidden_icon = child(map, "PlayerPositionIcon");
    if (hidden_icon && active(hidden_icon)) { unsigned char no = 0; set(hidden_icon, "SetActive", &no); } else hidden_icon = NULL;
    map_rect[0] = b[0] - b[3]; map_rect[1] = b[1] - b[4]; map_rect[2] = w; map_rect[3] = h;
    shot_map = map; shooting = 1;
}

static void map_end(void) {
    unsigned char no = 0, yes = 1;
    if (alive(cam)) { set(cam, "set_enabled", &no); set(cam, "set_targetTexture", NULL); }
    if (hidden_icon && alive(hidden_icon)) set(hidden_icon, "SetActive", &yes);
    hidden_icon = NULL; shooting = 0;
    char name[64]; snprintf(name, sizeof name, "map-%d-%d.png", (int)getpid(), ++map_rev);
    if (save_png(map_rt, 0, 0, map_rect[2], map_rect[3], name)) {
        char old[700]; snprintf(old, sizeof old, "%s/art/%s", datadir, map_file); if (map_file[0]) unlink(old);
        strcpy(map_file, name); shot_sig = map_sig;
    }
    release_rt(map_rt); map_rt = NULL;
}

// Once a second: follow the current map object and take a new picture when it changed.
static void map_watch(void) {
    void *map = sgetp(k_gm, "currentMapObject");
    if (!alive(map) || sgeti(k_gm, "loading") & 1 || sgeti(k_gm, "switchingScenes") & 1 || sgeti(k_gm, "mapOpened") & 1) return;
    if (map != held(map_h)) { hold(&map_h, map); map_file[0] = 0; shot_sig = -1; }
    void *mc = component(map, anyclass("Map"));
    if (mc) { invoke(il2cpp_object_get_class(mc), mc, "MaskChecker", 0, NULL); void *n = getp(mc, "mapName"); (void)n; }
    map_sig = signature(map);
    if (map_sig != shot_sig || !map_file[0]) map_begin(map);
}

// Where the game would put its marker: the map's own placement of the scene plus the
// player's offset inside the camera boundaries, at the game's fixed map scale.
static int marker(float *u, float *v) {
    void *player = held(player_h), *map = held(map_h), *scene = sgetp(k_gm, "currentMapScene");
    void *main = invoke(k_cam, NULL, "get_main", 0, NULL);
    if (!player || !map || !scene || !main || !map_file[0]) return 0;
    void *bk = anyclass("ProCamera2DNumericBoundaries"); void *nb = bk ? component(get0(main, "get_gameObject"), bk) : NULL;
    if (!nb) return 0;
    float p[3], m[3]; vec3(get0(player, "get_transform"), "get_position", p); vec3(get0(map, "get_transform"), "get_position", m);
    long mo = offset(scene, "mapPosition"); if (mo < 0) return 0;
    float *mp = (float *)((char *)scene + mo);
    float left = *(float *)((char *)nb + offset(nb, "LeftBoundary")), top = *(float *)((char *)nb + offset(nb, "TopBoundary"));
    float x = m[0] + mp[0] + (p[0] - left) * 0.0492f, y = m[1] - mp[1] + (p[1] - top) * 0.0492f;
    *u = (x - map_rect[0]) / map_rect[2]; *v = 1 - (y - map_rect[1]) / map_rect[3];
    return *u >= 0 && *u <= 1 && *v >= 0 && *v <= 1;
}

static void equip(const char *slot, void *owned, void *equips, int idx) {
    void *e = idx >= 0 ? find_by_id(owned, "id", idx) : NULL;
    void *d = e ? find_by_id(equips, "equipID", geti(e, "equipID")) : NULL;
    if (!d) return;
    putf("\"%s\":{", slot); kstr("name", getp(d, "equipName")); kint("level", geti(e, "equipLvl")); close_obj(); puts_(",");
}

// The area's display name, from the game's own menu code.
static uint32_t menu_h, place_h, code_h; static void *place_for;
static void *place_name(void *code) {
    if (!code) return NULL;
    if (code == place_for && place_h) return il2cpp_gchandle_get_target(place_h);
    void *ms = held(menu_h), *k = anyclass("MainMenuSystem");
    if (!ms && k) {
        void *a[1] = { typeobj(k) }, *all = invoke_sig(anyclass("Resources"), NULL, "FindObjectsOfTypeAll", "System.Type", a);
        if (all && *(long *)((char *)all + 24) > 0) { ms = ((void **)((char *)all + 32))[0]; hold(&menu_h, ms); }
    }
    if (!ms) return NULL;
    void *a[1] = { code }, *name = invoke(k, ms, "ReturnAreaName", 1, a);
    if (!name) return NULL;
    if (place_h) il2cpp_gchandle_free(place_h);
    place_h = il2cpp_gchandle_new(name, 0); place_for = code; hold(&code_h, code);
    return name;
}

static double slow_at; static int at_title;
static void snapshot(double t) {
    len = 0; puts_("{");
    int slow = t >= slow_at;
    if (slow) {
        slow_at = t + 1;
        if (!held(player_h)) { void *a[1] = { typeobj(anyclass("Player")) }; hold(&player_h, invoke_sig(k_obj, NULL, "FindObjectOfType", "System.Type", a)); }
        // the title screen has a player object and starting data too
        void *sm = anyclass("StartMenu"), *a[1] = { sm ? typeobj(sm) : NULL };
        at_title = sm && invoke_sig(k_obj, NULL, "FindObjectOfType", "System.Type", a);
    }
    void *party = held(player_h) && !at_title ? call(k_data, "GetPartyMember") : NULL;
    if (!party || !list_len(party) || sgeti(k_gm, "loading") & 1) { if (at_title) map_file[0] = 0; puts_("\"ready\":false}"); return; }
    if (slow && !shooting) { portraits(party); map_watch(); }
    puts_("\"ready\":true,");
    kint("money", sgeti(k_gm, "money")); kint("playTime", sgeti(k_gm, "playTime"));
    kint("inMech", sgeti(k_gm, "currentlyInMech") & 1); kstr("place", place_name(sgetp(k_gm, "currentAreaName")));
    float u, v;
    if (map_file[0]) {
        puts_("\"map\":{"); putf("\"file\":\"art/%s\",\"w\":%d,\"h\":%d,", map_file, (int)map_rect[2], (int)map_rect[3]);
        if (marker(&u, &v)) putf("\"x\":%.4f,\"y\":%.4f,", u, v);
        close_obj(); puts_(",");
    }
    void *owned = call(k_data, "GetEquipItems"), *equips = call(k_db, "GetEquipment"), *emblems = call(k_data, "GetClassEmblems");
    key("party"); puts_("[");
    for (int i = 0, n = list_len(party); i < n; i++) {
        void *m = list_at(party, i);
        if (!m || !getb(m, "available") || geti(m, "posInParty") >= 20) continue;
        puts_("{"); int id = geti(m, "memberID");
        kint("id", id); kstr("name", getp(m, "memberName")); kint("mech", getb(m, "mech")); kint("art", id >= 0 && id < 64 && portrait[id] == 1);
        kint("pos", geti(m, "posInParty")); kint("level", geti(m, "memberLevel")); kint("sp", geti(m, "sp"));
        kint("hp", geti(m, "currentHP")); kint("maxHp", geti(m, "maxHP")); kint("tp", geti(m, "currentTP")); kint("maxTp", geti(m, "maxTP"));
        kint("atk", geti(m, "currentAtk")); kint("mag", geti(m, "currentMag")); kint("def", geti(m, "currentDef"));
        kint("mnd", geti(m, "currentMnd")); kint("agi", geti(m, "currentAgi")); kint("crit", geti(m, "currentCrit"));
        equip("weapon", owned, equips, geti(m, "weapon")); equip("armor", owned, equips, geti(m, "body")); equip("accessory", owned, equips, geti(m, "accessoire"));
        void *em = geti(m, "emblem") >= 0 ? find_by_id(emblems, "classId", geti(m, "emblem")) : NULL;
        if (em) kstr("emblem", getp(em, "className"));
        close_obj(); puts_(",");
    }
    close_arr(); puts_(",");
    void *quests = call(k_data, "GetQuests"), *tasks = call(k_data, "GetQuestTasks");
    key("quests"); puts_("[");
    for (int i = 0, n = list_len(quests), shown = 0; i < n && shown < 40; i++) {
        void *q = list_at(quests, i);
        if (!q || !getb(q, "questStarted") || getb(q, "questFinished")) continue;
        shown++;
        puts_("{"); int id = geti(q, "questId");
        kstr("name", getp(q, "questName")); kstr("desc", getp(q, "questDescr"));
        kint("main", getb(q, "mainQuest")); kint("pinned", getb(q, "pinned")); kint("order", geti(q, "order"));
        key("tasks"); puts_("[");
        for (int k = 0, tn = list_len(tasks); k < tn; k++) {
            void *task = list_at(tasks, k);
            if (!task || geti(task, "questId") != id || !getb(task, "taskStarted")) continue;
            puts_("{"); kstr("text", getp(task, "taskDescr")); kint("done", getb(task, "taskFinished")); kint("order", geti(task, "order")); close_obj(); puts_(",");
        }
        close_arr(); puts_("},");
    }
    close_arr(); puts_(",");
    void *items = call(k_data, "GetInventory");
    key("items"); puts_("[");
    for (int i = 0, n = list_len(items), shown = 0; i < n && shown < 300; i++) {
        void *it = list_at(items, i);
        if (!it || geti(it, "itemAmount") <= 0) continue;
        shown++;
        puts_("{"); kint("id", geti(it, "itemId")); kstr("name", getp(it, "itemName")); kstr("desc", getp(it, "itemDesc")); kint("count", geti(it, "itemAmount"));
        kint("type", geti(it, "itemType")); close_obj(); puts_(",");
    }
    close_arr(); puts_("}");
}

// ---- development aid, dev mode only: get <Class.member[3].member>, dump <path>, fields|methods <Class> ----
static void putval(void *addr, void *type);
static void putobj(void *o) {
    if (!o) { puts_("null"); return; }
    void *k = il2cpp_object_get_class(o);
    if (!strcmp(il2cpp_class_get_name(k), "String")) { putstr(o); return; }
    putf("<%s", il2cpp_class_get_name(k));
    void *f = il2cpp_class_get_field_from_name(k, "_size");
    if (f) putf(" n=%d", *(int *)((char *)o + il2cpp_field_get_offset(f)));
    else if (il2cpp_class_get_element_class(k) && il2cpp_class_get_element_class(k) != k && strchr(il2cpp_class_get_name(k), '[')) putf(" len=%d", (int)*(long *)((char *)o + 24));
    puts_(">");
}
static void putval(void *a, void *type) {
    switch (il2cpp_type_get_type(type)) {
    case 2: putf("%s", *(unsigned char *)a ? "true" : "false"); break;
    case 4: case 5: putf("%d", *(unsigned char *)a); break;
    case 6: case 7: putf("%d", *(short *)a); break;
    case 8: case 9: putf("%d", *(int *)a); break;
    case 10: case 11: putf("%ld", *(long *)a); break;
    case 12: putf("%.9g", *(float *)a); break;
    case 13: putf("%g", *(double *)a); break;
    case 0x11: { char *n = il2cpp_type_get_name(type); putf("{%s %.9g %.9g %.9g %.9g %.9g %.9g}", n, ((float *)a)[0], ((float *)a)[1], ((float *)a)[2], ((float *)a)[3], ((float *)a)[4], ((float *)a)[5]); break; }
    default: putobj(*(void **)a);
    }
}
static void dumpobj(void *o) {
    for (void *k = il2cpp_object_get_class(o); k && strcmp(il2cpp_class_get_name(k), "Object") && strcmp(il2cpp_class_get_name(k), "MonoBehaviour"); k = il2cpp_class_get_parent(k)) {
        void *it = NULL, *f;
        while ((f = il2cpp_class_get_fields(k, &it))) {
            if (il2cpp_field_get_flags(f) & 0x10) continue;
            putf("%s=", il2cpp_field_get_name(f)); putval((char *)o + il2cpp_field_get_offset(f), il2cpp_field_get_type(f)); puts_("\n");
        }
    }
}
// Children of a GameObject or Transform with their components.
static void kids(void *o, int level, int depth) {
    void *kt = anyclass("Transform"), *kg = anyclass("GameObject"), *kc = anyclass("Component");
    void *tr = il2cpp_object_get_class(o) == kg ? invoke(kg, o, "get_transform", 0, NULL) : o;
    void *n = invoke(kt, tr, "get_childCount", 0, NULL); int count = n ? *(int *)il2cpp_object_unbox(n) : 0;
    for (int i = 0; i < count && len < 30000; i++) {
        void *a[1] = { &i }, *c = invoke(kt, tr, "GetChild", 1, a); if (!c) continue;
        void *go = invoke(kc, c, "get_gameObject", 0, NULL), *act = invoke(kg, go, "get_activeSelf", 0, NULL);
        for (int s = 0; s < level; s++) puts_("  ");
        putstr(invoke(anyclass("Object"), go, "get_name", 0, NULL)); putf("%s", act && *(unsigned char *)il2cpp_object_unbox(act) ? " +" : " -");
        void *ta[1] = { typeobj(kc) }, *comps = invoke_sig(kg, go, "GetComponents", "System.Type", ta);
        if (comps) for (long j = 0, cn = *(long *)((char *)comps + 24); j < cn; j++) { void *x = ((void **)((char *)comps + 32))[j]; if (x) putf(" %s", il2cpp_class_get_name(il2cpp_object_get_class(x))); }
        puts_("\n");
        if (level + 1 < depth) kids(c, level + 1, depth);
    }
}
static void command(char *cmd) {
    len = 0;
    char *arg = strchr(cmd, ' '); if (!arg) { puts_("?"); return; } *arg++ = 0;
    if (!strcmp(cmd, "fields") || !strcmp(cmd, "methods")) {
        void *k = anyclass(arg), *it = NULL, *x;
        if (!k) { puts_("no class"); return; }
        if (cmd[0] == 'f') while ((x = il2cpp_class_get_fields(k, &it))) { char *n = il2cpp_type_get_name(il2cpp_field_get_type(x)); putf("%s%s %s\n", il2cpp_field_get_flags(x) & 0x10 ? "static " : "", n, il2cpp_field_get_name(x)); }
        else while ((x = il2cpp_class_get_methods(k, &it))) {
            putf("%s(", il2cpp_method_get_name(x));
            for (unsigned i = 0; i < il2cpp_method_get_param_count(x); i++) { char *n = il2cpp_type_get_name(il2cpp_method_get_param(x, i)); putf("%s%s", i ? "," : "", n); }
            puts_(")\n");
        }
        return;
    }
    static void *found;
    if (!strcmp(cmd, "find")) {
        void *k = anyclass(arg); if (!k) { puts_("no class"); return; }
        void *a[1] = { typeobj(k) };
        found = invoke_sig(anyclass("Object"), NULL, "FindObjectOfType", "System.Type", a);
        putobj(found); return;
    }
    int depth = 1; char *want = NULL;
    if (!strcmp(cmd, "comp")) { char *sp = strchr(arg, ' '); if (!sp) { puts_("comp <path> <Class>"); return; } *sp = 0; want = sp + 1; }
    if (!strcmp(cmd, "kids")) { char *sp = strchr(arg, ' '); if (sp) { *sp = 0; depth = sp[1] - 48; } }
        char *tok = strtok(arg, "."); void *k, *obj = NULL, *type = NULL; char buf[64]; void *addr = NULL;
    if (!strcmp(tok, "$")) { if (!found) { puts_("nothing found"); return; } obj = found; k = il2cpp_object_get_class(obj); memcpy(buf, &obj, 8); addr = buf; }
    else k = anyclass(tok);
    if (!k) { puts_("no class"); return; }
    while ((tok = strtok(NULL, "."))) {
        char *idx = strchr(tok, '['); if (idx) *idx++ = 0;
        size_t tl = strlen(tok);
        char *par = strchr(tok, '(');
        if (par && tok[tl - 1] == ')') {
            *par++ = 0; int argc = *par != ')', iv = 0; for (char *d = par; *d >= 48 && *d <= 57; d++) iv = iv * 10 + *d - 48;
            void *a[1] = { &iv };
            void *m = il2cpp_class_get_method_from_name(k, tok, argc), *exc = NULL;
            if (!m) { putf("no method %s", tok); return; }
            void *r = il2cpp_runtime_invoke(m, obj, argc ? a : NULL, &exc);
            if (exc) { puts_("exception"); return; }
            type = il2cpp_method_get_return_type(m);
            if (r && il2cpp_class_is_valuetype(il2cpp_object_get_class(r))) addr = il2cpp_object_unbox(r);
            else { memcpy(buf, &r, 8); addr = buf; }
        } else {
            void *f = NULL;
            for (void *c = k; c && !f; c = il2cpp_class_get_parent(c)) f = il2cpp_class_get_field_from_name(c, tok);
            if (!f) { putf("no field %s", tok); return; }
            type = il2cpp_field_get_type(f);
            if (il2cpp_field_get_flags(f) & 0x10) { il2cpp_field_static_get_value(f, buf); addr = buf; }
            else if (obj) addr = (char *)obj + il2cpp_field_get_offset(f);
            else { puts_("instance field on a class"); return; }
        }
        while (idx) {
            int i = 0; for (char *d = idx; *d >= 48 && *d <= 57; d++) i = i * 10 + *d - 48; void *o = *(void **)addr; if (!o) { puts_("null"); return; }
            void *items = il2cpp_class_get_field_from_name(il2cpp_object_get_class(o), "_items");
            if (items) o = *(void **)((char *)o + il2cpp_field_get_offset(items));
            void *ek = il2cpp_class_get_element_class(il2cpp_object_get_class(o));
            if (i < 0 || i >= (int)*(long *)((char *)o + 24)) { puts_("index out of range"); return; }
            if (il2cpp_class_is_valuetype(ek)) { addr = (char *)o + 32 + (long)i * il2cpp_class_array_element_size(il2cpp_object_get_class(o)); type = NULL; putf("%d", *(int *)addr); return; }
            memcpy(buf, (char *)o + 32 + 8L * i, 8); addr = buf; type = NULL;
            idx = strchr(idx, '['); if (idx) idx++;
        }
        int tt = type ? il2cpp_type_get_type(type) : 0x12;
        if (tt == 0x12 || tt == 0x15 || tt == 0x1d || tt == 0x1c || tt == 0xe) { obj = *(void **)addr; if (obj) k = il2cpp_object_get_class(obj); else { puts_("null"); return; } }
        else obj = NULL;
    }
    if (!addr) { puts_("class only"); return; }
    if (!strcmp(cmd, "kids") && obj) { kids(obj, 0, depth); return; }
    if (want && obj) {
        void *wk = anyclass(want); if (!wk) { puts_("no class"); return; }
        void *a[1] = { typeobj(wk) };
        found = invoke_sig(k, obj, "GetComponentInChildren", "System.Type", a);
        putobj(found); return;
    }
    if (!strcmp(cmd, "dump") && obj) dumpobj(obj);
    else if (type) putval(addr, type); else putobj(*(void **)addr);
}

// ---- link to the host ----
static int sock = -1;
static char *last; static double next_try, next_snap;
static int dead;

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }

static int send_msg(int type, const char *body, size_t n) {
    unsigned char hd[16] = { 'A', '2', 'S', 'S', type, 0, 0, 0, n, n >> 8, n >> 16, n >> 24 };
    char *buf = malloc(16 + n); memcpy(buf, hd, 16); memcpy(buf + 16, body, n);
    size_t done = 0; int tries = 0;
    while (done < 16 + n) {
        ssize_t w = send(sock, buf + done, 16 + n - done, MSG_NOSIGNAL);
        if (w > 0) { done += w; continue; }
        if (w < 0 && (errno == EAGAIN || errno == EINTR) && tries++ < 50) { usleep(1000); continue; }
        break;
    }
    free(buf);
    if (done == 16 + n) return 1;
    close(sock); sock = -1; free(last); last = NULL;
    return 0;
}

static void tick(void) {
    double t = now();
    if (dead || t < next_snap) return;
    next_snap = t + 0.2;
    if (shooting) map_end();
    if (sock < 0) {
        if (t < next_try) return;
        next_try = t + 2;
        int s = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons(47800), .sin_addr.s_addr = htonl(INADDR_LOOPBACK) };
        if (connect(s, (struct sockaddr *)&a, sizeof a)) { close(s); return; }
        int one = 1; setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
        fcntl(s, F_SETFL, O_NONBLOCK);
        sock = s;
        if (!send_msg(1, HELLO, strlen(HELLO))) return;
        say("attached");
    }
    static char in[4096]; static size_t have;
    ssize_t got; while ((got = recv(sock, in + have, sizeof in - 1 - have, 0)) > 0) have += got;
    while (have >= 16) {
        size_t n = (unsigned char)in[8] | (unsigned char)in[9] << 8;
        if (n > sizeof in - 17) { have = 0; break; }
        if (have < 16 + n) break;
        if (in[4] == 18 && n < 2048 && getenv("ARMADA_MOD_DEV")) {
            char cmd[2048]; memcpy(cmd, in + 16, n); cmd[n] = 0;
            command(cmd);
            char *text = strdup(out ? out : ""); size_t tl = strlen(text);
            len = 0; puts_("{\"reply\":\"");
            for (size_t i = 0; i < tl; i++) { char c = text[i]; if (c == '"') puts_("'"); else if (c == '\\') puts_("/"); else if (c == '\n') puts_("\\n"); else if ((unsigned char)c >= 32) put(&c, 1); }
            puts_("\"}"); free(text);
            send_msg(3, out, len);
            if (sock < 0) return;
        }
        memmove(in, in + 16 + n, have - 16 - n); have -= 16 + n;
    }
    snapshot(t);
    if (!last || strcmp(last, out)) {
        if (send_msg(3, out, len)) { free(last); last = strdup(out); }
    }
}

// ---- main-thread hook ----
// Unity calls a script's Update through its method record, so pointing the
// record at this function runs it every frame on the main thread.
static void (*orig_update)(void *, void *);
static int frames; static double fps_at;
static void update(void *self, void *method) {
    orig_update(self, method);
    tick();
    frames++;
    if (getenv("ARMADA_MOD_DEV")) { double t = now(); if (t - fps_at >= 10) { if (fps_at) say("fps %.1f viewer %d", frames / (t - fps_at), sock >= 0); frames = 0; fps_at = t; } }
}

static void *run(void *arg) {
    void *lib = NULL;
    for (int i = 0; i < 1200 && !lib; i++) { lib = dlopen("GameAssembly.so", RTLD_NOLOAD | RTLD_LAZY); if (!lib) usleep(100000); }
    if (!lib || !resolve(lib)) { say("no IL2CPP runtime"); return NULL; }
    void *domain = NULL;
    for (int i = 0; i < 600 && !image; i++) {
        usleep(200000);
        if (!(domain = il2cpp_domain_get())) continue;
        size_t n = 0; void **as = il2cpp_domain_get_assemblies(domain, &n);
        for (size_t k = 0; k < n; k++) {
            void *im = il2cpp_assembly_get_image(as[k]);
            if (im && nimages < 256 && k == (size_t)nimages) images[nimages++] = im;
            if (im && !strcmp(il2cpp_image_get_name(im), "Assembly-CSharp.dll")) image = im;
        }
    }
    if (!image) { say("no game assembly"); return NULL; }
    sleep(3);
    il2cpp_thread_attach(domain);
    { size_t n = 0; void **as = il2cpp_domain_get_assemblies(domain, &n); nimages = 0; for (size_t k = 0; k < n && k < 256; k++) images[nimages++] = il2cpp_assembly_get_image(as[k]); }
    k_gm = klass("GameManager"); k_db = klass("GetDatabase"); k_data = klass("GetData");
    k_obj = anyclass("Object"); k_go = anyclass("GameObject"); k_tr = anyclass("Transform"); k_comp = anyclass("Component");
    k_cam = anyclass("Camera"); k_rt = anyclass("RenderTexture"); k_tex = anyclass("Texture2D"); k_sr = anyclass("SpriteRenderer");
    void *k_in = klass("InputManagement");
    void **m = k_in ? il2cpp_class_get_method_from_name(k_in, "Update", 0) : NULL;
    if (!k_gm || !k_db || !k_data || !k_cam || !k_rt || !k_tex || !k_sr || !m) { say("game classes not found: %p %p %p", k_gm, k_db, m); return NULL; }
    orig_update = m[0];
    m[0] = update;
    say("hooked InputManagement.Update");
    return NULL;
}

__attribute__((constructor)) static void init(void) {
    char exe[512] = ""; ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1); if (n > 0) exe[n] = 0;
    if (!strstr(exe, "Chained_Echoes")) return;
    const char *dir = getenv("ARMADA_SECOND_SCREEN_DATA");
    if (!dir) return;
    snprintf(datadir, sizeof datadir, "%s", dir);
    char art[600]; snprintf(art, sizeof art, "%s/art", dir); mkdir(art, 0755);
    snprintf(logpath, sizeof logpath, "%s/provider.log", dir); unlink(logpath);
    say("provider loaded in %s", exe);
    pthread_t t; pthread_create(&t, NULL, run, NULL);
}
