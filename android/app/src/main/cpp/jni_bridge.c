/*
 * jni_bridge.c - JNI surface for the Android frontend.
 *
 * Thin layer over acore: it owns the per-instance ARGB frame buffer and
 * translates Java calls into acore calls. All the emulation and settings
 * logic lives in acore.c so the same code runs on the desktop builds.
 */
#include <jni.h>
#include <stdlib.h>
#include <string.h>
#include "acore.h"

#ifdef __ANDROID__
#include <android/log.h>
#define LOG_TAG "BoycottAdvance"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#else
/* Non-Android build (used to test this bridge on a desktop JVM). */
#include <stdio.h>
#define LOGI(...) do { fprintf(stderr, "[jni] "); fprintf(stderr, __VA_ARGS__); \
                       fprintf(stderr, "\n"); } while (0)
#endif

/* One per open emulator session (a game). Holds the core wrapper plus the
 * ARGB scratch the Java layer copies out of. */
typedef struct {
    Acore    *core;
    uint32_t *argb;     /* ACORE_W*ACORE_H */
} Session;

static Session *session_new(void) {
    Session *s = (Session *)calloc(1, sizeof(Session));
    if (!s) return NULL;
    s->core = acore_create();
    s->argb = (uint32_t *)malloc((size_t)ACORE_W * ACORE_H * 4);
    if (!s->core || !s->argb) {
        if (s->core) acore_destroy(s->core);
        free(s->argb);
        free(s);
        return NULL;
    }
    return s;
}

static void session_free(Session *s) {
    if (!s) return;
    if (s->core) acore_destroy(s->core);
    free(s->argb);
    free(s);
}

JNIEXPORT jlong JNICALL
Java_com_boycottadvance_reborn_NativeEmulator_create(JNIEnv *env, jclass clazz) {
    (void)env; (void)clazz;
    Session *s = session_new();
    LOGI("create -> %p", (void *)s);
    return (jlong)(intptr_t)s;
}

JNIEXPORT void JNICALL
Java_com_boycottadvance_reborn_NativeEmulator_destroy(JNIEnv *env, jclass clazz, jlong h) {
    (void)env; (void)clazz;
    session_free((Session *)(intptr_t)h);
}

JNIEXPORT jboolean JNICALL
Java_com_boycottadvance_reborn_NativeEmulator_loadBios(JNIEnv *env, jclass clazz,
                                                       jlong h, jstring path) {
    (void)clazz;
    Session *s = (Session *)(intptr_t)h;
    if (!s || !path) return JNI_FALSE;
    const char *p = (*env)->GetStringUTFChars(env, path, NULL);
    jboolean ok = acore_load_bios(s->core, p) ? JNI_TRUE : JNI_FALSE;
    (*env)->ReleaseStringUTFChars(env, path, p);
    return ok;
}

JNIEXPORT jboolean JNICALL
Java_com_boycottadvance_reborn_NativeEmulator_loadRom(JNIEnv *env, jclass clazz,
                                                      jlong h, jstring path) {
    (void)clazz;
    Session *s = (Session *)(intptr_t)h;
    if (!s || !path) return JNI_FALSE;
    const char *p = (*env)->GetStringUTFChars(env, path, NULL);
    jboolean ok = acore_load_rom(s->core, p) ? JNI_TRUE : JNI_FALSE;
    LOGI("loadRom(%s) -> %d", p, ok);
    (*env)->ReleaseStringUTFChars(env, path, p);
    return ok;
}

JNIEXPORT jboolean JNICALL
Java_com_boycottadvance_reborn_NativeEmulator_romLoaded(JNIEnv *env, jclass clazz, jlong h) {
    (void)env; (void)clazz;
    Session *s = (Session *)(intptr_t)h;
    return (s && acore_rom_loaded(s->core)) ? JNI_TRUE : JNI_FALSE;
}

/* Returns "title\0code\0savetype" so one JNI call fills all three strings. */
JNIEXPORT jstring JNICALL
Java_com_boycottadvance_reborn_NativeEmulator_romInfo(JNIEnv *env, jclass clazz, jlong h) {
    (void)clazz;
    Session *s = (Session *)(intptr_t)h;
    char title[32] = {0}, code[8] = {0}, sv[24] = {0};
    if (s) acore_rom_info(s->core, title, sizeof title, code, sizeof code, sv, sizeof sv);
    char buf[80];
    snprintf(buf, sizeof buf, "%s\n%s\n%s", title, code, sv);
    return (*env)->NewStringUTF(env, buf);
}

JNIEXPORT void JNICALL
Java_com_boycottadvance_reborn_NativeEmulator_reset(JNIEnv *env, jclass clazz, jlong h) {
    (void)env; (void)clazz;
    Session *s = (Session *)(intptr_t)h;
    if (s) acore_reset(s->core);
}

JNIEXPORT void JNICALL
Java_com_boycottadvance_reborn_NativeEmulator_saveBattery(JNIEnv *env, jclass clazz, jlong h) {
    (void)env; (void)clazz;
    Session *s = (Session *)(intptr_t)h;
    if (s) acore_save_battery(s->core);
}

JNIEXPORT void JNICALL
Java_com_boycottadvance_reborn_NativeEmulator_setKeys(JNIEnv *env, jclass clazz,
                                                      jlong h, jint mask) {
    (void)env; (void)clazz;
    Session *s = (Session *)(intptr_t)h;
    if (s) acore_set_keys(s->core, (uint16_t)mask);
}

/* Run one frame and copy the ARGB image into the caller's int[] (must hold
 * ACORE_W*ACORE_H entries). */
JNIEXPORT void JNICALL
Java_com_boycottadvance_reborn_NativeEmulator_runFrame(JNIEnv *env, jclass clazz,
                                                       jlong h, jintArray out) {
    (void)clazz;
    Session *s = (Session *)(intptr_t)h;
    if (!s) return;
    acore_run_frame(s->core);
    acore_frame_argb(s->core, s->argb, ACORE_W, ACORE_H, 0);
    (*env)->SetIntArrayRegion(env, out, 0, ACORE_W * ACORE_H, (jint *)s->argb);
}

JNIEXPORT jint JNICALL
Java_com_boycottadvance_reborn_NativeEmulator_audioMix(JNIEnv *env, jclass clazz,
                                                       jlong h, jshortArray out, jint nframes) {
    (void)clazz;
    Session *s = (Session *)(intptr_t)h;
    if (!s || nframes <= 0) return 0;
    jshort *buf = (*env)->GetShortArrayElements(env, out, NULL);
    if (!buf) return 0;
    int got = acore_audio_mix(s->core, (int16_t *)buf, nframes);
    (*env)->ReleaseShortArrayElements(env, out, buf, 0);
    return got;
}

JNIEXPORT jint JNICALL
Java_com_boycottadvance_reborn_NativeEmulator_audioAvailable(JNIEnv *env, jclass clazz, jlong h) {
    (void)env; (void)clazz;
    Session *s = (Session *)(intptr_t)h;
    return s ? acore_audio_available(s->core) : 0;
}

JNIEXPORT jint JNICALL
Java_com_boycottadvance_reborn_NativeEmulator_saveState(JNIEnv *env, jclass clazz,
                                                        jlong h, jint slot) {
    (void)env; (void)clazz;
    Session *s = (Session *)(intptr_t)h;
    return s ? acore_save_state(s->core, slot) : -1;
}

JNIEXPORT jint JNICALL
Java_com_boycottadvance_reborn_NativeEmulator_loadState(JNIEnv *env, jclass clazz,
                                                        jlong h, jint slot) {
    (void)env; (void)clazz;
    Session *s = (Session *)(intptr_t)h;
    return s ? acore_load_state(s->core, slot) : -1;
}

/* Settings accessors keyed by an int id (see NativeEmulator.SET_*). */
enum {
    SET_SOUND = 0, SET_DSOUND_A, SET_DSOUND_B, SET_GBC_SOUND, SET_REVERSE_STEREO,
    SET_BIOS_ENABLE, SET_BIOS_FASTBOOT, SET_SKIP_INTROS, SET_FRAMESKIP,
    SET_AUTOFRAMESKIP, SET_SCANLINE, SET_SMOOTH, SET_STATE_SLOT, SET_SHOWFPS
};

JNIEXPORT void JNICALL
Java_com_boycottadvance_reborn_NativeEmulator_setInt(JNIEnv *env, jclass clazz,
                                                     jlong h, jint key, jint value) {
    (void)env; (void)clazz;
    Session *s = (Session *)(intptr_t)h;
    if (!s) return;
    AcoreCfg *c = acore_cfg(s->core);
    switch (key) {
    case SET_SOUND:         c->sound_enable  = value; break;
    case SET_DSOUND_A:      c->dsound_a      = value; break;
    case SET_DSOUND_B:      c->dsound_b      = value; break;
    case SET_GBC_SOUND:     c->gbc_sound     = value; break;
    case SET_REVERSE_STEREO:c->reverse_stereo= value; break;
    case SET_BIOS_ENABLE:   c->biosenable    = value; break;
    case SET_BIOS_FASTBOOT: c->biosfastboot  = value; break;
    case SET_SKIP_INTROS:   c->skipintros    = value; break;
    case SET_FRAMESKIP:     c->frameskip     = value; break;
    case SET_AUTOFRAMESKIP: c->autoframeskip = value; break;
    case SET_SCANLINE:      c->scanline      = value; break;
    case SET_SMOOTH:        c->smooth        = value; break;
    case SET_STATE_SLOT:    c->state_slot    = value; break;
    case SET_SHOWFPS:       c->showfps       = value; break;
    default: break;
    }
}

JNIEXPORT jint JNICALL
Java_com_boycottadvance_reborn_NativeEmulator_getInt(JNIEnv *env, jclass clazz,
                                                     jlong h, jint key) {
    (void)env; (void)clazz;
    Session *s = (Session *)(intptr_t)h;
    if (!s) return 0;
    AcoreCfg *c = acore_cfg(s->core);
    switch (key) {
    case SET_SOUND:         return c->sound_enable;
    case SET_DSOUND_A:      return c->dsound_a;
    case SET_DSOUND_B:      return c->dsound_b;
    case SET_GBC_SOUND:     return c->gbc_sound;
    case SET_REVERSE_STEREO:return c->reverse_stereo;
    case SET_BIOS_ENABLE:   return c->biosenable;
    case SET_BIOS_FASTBOOT: return c->biosfastboot;
    case SET_SKIP_INTROS:   return c->skipintros;
    case SET_FRAMESKIP:     return c->frameskip;
    case SET_AUTOFRAMESKIP: return c->autoframeskip;
    case SET_SCANLINE:      return c->scanline;
    case SET_SMOOTH:        return c->smooth;
    case SET_STATE_SLOT:    return c->state_slot;
    case SET_SHOWFPS:       return c->showfps;
    default: return 0;
    }
}
