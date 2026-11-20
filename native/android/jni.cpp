#include <jni.h>

#include <string>

#include "engine.h"

static Engine g;

static std::string str(JNIEnv* env, jstring s) {
  const char* c = env->GetStringUTFChars(s, nullptr);
  std::string r = c ? c : "";
  if (c) env->ReleaseStringUTFChars(s, c);
  return r;
}
static jstring jstr(JNIEnv* env, const std::string& s) { return env->NewStringUTF(s.c_str()); }

#define FN(ret, name) extern "C" JNIEXPORT ret JNICALL Java_app_fortrakz_NativeEngine_##name

FN(jboolean, init)(JNIEnv* env, jobject, jstring dir) { return g.init(str(env, dir)); }
FN(void, setConfig)(JNIEnv*, jobject, jint preset, jint perf) {
  g.cfg.inputPreset = preset;
  g.cfg.perfMode = perf;
}
FN(jboolean, startStreams)(JNIEnv*, jobject) { return g.startStreams(); }
FN(void, stopStreams)(JNIEnv*, jobject) { g.stopStreams(); }
FN(jstring, streamInfo)(JNIEnv* env, jobject) { return jstr(env, g.duplex.infoJson()); }
FN(void, play)(JNIEnv*, jobject) { g.studio.play(); }
FN(jboolean, stopTransport)(JNIEnv*, jobject) { return g.studio.stop(); }
FN(jboolean, record)(JNIEnv*, jobject, jint t) { return g.studio.record(t); }
FN(void, seek)(JNIEnv*, jobject, jfloat sec) { g.studio.seek(int64_t(sec * fz::Studio::kSr)); }
FN(void, setTrack)(JNIEnv*, jobject, jint t, jfloat gain, jboolean mute, jboolean solo) {
  g.studio.setTrack(t, gain, mute, solo);
}
FN(void, setLatencyFrames)(JNIEnv*, jobject, jint f) { g.studio.setLatencyFrames(f); }
FN(void, setCleanMode)(JNIEnv*, jobject, jint m) { g.studio.setCleanMode(m); }
FN(jstring, finalizeTake)(JNIEnv* env, jobject) { return jstr(env, g.studio.finalizeTake()); }
FN(jstring, reclean)(JNIEnv* env, jobject, jint t) { return jstr(env, g.studio.reclean(t)); }
FN(void, clearTrack)(JNIEnv*, jobject, jint t) { g.studio.clearTrack(t); }
FN(void, clearAll)(JNIEnv*, jobject) { g.studio.clearAll(); }
FN(jboolean, hasTake)(JNIEnv*, jobject, jint t) { return g.studio.hasTake(t); }
FN(jboolean, echoValid)(JNIEnv*, jobject) { return g.echoValid(); }
FN(jboolean, saveProject)(JNIEnv*, jobject) { return g.studio.save(); }
FN(jboolean, exportMix)(JNIEnv* env, jobject, jstring path) { return g.studio.exportMix(str(env, path)); }
FN(void, status)(JNIEnv* env, jobject, jfloatArray arr) {
  float v[12] = {0};
  g.studio.status(v, 10);
  v[10] = float(g.duplex.xruns());
  v[11] = g.duplex.isOpen() ? 1.f : 0.f;
  env->SetFloatArrayRegion(arr, 0, 12, v);
}
FN(jstring, calibrate)(JNIEnv* env, jobject, jstring dump) { return jstr(env, g.calibrate(str(env, dump))); }
FN(jstring, labLatency)(JNIEnv* env, jobject, jint preset, jint perf, jstring dump, jstring tag) {
  return jstr(env, g.labLatency(preset, perf, str(env, dump), str(env, tag)));
}
FN(jstring, labEcho)(JNIEnv* env, jobject, jint preset, jint perf, jstring dump, jstring tag) {
  return jstr(env, g.labEcho(preset, perf, str(env, dump), str(env, tag), false));
}
