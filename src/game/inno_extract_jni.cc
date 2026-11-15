#ifdef __ANDROID__

#include "game/inno_extract.h"

#include <jni.h>
#include <string>

extern "C" JNIEXPORT jstring JNICALL
Java_com_opende_deusex_GogInstaller_nativeExtract(
    JNIEnv *env,
    jclass /* clazz */,
    jstring jInstallerPath,
    jstring jOutputDir,
    jobject jListener) {

    const char *rawInstaller = env->GetStringUTFChars(jInstallerPath, nullptr);
    const char *rawOutput = env->GetStringUTFChars(jOutputDir, nullptr);
    std::string installerPath = (rawInstaller != nullptr) ? rawInstaller : "";
    std::string outputDir = (rawOutput != nullptr) ? rawOutput : "";
    if (rawInstaller != nullptr) {
        env->ReleaseStringUTFChars(jInstallerPath, rawInstaller);
    }
    if (rawOutput != nullptr) {
        env->ReleaseStringUTFChars(jOutputDir, rawOutput);
    }

    jmethodID onProgressId = nullptr;
    if (jListener != nullptr) {
        jclass listenerCls = env->GetObjectClass(jListener);
        if (listenerCls != nullptr) {
            onProgressId = env->GetMethodID(
                listenerCls, "onProgress", "(JJIILjava/lang/String;)Z");
            env->DeleteLocalRef(listenerCls);
        }
    }

    opende::inno::ProgressCallback cb;
    if (jListener != nullptr && onProgressId != nullptr) {
        cb = [&](const opende::inno::ExtractProgress &p) -> bool {
            jstring jFile = env->NewStringUTF(p.currentFile.c_str());
            jboolean keepGoing = env->CallBooleanMethod(
                jListener,
                onProgressId,
                static_cast<jlong>(p.bytesDone),
                static_cast<jlong>(p.bytesTotal),
                static_cast<jint>(p.filesDone),
                static_cast<jint>(p.filesTotal),
                jFile);
            if (jFile != nullptr) {
                env->DeleteLocalRef(jFile);
            }
            if (env->ExceptionCheck() != JNI_FALSE) {
                env->ExceptionClear();
                return false;
            }
            return keepGoing != JNI_FALSE;
        };
    }

    opende::inno::ExtractResult res =
        opende::inno::extractGameFromInstaller(installerPath, outputDir, cb);

    if (res.ok) {
        return env->NewStringUTF("");
    }
    if (res.error.empty()) {
        return env->NewStringUTF("Erro desconhecido durante a extracao.");
    }
    return env->NewStringUTF(res.error.c_str());
}

#endif
