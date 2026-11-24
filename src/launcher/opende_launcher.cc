#include "launcher/opende_launcher.h"

#include "core/config.h"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#ifdef __ANDROID__
#include <jni.h>
#endif

extern "C" {
#include "core/common.h"
#include "core/log.h"
#include "core/paths.h"
#include "core/saves.h"
#include "core/shotlist.h"
#include "core/theme_colors.h"
#include "core/upkg.h"
#include "core/upkg_assets.h"
#include "platform/delete.h"
#include "ui/art.h"
#include "ui/audio.h"
#include "ui/controls.h"
#include "ui/draw.h"
#include "ui/glyphs.h"
#include "ui/kit.h"
#include "ui/osk.h"
#include "ui/pad3d.h"
#include "ui/text.h"
#include "ui/theme.h"
#include "ui/thumbs.h"
#include "ui/widgets.h"
}

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace game {
    namespace {

        enum class Page {
            Home,
            Saves,
            Mods,
            Shots,
            Settings,
            Install,
            About
        };

        enum class DialogKind {
            None,
            ConfirmDeleteSave,
            ConfirmDeleteShot,
            ConfirmDeleteMod,
            SteamMethod,
            SteamCredentials,
            PadPickAction,
            PadTest
        };

        struct BridgeSnapshot {
            int seq = 0;
            bool perm = true;
            bool gameValid = false;
            std::string gameRoot;
            bool busy = false;
            std::string task;
            int permille = 0;
            std::string status;
            std::string detail;
            std::string gogPath;
            std::string gogLabel;
            std::string error;
            int qrSeq = 0;
            bool guard = false;
            bool guardEmail = false;
            bool guardBad = false;
            std::string guardDomain;
            std::string installedMod;
            std::string appVersion;
            bool updAvail = false;
            std::string updTag;
            long long updSize = 0;
            std::string updNotes;
            bool needInstallPerm = false;
            bool pendingApk = false;
        };

        struct ModEntry {
            std::string id;       // "" for Vanilla
            std::string name;
            std::string version;
            std::filesystem::path directory;
            bool hasCustomScripts = false;
            int saveCount = 0;
        };

        struct LauncherState {
            LauncherParams params;
            LauncherOutcome outcome;

            SDL_Window* window = nullptr;
            SDL_Renderer* renderer = nullptr;
            dxl_ui ui {};
            dxl_art art {};
            dxl_audio* audio = nullptr;
            bool audioLoaded = false;

            dxl_menu_theme menuThemes[DXL_MENU_THEMES_MAX] {};
            int menuThemeCount = 0;
            std::string appliedThemeName;

            dx::Ini launcherIni;
            std::filesystem::path launcherIniPath;
            std::filesystem::path gameRoot;
            std::filesystem::path systemDir;
            bool gameValid = false;

            std::string activeModId;
            std::vector<ModEntry> mods;
            int modSel = 0;

            Page page = Page::Home;
            double pageSince = -1.0;

            dxl_saves saveList {};
            int saveSel = 0;
            SDL_Texture* saveSnap = nullptr;
            int saveSnapSlot = -2;
            bool saveSnapRead = false;

            dxl_thumbs* shotThumbs = nullptr;
            dxl_thumbs* shotWhole = nullptr;
            dxl_shots shotList {};
            int shotSel = 0;
            bool shotOpen = false;
            bool shotRefocus = false;

            int settingsTab = 0;
            int settingsSelectedRow = 0;
            std::string settingsHelpText;

            // 3D Controller preview, meters, button remapper & test modal
            dxl_padmesh padMesh {};
            int padMeshFamily = -1;
            SDL_JoystickID padSelectedId = 0;
            float padYaw = 0.0f;
            float padPitch = 0.0f;
            float padDragX = 0.0f;
            float padDragY = 0.0f;
            int padChosenPart = -1;
            int padFindPart = -1;
            dxl_rect padModelRect {};
            std::string padPickKey;
            std::string padPickLabel;
            int padPickPart = -1;
            SDL_JoystickID padTestId = 0;
            double padTestHoldSince = -1.0;
            bool padTestRumbling = false;

            DialogKind dialog = DialogKind::None;
            std::string dialogTitle;
            std::string dialogBody;
            int deleteTargetSlot = -2;
            int deleteShotIndex = -1;
            std::string deleteModId;
            std::string deleteModName;

            BridgeSnapshot bridge;
            int lastBridgeSeq = -1;
            SDL_Texture* qrTex = nullptr;
            int qrTexSeq = 0;
            int qrW = 0;
            int qrH = 0;

            char steamUser[128] {};
            char steamPass[128] {};
            char steamGuard[64] {};

            bool oskOpen = false;
            char* oskTarget = nullptr;
            size_t oskTargetSize = 0;
            std::string oskTitle;
            dxl_osk_state oskState {};

            // Launcher preferences synced with Launcher.ini
            double launcherMusicVol = 0.50;
            double launcherSoundVol = 0.70;
            bool reducedMotion = false;
            bool returnAfterPlaying = true;

            // Audio buffers extracted from DeusEx packages
            uint8_t* musicBuf = nullptr;
            size_t musicLen = 0;
            uint8_t* soundBufs[DXL_SOUND_COUNT] {};
            size_t soundLens[DXL_SOUND_COUNT] {};

            // Touch swipe tracking for ergonomic vertical list scrolling
            bool touchTracking = false;
            bool touchSwiping = false;
            SDL_FingerID touchFinger = 0;
            float touchStartX = 0.0f;
            float touchStartY = 0.0f;
            float touchLastY = 0.0f;

            // Toast notification
            std::string toastText;
            double toastAt = -10.0;

            bool finished = false;
        };

        std::string lowerStr(std::string s)
        {
            for (char& c : s) {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            return s;
        }

        bool iequalsStr(const std::string& a, const std::string& b)
        {
            if (a.size() != b.size()) {
                return false;
            }
            for (size_t i = 0; i < a.size(); ++i) {
                if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
                    return false;
                }
            }
            return true;
        }

        bool isTrueText(const std::string& v)
        {
            std::string l = lowerStr(v);
            return l == "true" || l == "1" || l == "verdadeiro" || l == "yes";
        }

        bool isPtLanguage()
        {
            if (iequalsStr(dx::config::language(), "ptb")) {
                return true;
            }
            if (dx::config::system().path.empty()) {
                int count = 0;
                if (SDL_Locale** locs = SDL_GetPreferredLocales(&count)) {
                    bool pt = false;
                    if (count > 0 && locs[0] && locs[0]->language) {
                        pt = iequalsStr(locs[0]->language, "pt");
                    }
                    SDL_free(locs);
                    if (pt) {
                        return true;
                    }
                }
            }
            return false;
        }

        const char* tr(const char* en, const char* pt)
        {
            return isPtLanguage() ? pt : en;
        }

        void showToast(LauncherState& st, const std::string& msg)
        {
            st.toastText = msg;
            st.toastAt = st.ui.now;
        }

        std::filesystem::path currentSaveDir()
        {
            std::string savePath = dx::config::system().get("Core.System", "SavePath").value_or("..\\Save");
            std::replace(savePath.begin(), savePath.end(), '\\', '/');
            return (dx::config::userDirectory() / "System" / savePath).lexically_normal();
        }

        std::filesystem::path currentShotDir()
        {
            return dx::config::system().path.parent_path() / "Screenshots";
        }

        std::string videoSection()
        {
            return dx::config::system().get("Engine.Engine", "ViewportManager").value_or("WinDrv.WindowsClient");
        }

        std::string renderSection()
        {
            return dx::config::system().get("Engine.Engine", "GameRenderDevice").value_or("OpenGLDrv.OpenGLRenderDevice");
        }

        std::string audioSection()
        {
            return dx::config::system().get("Engine.Engine", "AudioDevice").value_or("Galaxy.GalaxyAudioSubsystem");
        }

        void syncAndroidSelectedMod(const std::string& modId)
        {
#ifdef __ANDROID__
            JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
            jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
            if (env && activity) {
                jclass cls = env->GetObjectClass(activity);
                if (cls) {
                    jmethodID mid = env->GetStaticMethodID(cls, "setSelectedMod", "(Ljava/lang/String;)V");
                    if (mid) {
                        jstring jMod = env->NewStringUTF(modId.c_str());
                        env->CallStaticVoidMethod(cls, mid, jMod);
                        env->DeleteLocalRef(jMod);
                    }
                    if (env->ExceptionCheck()) {
                        env->ExceptionClear();
                    }
                    env->DeleteLocalRef(cls);
                }
                env->DeleteLocalRef(activity);
            }
#else
            (void)modId;
#endif
        }

        void callBridgeVoid(const char* method)
        {
#ifdef __ANDROID__
            JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
            jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
            if (!env || !activity) return;
            jclass cls = env->GetObjectClass(activity);
            if (cls) {
                jmethodID mid = env->GetStaticMethodID(cls, method, "()V");
                if (mid) env->CallStaticVoidMethod(cls, mid);
                if (env->ExceptionCheck()) env->ExceptionClear();
                env->DeleteLocalRef(cls);
            }
            env->DeleteLocalRef(activity);
#else
            (void)method;
#endif
        }

        void callBridgeBoolArg(const char* method, bool arg)
        {
#ifdef __ANDROID__
            JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
            jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
            if (!env || !activity) return;
            jclass cls = env->GetObjectClass(activity);
            if (cls) {
                jmethodID mid = env->GetStaticMethodID(cls, method, "(Z)V");
                if (mid) env->CallStaticVoidMethod(cls, mid, static_cast<jboolean>(arg ? JNI_TRUE : JNI_FALSE));
                if (env->ExceptionCheck()) env->ExceptionClear();
                env->DeleteLocalRef(cls);
            }
            env->DeleteLocalRef(activity);
#else
            (void)method;
            (void)arg;
#endif
        }

        void callBridgeStrArg(const char* method, const std::string& arg)
        {
#ifdef __ANDROID__
            JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
            jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
            if (!env || !activity) return;
            jclass cls = env->GetObjectClass(activity);
            if (cls) {
                jmethodID mid = env->GetStaticMethodID(cls, method, "(Ljava/lang/String;)V");
                if (mid) {
                    jstring jArg = env->NewStringUTF(arg.c_str());
                    env->CallStaticVoidMethod(cls, mid, jArg);
                    env->DeleteLocalRef(jArg);
                }
                if (env->ExceptionCheck()) env->ExceptionClear();
                env->DeleteLocalRef(cls);
            }
            env->DeleteLocalRef(activity);
#else
            (void)method;
            (void)arg;
#endif
        }

        void callBridgeStr2Arg(const char* method, const std::string& a, const std::string& b)
        {
#ifdef __ANDROID__
            JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
            jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
            if (!env || !activity) return;
            jclass cls = env->GetObjectClass(activity);
            if (cls) {
                jmethodID mid = env->GetStaticMethodID(cls, method, "(Ljava/lang/String;Ljava/lang/String;)V");
                if (mid) {
                    jstring jA = env->NewStringUTF(a.c_str());
                    jstring jB = env->NewStringUTF(b.c_str());
                    env->CallStaticVoidMethod(cls, mid, jA, jB);
                    env->DeleteLocalRef(jA);
                    env->DeleteLocalRef(jB);
                }
                if (env->ExceptionCheck()) env->ExceptionClear();
                env->DeleteLocalRef(cls);
            }
            env->DeleteLocalRef(activity);
#else
            (void)method;
            (void)a;
            (void)b;
#endif
        }

        bool callBridgeDeleteMod(const std::string& modId)
        {
#ifdef __ANDROID__
            bool result = false;
            JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
            jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
            if (!env || !activity) return false;
            jclass cls = env->GetObjectClass(activity);
            if (cls) {
                jmethodID mid = env->GetStaticMethodID(cls, "bridgeDeleteMod", "(Ljava/lang/String;)Z");
                if (mid) {
                    jstring jArg = env->NewStringUTF(modId.c_str());
                    result = env->CallStaticBooleanMethod(cls, mid, jArg) != JNI_FALSE;
                    env->DeleteLocalRef(jArg);
                }
                if (env->ExceptionCheck()) env->ExceptionClear();
                env->DeleteLocalRef(cls);
            }
            env->DeleteLocalRef(activity);
            return result;
#else
            (void)modId;
            return false;
#endif
        }

        int fetchBridgeSeq()
        {
#ifdef __ANDROID__
            int seq = 0;
            JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
            jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
            if (!env || !activity) return 0;
            jclass cls = env->GetObjectClass(activity);
            if (cls) {
                jmethodID mid = env->GetStaticMethodID(cls, "bridgeGetSeq", "()I");
                if (mid) seq = static_cast<int>(env->CallStaticIntMethod(cls, mid));
                if (env->ExceptionCheck()) env->ExceptionClear();
                env->DeleteLocalRef(cls);
            }
            env->DeleteLocalRef(activity);
            return seq;
#else
            return 0;
#endif
        }

        std::string unescapeBridgeValue(const std::string& in)
        {
            std::string out;
            out.reserve(in.size());
            for (size_t idx = 0; idx < in.size(); ++idx) {
                if (in[idx] == '\\' && idx + 1 < in.size()) {
                    char nx = in[idx + 1];
                    if (nx == 'n') {
                        out.push_back('\n');
                        ++idx;
                        continue;
                    }
                    if (nx == '\\') {
                        out.push_back('\\');
                        ++idx;
                        continue;
                    }
                }
                out.push_back(in[idx]);
            }
            return out;
        }

        BridgeSnapshot fetchBridgeSnapshot()
        {
            BridgeSnapshot snap;
#ifdef __ANDROID__
            std::string raw;
            JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
            jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
            if (!env || !activity) return snap;
            jclass cls = env->GetObjectClass(activity);
            if (cls) {
                jmethodID mid = env->GetStaticMethodID(cls, "bridgeGetSnapshot", "()Ljava/lang/String;");
                if (mid) {
                    jstring jRaw = static_cast<jstring>(env->CallStaticObjectMethod(cls, mid));
                    if (jRaw) {
                        const char* utf = env->GetStringUTFChars(jRaw, nullptr);
                        if (utf) {
                            raw = utf;
                            env->ReleaseStringUTFChars(jRaw, utf);
                        }
                        env->DeleteLocalRef(jRaw);
                    }
                }
                if (env->ExceptionCheck()) env->ExceptionClear();
                env->DeleteLocalRef(cls);
            }
            env->DeleteLocalRef(activity);

            size_t pos = 0;
            while (pos < raw.size()) {
                size_t nl = raw.find('\n', pos);
                std::string line = (nl == std::string::npos) ? raw.substr(pos) : raw.substr(pos, nl - pos);
                pos = (nl == std::string::npos) ? raw.size() : nl + 1;
                size_t eq = line.find('=');
                if (eq == std::string::npos) continue;
                std::string k = line.substr(0, eq);
                std::string v = unescapeBridgeValue(line.substr(eq + 1));
                if (k == "seq") snap.seq = std::atoi(v.c_str());
                else if (k == "perm") snap.perm = (std::atoi(v.c_str()) != 0);
                else if (k == "game_valid") snap.gameValid = (std::atoi(v.c_str()) != 0);
                else if (k == "game_root") snap.gameRoot = v;
                else if (k == "busy") snap.busy = (std::atoi(v.c_str()) != 0);
                else if (k == "task") snap.task = v;
                else if (k == "permille") snap.permille = std::atoi(v.c_str());
                else if (k == "status") snap.status = v;
                else if (k == "detail") snap.detail = v;
                else if (k == "gog_path") snap.gogPath = v;
                else if (k == "gog_label") snap.gogLabel = v;
                else if (k == "error") snap.error = v;
                else if (k == "qr_seq") snap.qrSeq = std::atoi(v.c_str());
                else if (k == "guard") snap.guard = (std::atoi(v.c_str()) != 0);
                else if (k == "guard_email") snap.guardEmail = (std::atoi(v.c_str()) != 0);
                else if (k == "guard_bad") snap.guardBad = (std::atoi(v.c_str()) != 0);
                else if (k == "guard_domain") snap.guardDomain = v;
                else if (k == "installed_mod") snap.installedMod = v;
                else if (k == "app_version") snap.appVersion = v;
                else if (k == "upd_avail") snap.updAvail = (std::atoi(v.c_str()) != 0);
                else if (k == "upd_tag") snap.updTag = v;
                else if (k == "upd_size") snap.updSize = std::atoll(v.c_str());
                else if (k == "upd_notes") snap.updNotes = v;
                else if (k == "need_install_perm") snap.needInstallPerm = (std::atoi(v.c_str()) != 0);
                else if (k == "pending_apk") snap.pendingApk = (std::atoi(v.c_str()) != 0);
            }
#endif
            return snap;
        }

        void syncSteamQrTexture(LauncherState& st)
        {
#ifdef __ANDROID__
            if (st.bridge.qrSeq <= 0) {
                if (st.qrTex) {
                    SDL_DestroyTexture(st.qrTex);
                    st.qrTex = nullptr;
                    st.qrTexSeq = 0;
                    st.qrW = 0;
                    st.qrH = 0;
                }
                return;
            }
            if (st.qrTexSeq == st.bridge.qrSeq && st.qrTex) {
                return;
            }
            JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
            jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
            if (!env || !activity || !st.ui.renderer) return;
            jclass cls = env->GetObjectClass(activity);
            if (cls) {
                jmethodID mid = env->GetStaticMethodID(cls, "bridgeGetSteamQrRgba", "([I)[B");
                if (mid) {
                    jintArray whArr = env->NewIntArray(2);
                    if (whArr) {
                        jbyteArray pxArr = static_cast<jbyteArray>(env->CallStaticObjectMethod(cls, mid, whArr));
                        if (pxArr) {
                            jint wh[2] { 0, 0 };
                            env->GetIntArrayRegion(whArr, 0, 2, wh);
                            jsize len = env->GetArrayLength(pxArr);
                            if (wh[0] > 0 && wh[1] > 0 && len >= wh[0] * wh[1] * 4) {
                                jbyte* ptr = env->GetByteArrayElements(pxArr, nullptr);
                                if (ptr) {
                                    if (st.qrTex) {
                                        SDL_DestroyTexture(st.qrTex);
                                    }
                                    st.qrTex = SDL_CreateTexture(st.ui.renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, wh[0], wh[1]);
                                    if (st.qrTex) {
                                        SDL_UpdateTexture(st.qrTex, nullptr, ptr, wh[0] * 4);
                                        SDL_SetTextureScaleMode(st.qrTex, SDL_SCALEMODE_NEAREST);
                                        st.qrW = wh[0];
                                        st.qrH = wh[1];
                                        st.qrTexSeq = st.bridge.qrSeq;
                                    }
                                    env->ReleaseByteArrayElements(pxArr, ptr, JNI_ABORT);
                                }
                            }
                            env->DeleteLocalRef(pxArr);
                        }
                        env->DeleteLocalRef(whArr);
                    }
                }
                if (env->ExceptionCheck()) env->ExceptionClear();
                env->DeleteLocalRef(cls);
            }
            env->DeleteLocalRef(activity);
#else
            (void)st;
#endif
        }

        std::filesystem::path resolveSubdirCi(const std::filesystem::path& base, const char* name)
        {
            std::filesystem::path exact = base / name;
            std::error_code ec;
            if (std::filesystem::is_directory(exact, ec)) {
                return exact;
            }
            std::filesystem::path resolved = dx::config::resolveCase(exact);
            if (std::filesystem::is_directory(resolved, ec)) {
                return resolved;
            }
            return {};
        }

        void applyMenuTheme(LauncherState& st)
        {
            std::string themeName = dx::config::user().get("DeusEx.DeusExPlayer", "MenuThemeName").value_or("Default");
            if (themeName == st.appliedThemeName && !st.appliedThemeName.empty()) {
                return;
            }
            st.appliedThemeName = themeName;
            dxl_theme theme {};
            const dxl_menu_theme* found = dxl_menu_theme_find(st.menuThemes, st.menuThemeCount, themeName.c_str());
            if (found) {
                dxl_rgba cols[DXL_MENU_COLORS] {};
                for (int i = 0; i < DXL_MENU_COLORS; ++i) {
                    cols[i] = dxl_rgba{ found->colors[i][0], found->colors[i][1], found->colors[i][2], found->colors[i][3] };
                }
                dxl_theme_from_menu(&theme, found->name, cols);
            } else {
                dxl_theme_default(&theme);
            }
            dxl_ui_set_theme(&st.ui, &theme);
        }

        void reloadSaves(LauncherState& st)
        {
            int keep = (st.saveSel >= 0 && st.saveSel < st.saveList.count) ? st.saveList.items[st.saveSel].slot : -2;
            if (st.saveSnap) {
                SDL_DestroyTexture(st.saveSnap);
                st.saveSnap = nullptr;
            }
            st.saveSnapSlot = -2;
            st.saveSnapRead = false;
            dxl_saves_free(&st.saveList);
            st.saveSel = 0;
            dxl_err err {};
            std::filesystem::path dir = currentSaveDir();
            if (dxl_saves_list(dir.c_str(), &st.saveList, &err) != 0) {
                dxl_log("saves: %s", dxl_err_msg(&err));
            }
            for (int i = 0; i < st.saveList.count; ++i) {
                if (st.saveList.items[i].slot == keep) {
                    st.saveSel = i;
                    break;
                }
            }
        }

        void reloadShots(LauncherState& st)
        {
            std::string keep;
            if (st.shotSel >= 0 && st.shotSel < st.shotList.count) {
                keep = st.shotList.items[st.shotSel].name;
            }
            dxl_shots_free(&st.shotList);
            st.shotSel = 0;
            dxl_err err {};
            std::filesystem::path dir = currentShotDir();
            if (dxl_shots_list(dir.c_str(), &st.shotList, &err) != 0) {
                dxl_log("shots: %s", dxl_err_msg(&err));
            }
            for (int i = 0; i < st.shotList.count && !keep.empty(); ++i) {
                if (keep == st.shotList.items[i].name) {
                    st.shotSel = i;
                    break;
                }
            }
            if (st.shotOpen && st.shotList.count == 0) {
                st.shotOpen = false;
            }
        }

        void activateModProfile(LauncherState& st, const std::string& modId)
        {
            st.activeModId = modId;
            if (!modId.empty()) {
                std::filesystem::path modDir = (st.gameRoot / ".." / "mods" / modId).lexically_normal();
                std::error_code ec;
                if (!std::filesystem::is_directory(modDir, ec)) {
                    modDir = (st.gameRoot / "mods" / modId).lexically_normal();
                }
                if (std::filesystem::is_directory(modDir, ec)) {
                    dx::config::useMod(modDir);
                } else {
                    dx::config::useMod({});
                    st.activeModId.clear();
                }
            } else {
                dx::config::useMod({});
            }
            dx::config::initialize(st.systemDir, "", "");
            st.appliedThemeName.clear();
            applyMenuTheme(st);
            reloadSaves(st);
            reloadShots(st);
            syncAndroidSelectedMod(st.activeModId);
        }

        void scanMods(LauncherState& st)
        {
            st.mods.clear();
            ModEntry vanilla;
            vanilla.id = "";
            vanilla.name = tr("Deus Ex (Original / Vanilla)", "Deus Ex (Original / Vanilla)");
            vanilla.version = "1112fm";
            vanilla.directory = st.gameRoot;
            {
                dx::config::useMod({});
                dx::config::initialize(st.systemDir, "", "");
                vanilla.saveCount = dxl_saves_count(currentSaveDir().c_str());
            }
            st.mods.push_back(vanilla);

            std::vector<std::filesystem::path> roots;
            std::filesystem::path parentMods = (st.gameRoot / ".." / "mods").lexically_normal();
            std::filesystem::path innerMods = (st.gameRoot / "mods").lexically_normal();
            roots.push_back(parentMods);
            if (innerMods != parentMods) {
                roots.push_back(innerMods);
            }

            std::error_code ec;
            for (const std::filesystem::path& mroot : roots) {
                if (!std::filesystem::is_directory(mroot, ec)) {
                    continue;
                }
                for (const auto& entry : std::filesystem::directory_iterator(mroot, ec)) {
                    if (ec || !entry.is_directory()) {
                        continue;
                    }
                    std::string id = entry.path().filename().string();
                    if (id.empty() || id[0] == '.') {
                        continue;
                    }
                    bool duplicate = false;
                    for (const ModEntry& existing : st.mods) {
                        if (iequalsStr(existing.id, id)) {
                            duplicate = true;
                            break;
                        }
                    }
                    if (duplicate) {
                        continue;
                    }
                    std::filesystem::path modSys = resolveSubdirCi(entry.path(), "System");
                    std::filesystem::path modMaps = resolveSubdirCi(entry.path(), "Maps");
                    std::filesystem::path modTex = resolveSubdirCi(entry.path(), "Textures");
                    if (modSys.empty() && modMaps.empty() && modTex.empty() && !std::filesystem::is_regular_file(entry.path() / "mod.json", ec)) {
                        continue;
                    }
                    ModEntry mod;
                    mod.id = id;
                    mod.name = id;
                    mod.directory = entry.path();
                    if (!modSys.empty()) {
                        for (const auto& f : std::filesystem::directory_iterator(modSys, ec)) {
                            if (!ec && f.is_regular_file() && iequalsStr(f.path().extension().string(), ".u")) {
                                mod.hasCustomScripts = true;
                                break;
                            }
                        }
                    }
                    std::filesystem::path modSaveDir = dx::config::userDirectory() / "mods" / id / "Save";
                    if (st.activeModId.empty()) {
                        modSaveDir = (dx::config::userDirectory() / "mods" / id / "Save").lexically_normal();
                    }
                    mod.saveCount = dxl_saves_count(modSaveDir.c_str());
                    st.mods.push_back(std::move(mod));
                }
            }

            if (st.mods.size() > 1) {
                std::sort(st.mods.begin() + 1, st.mods.end(), [](const ModEntry& a, const ModEntry& b) {
                    return lowerStr(a.name) < lowerStr(b.name);
                });
            }
            activateModProfile(st, st.activeModId);
            st.modSel = 0;
            for (size_t i = 0; i < st.mods.size(); ++i) {
                if (st.mods[i].id == st.activeModId) {
                    st.modSel = static_cast<int>(i);
                    break;
                }
            }
        }

        bool isDirectoryValidGameRoot(const std::filesystem::path& root)
        {
            if (root.empty()) {
                return false;
            }
            std::error_code ec;
            std::filesystem::path sys = resolveSubdirCi(root, "System");
            if (sys.empty()) {
                return false;
            }
            return std::filesystem::is_regular_file(dx::config::resolveCase(sys / "DeusEx.u"), ec)
                && std::filesystem::is_regular_file(dx::config::resolveCase(sys / "Engine.u"), ec);
        }

        void loadPackagesAudioAndThemes(LauncherState& st)
        {
            if (st.audioLoaded || !st.gameValid) {
                return;
            }
            st.audioLoaded = true;
            dxl_err err {};
            std::filesystem::path dxU = dx::config::resolveCase(st.systemDir / "DeusEx.u");
            if (std::filesystem::is_regular_file(dxU)) {
                int n = dxl_menu_themes_read(dxU.c_str(), st.menuThemes, DXL_MENU_THEMES_MAX, &err);
                if (n > 0) {
                    st.menuThemeCount = n;
                }
            }

            char* umx = dxl_path_resolve_ci(st.gameRoot.c_str(), "Music/Title_Music.umx");
            if (umx) {
                dxl_upkg pkg {};
                char type[16] {};
                if (dxl_upkg_load(&pkg, umx, &err) == 0) {
                    st.musicBuf = dxl_upkg_file(&pkg, dxl_upkg_find(&pkg, "Music", "Title_Music"), &st.musicLen, type, sizeof(type), &err);
                    dxl_upkg_free(&pkg);
                }
                std::free(umx);
            }

            char* soundsU = dxl_path_resolve_ci(st.gameRoot.c_str(), "System/DeusExSounds.u");
            if (soundsU) {
                dxl_upkg pkg {};
                char type[16] {};
                if (dxl_upkg_load(&pkg, soundsU, &err) == 0) {
                    for (int i = 0; i < DXL_SOUND_COUNT; ++i) {
                        st.soundBufs[i] = dxl_upkg_file(&pkg, dxl_upkg_find(&pkg, "Sound", dxl_sound_names[i]), &st.soundLens[i], type, sizeof(type), &err);
                    }
                    dxl_upkg_free(&pkg);
                }
                std::free(soundsU);
            }

            if (!st.audio) {
                st.audio = dxl_audio_open();
            }
            if (st.audio) {
                dxl_audio_volumes(st.audio, static_cast<float>(st.launcherMusicVol), static_cast<float>(st.launcherSoundVol));
                for (int i = 0; i < DXL_SOUND_COUNT; ++i) {
                    if (st.soundBufs[i]) {
                        dxl_audio_sound(st.audio, static_cast<dxl_sound>(i), st.soundBufs[i], st.soundLens[i]);
                    }
                }
                if (st.musicBuf && st.musicLen > 0) {
                    dxl_audio_music(st.audio, st.musicBuf, st.musicLen);
                }
            }
        }

        void goPage(LauncherState& st, Page next);

        void applyGameRoot(LauncherState& st, const std::filesystem::path& root, bool switchToHome)
        {
            if (!isDirectoryValidGameRoot(root)) {
                return;
            }
            st.gameRoot = root;
            st.systemDir = resolveSubdirCi(st.gameRoot, "System");
            if (st.systemDir.empty()) {
                st.systemDir = st.gameRoot / "System";
            }
            st.gameValid = true;
            scanMods(st);
            loadPackagesAudioAndThemes(st);
            applyMenuTheme(st);
            dxl_art_load(&st.art, &st.ui, st.gameRoot.c_str());
            if (switchToHome && st.page == Page::Install) {
                goPage(st, Page::Home);
                showToast(st, tr("Deus Ex installed and ready!", "Deus Ex pronto para jogar!"));
            }
        }

        void pollAndroidBridge(LauncherState& st)
        {
#ifdef __ANDROID__
            int seq = fetchBridgeSeq();
            if (seq == st.lastBridgeSeq) {
                return;
            }
            st.lastBridgeSeq = seq;
            st.bridge = fetchBridgeSnapshot();
            syncSteamQrTexture(st);

            if (st.bridge.gameValid && !st.bridge.gameRoot.empty()) {
                std::filesystem::path newRoot(st.bridge.gameRoot);
                if (!st.gameValid || newRoot != st.gameRoot) {
                    bool wasInvalid = !st.gameValid;
                    applyGameRoot(st, newRoot, wasInvalid);
                }
            }
            if (!st.bridge.installedMod.empty() && st.gameValid) {
                std::string newMod = st.bridge.installedMod;
                callBridgeVoid("bridgeConsumeInstalledModId");
                st.activeModId = newMod;
                scanMods(st);
                showToast(st, std::string(tr("Mod installed: ", "Mod instalado: ")) + newMod);
            }
#else
            (void)st;
#endif
        }

        void loadLauncherIni(LauncherState& st)
        {
            std::filesystem::path baseDir = dx::config::userDirectory();
#ifdef __ANDROID__
            if (const char* internalPath = SDL_GetAndroidInternalStoragePath()) {
                baseDir = internalPath;
            }
#endif
            std::error_code ec;
            std::filesystem::create_directories(baseDir, ec);
            st.launcherIniPath = baseDir / "Launcher.ini";
            st.launcherIni.path = st.launcherIniPath;
            st.launcherIni.load(st.launcherIniPath);

            st.launcherMusicVol = std::clamp(std::atof(st.launcherIni.get("Launcher", "MusicVolume").value_or("0.50").c_str()), 0.0, 1.0);
            st.launcherSoundVol = std::clamp(std::atof(st.launcherIni.get("Launcher", "SoundVolume").value_or("0.70").c_str()), 0.0, 1.0);
            st.reducedMotion = isTrueText(st.launcherIni.get("Launcher", "ReducedMotion").value_or("False"));
            st.returnAfterPlaying = isTrueText(st.launcherIni.get("Launcher", "ReturnAfterPlaying").value_or("True"));
            st.ui.reduced_motion = st.reducedMotion ? 1 : 0;
        }

        void saveLauncherIni(LauncherState& st)
        {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.2f", st.launcherMusicVol);
            st.launcherIni.set("Launcher", "MusicVolume", buf);
            std::snprintf(buf, sizeof(buf), "%.2f", st.launcherSoundVol);
            st.launcherIni.set("Launcher", "SoundVolume", buf);
            st.launcherIni.set("Launcher", "ReducedMotion", st.reducedMotion ? "True" : "False");
            st.launcherIni.set("Launcher", "ReturnAfterPlaying", st.returnAfterPlaying ? "True" : "False");
            st.launcherIni.save();
        }

        void goPage(LauncherState& st, Page next)
        {
            if (st.page == Page::Settings && next != Page::Settings) {
                dx::config::system().save();
                dx::config::user().save();
                saveLauncherIni(st);
            }
            st.page = next;
            st.pageSince = st.ui.now;
            st.settingsHelpText.clear();
            dxl_ui_forget(&st.ui);
            if (next == Page::Saves) {
                reloadSaves(st);
            } else if (next == Page::Shots) {
                if (!st.shotThumbs) {
                    st.shotThumbs = dxl_thumbs_new(384, 216);
                }
                if (!st.shotWhole) {
                    st.shotWhole = dxl_thumbs_new(1920, 1080);
                }
                st.shotOpen = false;
                st.shotRefocus = false;
                reloadShots(st);
            }
        }

        void playCueSound(LauncherState& st)
        {
            if (!st.audio) {
                return;
            }
            int c = st.ui.cues;
            if (c & DXL_CUE_PLAY) {
                dxl_audio_play(st.audio, DXL_SOUND_OK);
            } else if (c & DXL_CUE_BACK) {
                dxl_audio_play(st.audio, DXL_SOUND_CANCEL);
            } else if (c & DXL_CUE_OPEN) {
                dxl_audio_play(st.audio, DXL_SOUND_ACTIVATE);
            } else if (c & DXL_CUE_PRESS) {
                dxl_audio_play(st.audio, DXL_SOUND_PRESS);
            } else if (c & DXL_CUE_SLIDE) {
                dxl_audio_play(st.audio, DXL_SOUND_SLIDER);
            } else if (c & DXL_CUE_FOCUS) {
                dxl_audio_play(st.audio, DXL_SOUND_FOCUS);
            }
        }

        const dxl_save* latestSave(const LauncherState& st)
        {
            return dxl_saves_latest(&st.saveList);
        }

        SDL_Texture* loadSaveSnapshotTexture(LauncherState& st, const dxl_save* sv)
        {
            if (!sv || !sv->readable) {
                return nullptr;
            }
            if (st.saveSnapSlot != sv->slot) {
                if (st.saveSnap) {
                    SDL_DestroyTexture(st.saveSnap);
                    st.saveSnap = nullptr;
                }
                st.saveSnapSlot = sv->slot;
                st.saveSnapRead = false;
            }
            if (st.saveSnapRead || !st.ui.renderer) {
                return st.saveSnap;
            }
            st.saveSnapRead = true;
            std::filesystem::path saveDir = currentSaveDir();
            char* folderPath = dxl_save_path(saveDir.c_str(), sv);
            if (folderPath) {
                char* infoPath = dxl_path_resolve_ci(folderPath, "SaveInfo.dxs");
                if (infoPath) {
                    dxl_saveinfo info {};
                    dxl_err err {};
                    if (dxl_saveinfo_read(infoPath, 1, &info, &err) == 0 && info.snapshot.rgba) {
                        st.saveSnap = SDL_CreateTexture(st.ui.renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, info.snapshot.w, info.snapshot.h);
                        if (st.saveSnap) {
                            SDL_UpdateTexture(st.saveSnap, nullptr, info.snapshot.rgba, info.snapshot.w * 4);
                            SDL_SetTextureScaleMode(st.saveSnap, SDL_SCALEMODE_LINEAR);
                        }
                        dxl_saveinfo_free(&info);
                    }
                    std::free(infoPath);
                }
                std::free(folderPath);
            }
            return st.saveSnap;
        }

        void drawSaveSnapshot(LauncherState& st, const dxl_save* sv, dxl_rect r)
        {
            SDL_Texture* tex = loadSaveSnapshotTexture(st, sv);
            if (!tex) {
                return;
            }
            float tw = 0.0f;
            float th = 0.0f;
            SDL_GetTextureSize(tex, &tw, &th);
            SDL_FRect src { 0.0f, 0.0f, tw, th };
            if (tw == 256.0f && th == 128.0f) {
                src = SDL_FRect { 0.0f, 0.0f, 160.0f, 120.0f };
            }
            float k = std::min(r.w / src.w, r.h / src.h);
            dxl_rect at { r.x, r.y, src.w * k, src.h * k };
            dxl_rect px = dxl_px(&st.ui, at);
            SDL_RenderTexture(st.ui.renderer, tex, &src, &px);
            dxl_outline(&st.ui, at, 1.0f, st.ui.theme.panel_light);
        }

        void startGame(LauncherState& st, const std::string& url)
        {
            if (!st.gameValid) {
                goPage(st, Page::Install);
                return;
            }
            dx::config::system().save();
            dx::config::user().save();
            saveLauncherIni(st);
            st.ui.cues |= DXL_CUE_PLAY;
            st.outcome.action = LauncherAction::Play;
            st.outcome.gameRoot = st.gameRoot.string();
            st.outcome.modId = st.activeModId;
            st.outcome.startUrl = url;
            st.outcome.returnAfterPlaying = st.returnAfterPlaying;
            st.finished = true;
        }

        int homeButton(LauncherState& st, const char* id, dxl_rect r, const char* label, int flags)
        {
            uint32_t bid = dxl_ui_id(&st.ui, id);
            if (dxl_art_has_buttons(&st.art)) {
                return dxl_art_button(&st.ui, &st.art, bid, r, label, flags);
            }
            return dxl_button_id(&st.ui, bid, r, label, flags);
        }

        float drawHeader(LauncherState& st, float margin, float shift)
        {
            dxl_ui* ui = &st.ui;
            const dxl_theme* t = &ui->theme;
            float x = margin + shift;
            float w = ui->w - 2.0f * margin;
            dxl_label(ui, dxl_rect{ x, 20.0f, w, 20.0f }, DXL_FONT_LABEL, "DXLAUNCHER", t->accent, DXL_W_LEFT);
            if (st.page == Page::Home) {
                if (st.gameValid) {
                    dxl_art_load(&st.art, ui, st.gameRoot.c_str());
                }
                if (dxl_art_wordmark(ui, &st.art, dxl_rect{ x - 4.0f, 42.0f, w, 54.0f }) <= 0.0f) {
                    dxl_label(ui, dxl_rect{ x, 40.0f, w, 56.0f }, DXL_FONT_TITLE, "DEUS EX", t->title_text, DXL_W_LEFT);
                }
                dxl_fill(ui, dxl_rect{ x, 102.0f, 64.0f, 2.0f }, t->accent);
                return 124.0f;
            }
            const char* pageTitle = "";
            switch (st.page) {
            case Page::Home: pageTitle = "DXLauncher"; break;
            case Page::Saves: pageTitle = tr("Saved Games", "Jogos Salvos"); break;
            case Page::Mods: pageTitle = tr("Mods & Profiles", "Mods e Perfis"); break;
            case Page::Shots: pageTitle = tr("Screenshots", "Capturas de Tela"); break;
            case Page::Settings: pageTitle = tr("Settings", "Configurações"); break;
            case Page::Install: pageTitle = tr("Install & Updates", "Instalação e Atualizações"); break;
            case Page::About: pageTitle = tr("About OpenDE", "Sobre o OpenDE"); break;
            }
            dxl_label_typed(ui, dxl_rect{ x, 44.0f, w * 0.7f, 34.0f }, DXL_FONT_HEADING, pageTitle, t->title_text, DXL_W_LEFT, st.pageSince);
            dxl_fill(ui, dxl_rect{ x, 82.0f, 56.0f, 2.0f }, t->accent);
            return 102.0f;
        }

        void drawFooter(LauncherState& st, dxl_rect r, float margin)
        {
            dxl_ui* ui = &st.ui;
            const dxl_theme* t = &ui->theme;
            dxl_fill(ui, r, dxl_rgba_fade(t->backdrop_bottom, 0.88f));
            dxl_fill(ui, dxl_rect{ r.x, r.y, r.w, 1.0f }, dxl_rgba_fade(t->panel_light, 0.25f));

            dxl_hint_item items[4] {};
            int n = 0;
            items[n++] = dxl_hint_item{ DXL_HINT_CONFIRM, tr("Select", "Selecionar") };
            if (st.dialog != DialogKind::None || st.bridge.busy || st.bridge.guard || st.oskOpen) {
                items[n++] = dxl_hint_item{ DXL_HINT_BACK, tr("Cancel", "Cancelar") };
            } else {
                if (st.page == Page::Home) {
                    if (st.gameValid) {
                        const dxl_save* latest = latestSave(st);
                        items[n++] = dxl_hint_item{ DXL_HINT_PLAY, latest ? tr("Continue", "Continuar") : tr("Play", "Jogar") };
                    }
                } else if (st.page == Page::Saves && st.saveSel >= 0 && st.saveSel < st.saveList.count && st.saveList.items[st.saveSel].readable) {
                    items[n++] = dxl_hint_item{ DXL_HINT_PLAY, tr("Load", "Carregar") };
                    items[n++] = dxl_hint_item{ DXL_HINT_BACK, tr("Back", "Voltar") };
                } else if (st.page == Page::Settings) {
                    items[n++] = dxl_hint_item{ DXL_HINT_TAB_NEXT, tr("Next Tab", "Próxima Aba") };
                    items[n++] = dxl_hint_item{ DXL_HINT_BACK, tr("Done", "Concluir") };
                } else {
                    items[n++] = dxl_hint_item{ DXL_HINT_BACK, tr("Back", "Voltar") };
                }
            }

            dxl_rect hints { r.x + margin, r.y, r.w - 2.0f * margin, r.h };
            int hit = dxl_hints(ui, hints, items, n);
            if (hit == DXL_HINT_CONFIRM) {
                ui->in.activate++;
            } else if (hit == DXL_HINT_BACK) {
                ui->in.back++;
            } else if (hit == DXL_HINT_PLAY) {
                ui->in.start++;
            } else if (hit == DXL_HINT_TAB_NEXT) {
                ui->in.shoulder_r++;
            }

            const std::string& profileLabel = (st.modSel >= 0 && static_cast<size_t>(st.modSel) < st.mods.size())
                ? st.mods[static_cast<size_t>(st.modSel)].name
                : std::string("Deus Ex");
            std::string status = profileLabel + "  \xc2\xb7  OpenDE " + st.params.versionLabel + "  \xc2\xb7  SDL 3.4";
            dxl_label(ui, dxl_rect{ r.x + margin, r.y, r.w * 0.45f, r.h }, DXL_FONT_SMALL, status.c_str(), t->help, DXL_W_LEFT | DXL_W_MAY_CUT);
        }

        void drawHomePage(LauncherState& st, dxl_rect r)
        {
            dxl_ui* ui = &st.ui;
            const dxl_theme* t = &ui->theme;
            const dxl_save* latest = st.gameValid ? latestSave(st) : nullptr;

            float menuW = std::clamp(r.w * 0.30f, 230.0f, 320.0f);
            dxl_rect menu { r.x, r.y + 4.0f, menuW, r.h - 4.0f };

            const int btnCount = st.gameValid ? ((latest ? 1 : 0) + 8) : 3;
            float bh = 44.0f;
            float gap = 7.0f;
            if (static_cast<float>(btnCount) * bh + static_cast<float>(btnCount - 1) * gap > menu.h) {
                gap = 4.0f;
                bh = std::max(24.0f, (menu.h - static_cast<float>(btnCount - 1) * gap) / static_cast<float>(btnCount));
            }

            bool clickContinue = false;
            bool clickPlay = false;
            bool clickMods = false;
            bool clickSaves = false;
            bool clickShots = false;
            bool clickSettings = false;
            bool clickInstall = false;
            bool clickAbout = false;
            bool clickQuit = false;

            std::string installLabel = st.gameValid
                ? (st.bridge.updAvail
                    ? std::string(tr("Install & Updates (!)", "Instalar e Atualizar (!)"))
                    : std::string(tr("Install & Updates...", "Instalar e Atualizar...")))
                : std::string(tr("Install Deus Ex...", "Instalar Deus Ex..."));

            if (st.gameValid) {
                if (latest) {
                    clickContinue = homeButton(st, "home continue", dxl_cut_top(&menu, bh), tr("Continue", "Continuar"), DXL_W_PRIMARY | DXL_W_LEFT) != 0;
                    dxl_cut_top(&menu, gap);
                }
                clickPlay = homeButton(st, "home play", dxl_cut_top(&menu, bh), tr("Play", "Jogar"), (latest ? 0 : DXL_W_PRIMARY) | DXL_W_LEFT) != 0;
                dxl_cut_top(&menu, gap);
                clickMods = homeButton(st, "home mods", dxl_cut_top(&menu, bh), tr("Mods & Profiles", "Mods e Perfis"), DXL_W_LEFT) != 0;
                dxl_cut_top(&menu, gap);
                clickSaves = homeButton(st, "home saves", dxl_cut_top(&menu, bh), tr("Saves", "Saves"), DXL_W_LEFT) != 0;
                dxl_cut_top(&menu, gap);
                clickShots = homeButton(st, "home shots", dxl_cut_top(&menu, bh), tr("Screenshots", "Capturas"), DXL_W_LEFT) != 0;
                dxl_cut_top(&menu, gap);
                clickSettings = homeButton(st, "home settings", dxl_cut_top(&menu, bh), tr("Settings", "Configurações"), DXL_W_LEFT) != 0;
                dxl_cut_top(&menu, gap);
                clickInstall = homeButton(st, "home setup", dxl_cut_top(&menu, bh), installLabel.c_str(), DXL_W_LEFT) != 0;
                dxl_cut_top(&menu, gap);
                clickAbout = homeButton(st, "home about", dxl_cut_top(&menu, bh), tr("About", "Sobre"), DXL_W_LEFT) != 0;
                dxl_cut_top(&menu, gap);
                clickQuit = homeButton(st, "home quit", dxl_cut_top(&menu, bh), tr("Quit", "Sair"), DXL_W_LEFT) != 0;
            } else {
                clickInstall = homeButton(st, "home setup", dxl_cut_top(&menu, bh), installLabel.c_str(), DXL_W_PRIMARY | DXL_W_LEFT) != 0;
                dxl_cut_top(&menu, gap);
                clickAbout = homeButton(st, "home about", dxl_cut_top(&menu, bh), tr("About", "Sobre"), DXL_W_LEFT) != 0;
                dxl_cut_top(&menu, gap);
                clickQuit = homeButton(st, "home quit", dxl_cut_top(&menu, bh), tr("Quit", "Sair"), DXL_W_LEFT) != 0;
            }

            // Profile summary card on right
            float px = r.x + menuW + 34.0f;
            float pw = std::min(640.0f, r.x + r.w - px);
            if (pw > 180.0f) {
                const std::string& profileName = (st.gameValid && st.modSel >= 0 && static_cast<size_t>(st.modSel) < st.mods.size())
                    ? st.mods[static_cast<size_t>(st.modSel)].name
                    : std::string(st.gameValid ? "Deus Ex" : tr("Setup Required", "Instalação Pendente"));
                std::string cardTitle = st.gameValid
                    ? (std::string(tr("Profile: ", "Perfil: ")) + profileName)
                    : profileName;
                int rows = st.gameValid ? (5 + (st.activeModId.empty() ? 0 : 1) + (latest ? 1 : 0)) : 4;
                const float needH = 5.0f + 32.0f + 16.0f + static_cast<float>(rows) * 28.0f + 22.0f;
                float cardH = std::min(r.h - 4.0f, needH);
                dxl_rect panel { px, r.y + 4.0f, pw, cardH };
                dxl_rect in = dxl_window(ui, panel, cardTitle.c_str());

                const float nw = 108.0f;
                const float lh = 28.0f;
                if (st.gameValid) {
                    dxl_field(ui, &in, lh, nw, tr("Game", "Jogo"), st.gameRoot.c_str(), t->text, DXL_W_TAIL | DXL_W_MAY_CUT);
                    if (!st.activeModId.empty()) {
                        dxl_field(ui, &in, lh, nw, "Mod", st.activeModId.c_str(), t->accent, DXL_W_MAY_CUT);
                    }
                    std::string engineDesc = "OpenDE " + st.params.versionLabel + " (OpenGL ES 3 / SDL 3.4)";
                    dxl_field(ui, &in, lh, nw, tr("Engine", "Motor"), engineDesc.c_str(), t->text, DXL_W_MAY_CUT);
                    std::string langDesc = isPtLanguage() ? "Português (BR)" : "English";
                    dxl_field(ui, &in, lh, nw, tr("Language", "Idioma"), langDesc.c_str(), t->text, DXL_W_MAY_CUT);
                    char savesStr[32];
                    std::snprintf(savesStr, sizeof(savesStr), "%d", st.saveList.count);
                    dxl_field(ui, &in, lh, nw, "Saves", st.saveList.count > 0 ? savesStr : tr("None yet", "Nenhum"), t->text, 0);

                    if (latest) {
                        char when[64] {};
                        dxl_save_when(latest, when, sizeof(when));
                        std::string latestDesc = latest->info.description;
                        if (latest->info.location[0] && latestDesc != latest->info.location) {
                            latestDesc += std::string("  \xc2\xb7  ") + latest->info.location;
                        }
                        if (when[0]) {
                            latestDesc += std::string(", ") + when;
                        }
                        dxl_field(ui, &in, lh, nw, tr("Latest", "Último"), latestDesc.c_str(), t->text_focus, DXL_W_MAY_CUT);

                        float room = r.y + r.h - (panel.y + panel.h + 14.0f);
                        if (room >= 96.0f) {
                            float sh = std::min(room, 210.0f);
                            drawSaveSnapshot(st, latest, dxl_rect{ px, panel.y + panel.h + 14.0f, sh * 4.0f / 3.0f, sh });
                        }
                    }
                } else {
                    dxl_field(ui, &in, lh, nw, tr("Status", "Estado"), tr("Game files not configured yet", "Arquivos do jogo ainda não configurados"), t->accent, DXL_W_MAY_CUT);
                    dxl_field(ui, &in, lh, nw, tr("Storage", "Arquivos"), st.bridge.perm ? tr("Access granted", "Acesso permitido") : tr("Permission needed", "Permissão pendente"), t->text, DXL_W_MAY_CUT);
                    dxl_field(ui, &in, lh, nw, tr("Options", "Opções"), tr("GOG .exe, Steam (QR/Login), or Folder", "Instalador GOG (.exe), Steam ou Pasta"), t->text, DXL_W_MAY_CUT);
                    if (!st.bridge.gogLabel.empty()) {
                        dxl_field(ui, &in, lh, nw, "GOG", st.bridge.gogLabel.c_str(), t->text_focus, DXL_W_MAY_CUT);
                    }
                }
            }

            if (st.gameValid && (clickContinue || (ui->in.start && latest))) {
                char url[64];
                dxl_save_url(latest->slot, url, sizeof(url));
                startGame(st, url);
            } else if (st.gameValid && (clickPlay || ui->in.start)) {
                startGame(st, "");
            } else if (st.gameValid && clickMods) {
                goPage(st, Page::Mods);
            } else if (st.gameValid && clickSaves) {
                goPage(st, Page::Saves);
            } else if (st.gameValid && clickShots) {
                goPage(st, Page::Shots);
            } else if (st.gameValid && clickSettings) {
                goPage(st, Page::Settings);
            } else if (clickInstall) {
                goPage(st, Page::Install);
            } else if (clickAbout) {
                goPage(st, Page::About);
            } else if (clickQuit) {
                st.outcome.action = LauncherAction::Quit;
                st.finished = true;
            }
        }

        void drawSavesPage(LauncherState& st, dxl_rect r)
        {
            dxl_ui* ui = &st.ui;
            const dxl_theme* t = &ui->theme;
            const float sideW = std::min(340.0f, r.w * 0.36f);
            dxl_rect listPanel { r.x, r.y, r.w - sideW - 18.0f, r.h };
            dxl_rect side { listPanel.x + listPanel.w + 18.0f, r.y, sideW, r.h };

            dxl_panel(ui, listPanel);
            dxl_rect inside = dxl_inset(listPanel, 10.0f);

            if (st.saveList.count == 0) {
                dxl_label(ui, inside, DXL_FONT_BODY, tr("No saved games in this profile yet.", "Nenhum save encontrado neste perfil ainda."), t->help, DXL_W_CENTER | DXL_W_MAY_CUT);
            } else {
                dxl_list list {};
                dxl_list_begin(ui, &list, "saves", inside, st.saveList.count, 58.0f, DXL_LIST_WHOLE_ROWS);
                for (int i = 0; i < st.saveList.count; ++i) {
                    const dxl_save* sv = &st.saveList.items[i];
                    char title[180] {};
                    char detail[256] {};
                    if (sv->readable) {
                        char when[64] {};
                        char played[48] {};
                        dxl_save_when(sv, when, sizeof(when));
                        dxl_save_played(sv, played, sizeof(played));
                        std::snprintf(title, sizeof(title), "%s", sv->info.description[0] ? sv->info.description : sv->folder);
                        std::snprintf(detail, sizeof(detail), "%s  \xc2\xb7  %s  \xc2\xb7  %s",
                            sv->info.location[0] ? sv->info.location : sv->folder,
                            when[0] ? when : "-",
                            played[0] ? played : "-");
                    } else {
                        std::snprintf(title, sizeof(title), "%s", sv->folder);
                        std::snprintf(detail, sizeof(detail), "%s", tr("Unreadable SaveInfo.dxs", "SaveInfo.dxs ilegível"));
                    }
                    int hits = dxl_list_row(ui, &list, i, title, detail, t->help, 0);
                    if (hits & DXL_HIT_FOCUSED) {
                        st.saveSel = i;
                    }
                    if ((hits & DXL_HIT_ACTIVATED) && sv->readable) {
                        char url[64];
                        dxl_save_url(sv->slot, url, sizeof(url));
                        startGame(st, url);
                    }
                }
                dxl_list_end(ui, &list);
            }

            const dxl_save* chosen = (st.saveSel >= 0 && st.saveSel < st.saveList.count) ? &st.saveList.items[st.saveSel] : nullptr;
            if (chosen && chosen->readable) {
                float snapH = std::min(190.0f, side.w * 0.75f);
                drawSaveSnapshot(st, chosen, dxl_rect{ side.x, side.y, side.w, snapH });
                dxl_cut_top(&side, snapH + 10.0f);

                char when[64] {};
                char played[48] {};
                dxl_save_when(chosen, when, sizeof(when));
                dxl_save_played(chosen, played, sizeof(played));
                dxl_field(ui, &side, 26.0f, 82.0f, tr("Saved", "Salvo"), when, t->text, DXL_W_MAY_CUT);
                dxl_field(ui, &side, 26.0f, 82.0f, tr("Played", "Jogado"), played, t->text, DXL_W_MAY_CUT);
                dxl_field(ui, &side, 26.0f, 82.0f, tr("Map", "Mapa"), chosen->info.map[0] ? chosen->info.map : chosen->folder, t->help, DXL_W_MAY_CUT);
                dxl_cut_top(&side, 10.0f);
            }

            if (dxl_button(ui, "save load", dxl_cut_top(&side, 44.0f), tr("Load", "Carregar"),
                DXL_W_PRIMARY | ((chosen && chosen->readable) ? 0 : DXL_W_DISABLED)) ||
                (ui->in.start && chosen && chosen->readable)) {
                char url[64];
                dxl_save_url(chosen->slot, url, sizeof(url));
                startGame(st, url);
                return;
            }
            dxl_cut_top(&side, 10.0f);
            if (dxl_button(ui, "save delete", dxl_cut_top(&side, 44.0f), tr("Delete...", "Excluir..."), chosen ? 0 : DXL_W_DISABLED)) {
                st.dialog = DialogKind::ConfirmDeleteSave;
                st.deleteTargetSlot = st.saveSel;
                st.dialogTitle = tr("Delete Saved Game?", "Excluir Save?");
                st.dialogBody = chosen->readable ? chosen->info.description : chosen->folder;
            }
            dxl_cut_top(&side, 10.0f);
            if (dxl_button(ui, "save back", dxl_cut_top(&side, 44.0f), tr("Back", "Voltar"), 0) || ui->in.back) {
                ui->in.back = 0;
                goPage(st, Page::Home);
            }
        }

        void drawModsPage(LauncherState& st, dxl_rect r)
        {
            dxl_ui* ui = &st.ui;
            const dxl_theme* t = &ui->theme;
            const float sideW = std::min(320.0f, r.w * 0.36f);
            dxl_rect listPanel { r.x, r.y, r.w - sideW - 18.0f, r.h };
            dxl_rect side { listPanel.x + listPanel.w + 18.0f, r.y, sideW, r.h };

            dxl_panel(ui, listPanel);
            dxl_rect inside = dxl_inset(listPanel, 10.0f);
            dxl_list list {};
            dxl_list_begin(ui, &list, "mods", inside, static_cast<int>(st.mods.size()), 58.0f, DXL_LIST_WHOLE_ROWS);
            for (size_t i = 0; i < st.mods.size(); ++i) {
                const ModEntry& m = st.mods[i];
                std::string detail = m.id.empty()
                    ? tr("Base Deus Ex installation (isolated User & Save)", "Instalação base Deus Ex (User e Save padrão)")
                    : (std::string(m.hasCustomScripts ? "[Total/Script] " : "[Content Mod] ") + "mods/" + m.id);
                if (m.id == st.activeModId) {
                    detail = std::string(tr("[ACTIVE] ", "[ATIVO] ")) + detail;
                }
                int hits = dxl_list_row(ui, &list, static_cast<int>(i), m.name.c_str(), detail.c_str(), m.id == st.activeModId ? t->accent : t->help, 0);
                if (hits & DXL_HIT_FOCUSED) {
                    st.modSel = static_cast<int>(i);
                }
                if (hits & DXL_HIT_ACTIVATED) {
                    activateModProfile(st, m.id);
                    showToast(st, std::string(tr("Active profile: ", "Perfil ativo: ")) + m.name);
                }
            }
            dxl_list_end(ui, &list);

            const ModEntry* chosen = (st.modSel >= 0 && static_cast<size_t>(st.modSel) < st.mods.size()) ? &st.mods[static_cast<size_t>(st.modSel)] : nullptr;
            if (chosen) {
                dxl_rect infoBox = dxl_window(ui, dxl_rect{ side.x, side.y, side.w, 126.0f }, chosen->name.c_str());
                dxl_field(ui, &infoBox, 26.0f, 80.0f, tr("ID", "ID"), chosen->id.empty() ? "vanilla" : chosen->id.c_str(), t->text, DXL_W_MAY_CUT);
                dxl_field(ui, &infoBox, 26.0f, 80.0f, tr("Scripts", "Scripts"), chosen->hasCustomScripts ? tr("Custom .u", ".u customizado") : tr("Standard", "Padrão"), t->text, 0);
                dxl_cut_top(&side, 138.0f);
            }

            if (dxl_button(ui, "mod use", dxl_cut_top(&side, 42.0f), tr("Use & Play", "Ativar e Jogar"), DXL_W_PRIMARY) && chosen) {
                activateModProfile(st, chosen->id);
                startGame(st, "");
                return;
            }
            dxl_cut_top(&side, 8.0f);
            if (dxl_button(ui, "mod install file", dxl_cut_top(&side, 42.0f), tr("Install Mod (.zip/.7z/.exe)...", "Instalar Mod (Arquivo)..."), 0)) {
                callBridgeVoid("bridgePickModArchive");
            }
            dxl_cut_top(&side, 8.0f);
            if (dxl_button(ui, "mod install dir", dxl_cut_top(&side, 42.0f), tr("Install Mod (Folder)...", "Instalar Mod (Pasta)..."), 0)) {
                callBridgeVoid("bridgePickModFolder");
            }
            if (chosen && !chosen->id.empty()) {
                dxl_cut_top(&side, 8.0f);
                if (dxl_button(ui, "mod delete", dxl_cut_top(&side, 42.0f), tr("Delete Mod...", "Excluir Mod..."), 0)) {
                    st.dialog = DialogKind::ConfirmDeleteMod;
                    st.deleteModId = chosen->id;
                    st.deleteModName = chosen->name;
                    st.dialogTitle = tr("Delete Mod?", "Excluir Mod?");
                    st.dialogBody = chosen->name + " (" + chosen->id + ")";
                }
            }
            dxl_cut_top(&side, 8.0f);
            if (dxl_button(ui, "mod back", dxl_cut_top(&side, 42.0f), tr("Back", "Voltar"), 0) || ui->in.back) {
                ui->in.back = 0;
                goPage(st, Page::Home);
            }
        }

        void drawPicture(LauncherState& st, dxl_thumbs* set, const char* path, dxl_rect r)
        {
            dxl_ui* ui = &st.ui;
            const dxl_theme* t = &ui->theme;
            dxl_thumbs_want(set, path);
            int failed = 0;
            int w = 0;
            int h = 0;
            SDL_Texture* tex = dxl_thumbs_get(set, path, &failed, &w, &h);
            if (!tex || w <= 0 || h <= 0) {
                dxl_fill(ui, r, dxl_rgba_fade(t->panel_shadow, 0.6f));
                dxl_label(ui, r, DXL_FONT_SMALL, failed ? tr("Unreadable", "Ilegível") : "...", t->help, DXL_W_MAY_CUT);
                return;
            }
            float k = std::min(r.w / static_cast<float>(w), r.h / static_cast<float>(h));
            dxl_rect at { r.x + (r.w - static_cast<float>(w) * k) * 0.5f, r.y + (r.h - static_cast<float>(h) * k) * 0.5f, static_cast<float>(w) * k, static_cast<float>(h) * k };
            dxl_rect px = dxl_px(ui, at);
            SDL_RenderTexture(ui->renderer, tex, nullptr, &px);
        }

        void drawShotsPage(LauncherState& st, dxl_rect r)
        {
            dxl_ui* ui = &st.ui;
            const dxl_theme* t = &ui->theme;
            if (st.shotThumbs) {
                dxl_thumbs_update(st.shotThumbs, ui->renderer);
            }
            if (st.shotWhole) {
                dxl_thumbs_update(st.shotWhole, ui->renderer);
            }

            if (st.shotList.count == 0) {
                dxl_panel(ui, r);
                dxl_label(ui, r, DXL_FONT_BODY, tr("No screenshots in this profile yet.", "Nenhuma captura de tela neste perfil ainda."), t->help, DXL_W_CENTER | DXL_W_MAY_CUT);
                if (ui->in.back) {
                    ui->in.back = 0;
                    goPage(st, Page::Home);
                }
                return;
            }

            if (st.shotOpen && st.shotSel >= 0 && st.shotSel < st.shotList.count) {
                const dxl_shot* sh = &st.shotList.items[st.shotSel];
                dxl_rect bar = dxl_cut_bottom(&r, 46.0f);
                dxl_cut_bottom(&r, 8.0f);
                dxl_rect cap = dxl_cut_bottom(&r, 24.0f);
                dxl_cut_bottom(&r, 6.0f);
                drawPicture(st, st.shotWhole, sh->path, r);
                char when[64] {};
                dxl_shot_when(sh, when, sizeof(when));
                char text[320] {};
                std::snprintf(text, sizeof(text), "%s  \xc2\xb7  %s  \xc2\xb7  %d / %d", sh->name, when, st.shotSel + 1, st.shotList.count);
                dxl_label(ui, cap, DXL_FONT_SMALL, text, t->help, DXL_W_LEFT | DXL_W_MAY_CUT);

                float bw = std::min(180.0f, bar.w * 0.23f);
                if (dxl_button(ui, "shot prev", dxl_cut_left(&bar, bw), tr("Previous", "Anterior"), st.shotSel > 0 ? 0 : DXL_W_DISABLED) && st.shotSel > 0) {
                    st.shotSel--;
                }
                dxl_cut_left(&bar, 10.0f);
                if (dxl_button(ui, "shot next", dxl_cut_left(&bar, bw), tr("Next", "Próxima"), st.shotSel + 1 < st.shotList.count ? 0 : DXL_W_DISABLED) && st.shotSel + 1 < st.shotList.count) {
                    st.shotSel++;
                }
                dxl_cut_left(&bar, 10.0f);
                if (dxl_button(ui, "shot del", dxl_cut_left(&bar, bw), tr("Delete...", "Excluir..."), 0)) {
                    st.dialog = DialogKind::ConfirmDeleteShot;
                    st.deleteShotIndex = st.shotSel;
                    st.dialogTitle = tr("Delete Screenshot?", "Excluir Captura?");
                    st.dialogBody = sh->name;
                }
                dxl_cut_left(&bar, 10.0f);
                if (dxl_button(ui, "shot close", dxl_cut_left(&bar, bw), tr("Close", "Fechar"), 0) || ui->in.back) {
                    ui->in.back = 0;
                    st.shotOpen = false;
                }
                return;
            }

            const float sideW = std::min(280.0f, r.w * 0.30f);
            dxl_rect gridPanel { r.x, r.y, r.w - sideW - 18.0f, r.h };
            dxl_rect side { gridPanel.x + gridPanel.w + 18.0f, r.y, sideW, r.h };

            dxl_panel(ui, gridPanel);
            dxl_rect inside = dxl_inset(gridPanel, 10.0f);
            const int cols = 3;
            const int rowCount = (st.shotList.count + cols - 1) / cols;
            const float cellW = (inside.w - 18.0f) / static_cast<float>(cols);
            const float rowH = std::max(100.0f, cellW * 0.62f);

            dxl_list grid {};
            dxl_list_begin(ui, &grid, "shots grid", inside, rowCount, rowH, DXL_LIST_WHOLE_ROWS);
            for (int row = 0; row < rowCount; ++row) {
                dxl_rect rowRect {};
                if (!dxl_list_rect(&grid, row, &rowRect)) {
                    continue;
                }
                for (int c = 0; c < cols; ++c) {
                    int idx = row * cols + c;
                    if (idx >= st.shotList.count) {
                        break;
                    }
                    dxl_rect cell { rowRect.x + static_cast<float>(c) * cellW + 4.0f, rowRect.y + 4.0f, cellW - 8.0f, rowRect.h - 8.0f };
                    uint32_t cid = dxl_list_cell_id(&grid, row, c);
                    int hits = dxl_ui_focusable(ui, cid, cell, 0);
                    dxl_bevel_style stBox {
                        (hits & DXL_HIT_FOCUSED) ? t->face_focus_top : t->panel_top,
                        (hits & DXL_HIT_FOCUSED) ? t->face_focus_bottom : t->panel_bottom,
                        t->panel_light,
                        t->panel_shadow,
                        4.0f
                    };
                    dxl_bevel(ui, cell, &stBox);
                    drawPicture(st, st.shotThumbs, st.shotList.items[idx].path, dxl_inset(cell, 4.0f));
                    if (hits & DXL_HIT_FOCUSED) {
                        st.shotSel = idx;
                    }
                    if (hits & DXL_HIT_ACTIVATED) {
                        st.shotSel = idx;
                        st.shotOpen = true;
                    }
                }
            }
            dxl_list_end(ui, &grid);

            const dxl_shot* cur = (st.shotSel >= 0 && st.shotSel < st.shotList.count) ? &st.shotList.items[st.shotSel] : nullptr;
            if (cur) {
                char when[64] {};
                dxl_shot_when(cur, when, sizeof(when));
                dxl_field(ui, &side, 26.0f, 70.0f, tr("File", "Arquivo"), cur->name, t->text, DXL_W_MAY_CUT);
                dxl_field(ui, &side, 26.0f, 70.0f, tr("Taken", "Data"), when, t->help, DXL_W_MAY_CUT);
                dxl_cut_top(&side, 12.0f);
            }
            if (dxl_button(ui, "shot view", dxl_cut_top(&side, 44.0f), tr("View", "Visualizar"), DXL_W_PRIMARY) && cur) {
                st.shotOpen = true;
            }
            dxl_cut_top(&side, 10.0f);
            if (dxl_button(ui, "shot del side", dxl_cut_top(&side, 44.0f), tr("Delete...", "Excluir..."), cur ? 0 : DXL_W_DISABLED) && cur) {
                st.dialog = DialogKind::ConfirmDeleteShot;
                st.deleteShotIndex = st.shotSel;
                st.dialogTitle = tr("Delete Screenshot?", "Excluir Captura?");
                st.dialogBody = cur->name;
            }
            dxl_cut_top(&side, 10.0f);
            if (dxl_button(ui, "shot back", dxl_cut_top(&side, 44.0f), tr("Back", "Voltar"), 0) || ui->in.back) {
                ui->in.back = 0;
                goPage(st, Page::Home);
            }
        }

        // Helpers to read/write OpenDE's DeusEx.ini & User.ini cleanly
        int readSysInt(const char* sec, const char* key, int def)
        {
            if (auto v = dx::config::system().get(sec, key)) {
                return std::atoi(v->c_str());
            }
            return def;
        }
        bool readSysBool(const char* sec, const char* key, bool def)
        {
            if (auto v = dx::config::system().get(sec, key)) {
                return isTrueText(*v);
            }
            return def;
        }
        double readSysFloat(const char* sec, const char* key, double def)
        {
            if (auto v = dx::config::system().get(sec, key)) {
                return std::atof(v->c_str());
            }
            return def;
        }
        void writeSysInt(const char* sec, const char* key, int val)
        {
            dx::config::system().set(sec, key, std::to_string(val));
        }
        void writeSysBool(const char* sec, const char* key, bool val)
        {
            dx::config::system().set(sec, key, val ? "True" : "False");
        }
        void writeSysFloat(const char* sec, const char* key, double val, int decimals = 2)
        {
            char buf[32];
            if (decimals <= 1) {
                std::snprintf(buf, sizeof(buf), "%.1f", val);
            } else {
                std::snprintf(buf, sizeof(buf), "%.2f", val);
            }
            dx::config::system().set(sec, key, buf);
        }

        bool readUserBool(const char* sec, const char* key, bool def)
        {
            if (auto v = dx::config::user().get(sec, key)) {
                return isTrueText(*v);
            }
            return def;
        }
        double readUserFloat(const char* sec, const char* key, double def)
        {
            if (auto v = dx::config::user().get(sec, key)) {
                return std::atof(v->c_str());
            }
            return def;
        }
        void writeUserBool(const char* sec, const char* key, bool val)
        {
            dx::config::user().set(sec, key, val ? "True" : "False");
        }
        void writeUserFloat(const char* sec, const char* key, double val)
        {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.2f", val);
            dx::config::user().set(sec, key, buf);
        }

        // ---- 3D Controller Preview, Live Meters & Button Remapping ----

        constexpr const char* kPadBindSec = "OpenDE.GamepadBindings";
        constexpr const char* kPadSettingsSec = "DXController.ControllerSettings";

        struct PadActionDef {
            const char* command;
            const char* labelEn;
            const char* labelPt;
        };

        const PadActionDef kPadActions[] = {
            { "", "(nothing)", "(nenhum)" },
            { "Jump", "Jump", "Pular" },
            { "ParseRightClick", "Use / Interact", "Usar / Interagir" },
            { "ReloadWeapon", "Reload", "Recarregar" },
            { "SwitchAmmo", "Change Ammo", "Trocar Munição" },
            { "Fire", "Fire Weapon", "Atirar" },
            { "Duck", "Crouch", "Agachar" },
            { "Scope", "Toggle Scope", "Alternar Mira" },
            { "Laser", "Toggle Laser", "Alternar Laser" },
            { "LeanLeft", "Lean Left", "Inclinar Esq." },
            { "LeanRight", "Lean Right", "Inclinar Dir." },
            { "DropItem", "Drop Item", "Soltar Item" },
            { "PutInHand", "Put Away Item", "Guardar Item" },
            { "QuickSave", "Quick Save", "Salvar Rápido" },
            { "QuickLoad", "Quick Load", "Carregar Rápido" },
            { "TogglePlayerMenuWindow", "Inventory / Persona", "Inventário / Menu" },
            { "ShowInventoryWindow", "Inventory Screen", "Tela Inventário" },
            { "ShowAugmentationsWindow", "Augmentations Screen", "Tela Aprimoramentos" },
            { "ShowSkillsWindow", "Skills Screen", "Tela Habilidades" },
            { "ShowGoalsWindow", "Goals & Notes", "Objetivos e Notas" },
            { "ShowMainMenu", "Main Menu", "Menu Principal" },
            { "ActivateAllAugs", "Toggle All Augs", "Ativar Todos Augs" },
            { "ActivateBelt 1", "Belt Slot 1", "Cinto Slot 1" },
            { "ActivateBelt 2", "Belt Slot 2", "Cinto Slot 2" },
            { "ActivateBelt 3", "Belt Slot 3", "Cinto Slot 3" },
            { "ActivateBelt 4", "Belt Slot 4", "Cinto Slot 4" },
            { "ActivateBelt 5", "Belt Slot 5", "Cinto Slot 5" },
            { "ActivateBelt 6", "Belt Slot 6", "Cinto Slot 6" },
            { "ActivateBelt 7", "Belt Slot 7", "Cinto Slot 7" },
            { "ActivateBelt 8", "Belt Slot 8", "Cinto Slot 8" },
            { "ActivateBelt 9", "Belt Slot 9", "Cinto Slot 9" },
            { "ActivateBelt 0", "Belt Slot 0", "Cinto Slot 0" },
            { "NextBeltItem", "Next Belt Item", "Próximo do Cinto" },
            { "PrevBeltItem", "Prev Belt Item", "Anterior do Cinto" },
            { "ToggleWalk", "Walk / Run Toggle", "Andar / Correr" },
        };
        constexpr int kPadActionCount = static_cast<int>(sizeof(kPadActions) / sizeof(kPadActions[0]));

        struct PadButtonDef {
            dxl_padpart part;
            const char* joyKey;
            const char* defaultCmd;
        };

        const PadButtonDef kPadButtons[] = {
            { DXL_PADPART_SOUTH,   "Joy1",        "Jump" },
            { DXL_PADPART_EAST,    "Joy2",        "ReloadWeapon" },
            { DXL_PADPART_WEST,    "Joy3",        "ParseRightClick" },
            { DXL_PADPART_NORTH,   "Joy4",        "SwitchAmmo" },
            { DXL_PADPART_RSTICK,  "Joy10",       "" },
            { DXL_PADPART_BACK,    "Joy7",        "TogglePlayerMenuWindow" },
            { DXL_PADPART_START,   "Joy8",        "ShowMainMenu" },
            { DXL_PADPART_DPUP,    "JoyPovUp",    "ActivateBelt 1" },
            { DXL_PADPART_DPLEFT,  "JoyPovLeft",  "ActivateBelt 2" },
            { DXL_PADPART_DPRIGHT, "JoyPovRight", "ActivateBelt 3" },
            { DXL_PADPART_DPDOWN,  "JoyPovDown",  "ActivateBelt 4" },
        };
        constexpr int kPadButtonCount = static_cast<int>(sizeof(kPadButtons) / sizeof(kPadButtons[0]));

        int findPadButtonByPart(int part)
        {
            for (int i = 0; i < kPadButtonCount; ++i) {
                if (static_cast<int>(kPadButtons[i].part) == part) {
                    return i;
                }
            }
            return -1;
        }

        std::string getPadBindingCmd(const PadButtonDef& btn)
        {
            if (auto custom = dx::config::user().get(kPadBindSec, btn.joyKey)) {
                return *custom;
            }
            return btn.defaultCmd;
        }

        const char* describePadCmd(const std::string& cmd)
        {
            for (int i = 0; i < kPadActionCount; ++i) {
                if (iequalsStr(cmd, kPadActions[i].command)) {
                    return isPtLanguage() ? kPadActions[i].labelPt : kPadActions[i].labelEn;
                }
            }
            return cmd.empty() ? tr("(nothing)", "(nenhum)") : cmd.c_str();
        }

        const char* padPartLabel(dxl_pad_family fam, dxl_padpart part)
        {
            const bool ps = (fam == DXL_PADFAMILY_PLAYSTATION);
            const bool nin = (fam == DXL_PADFAMILY_NINTENDO);
            switch (part) {
            case DXL_PADPART_SOUTH:     return ps ? "Cross" : (nin ? "B" : "A");
            case DXL_PADPART_EAST:      return ps ? "Circle" : (nin ? "A" : "B");
            case DXL_PADPART_WEST:      return ps ? "Square" : (nin ? "Y" : "X");
            case DXL_PADPART_NORTH:     return ps ? "Triangle" : (nin ? "X" : "Y");
            case DXL_PADPART_LSHOULDER: return ps ? "L1" : (nin ? "L" : "LB");
            case DXL_PADPART_RSHOULDER: return ps ? "R1" : (nin ? "R" : "RB");
            case DXL_PADPART_LTRIGGER:  return ps ? "L2" : (nin ? "ZL" : "LT");
            case DXL_PADPART_RTRIGGER:  return ps ? "R2" : (nin ? "ZR" : "RT");
            case DXL_PADPART_BACK:      return ps ? "Share" : (nin ? "-" : "Back");
            case DXL_PADPART_START:     return ps ? "Options" : (nin ? "+" : "Start");
            case DXL_PADPART_LSTICK:    return "L3";
            case DXL_PADPART_RSTICK:    return "R3";
            case DXL_PADPART_DPUP:      return tr("D-Pad Up", "D-Pad Cima");
            case DXL_PADPART_DPDOWN:    return tr("D-Pad Down", "D-Pad Baixo");
            case DXL_PADPART_DPLEFT:    return tr("D-Pad Left", "D-Pad Esq.");
            case DXL_PADPART_DPRIGHT:   return tr("D-Pad Right", "D-Pad Dir.");
            case DXL_PADPART_BODY:
            case DXL_PADPART_GUIDE:
            case DXL_PADPART_TRIM:
            case DXL_PADPART_COUNT:
            default:                    return "";
            }
        }

        std::string padPartLeaderText(int part)
        {
            const int btnIdx = findPadButtonByPart(part);
            if (part == DXL_PADPART_LSTICK) {
                return tr("Moves; crouch", "Move; agacha");
            }
            if (part == DXL_PADPART_RSTICK) {
                std::string cmd = btnIdx >= 0 ? getPadBindingCmd(kPadButtons[btnIdx]) : std::string();
                if (cmd.empty()) {
                    return tr("Looks; no click", "Mira; sem clique");
                }
                return std::string(tr("Looks; ", "Mira; ")) + describePadCmd(cmd);
            }
            if (part == DXL_PADPART_LSHOULDER) {
                return tr("Weapon wheel", "Roda de armas");
            }
            if (part == DXL_PADPART_RSHOULDER) {
                return tr("Aug wheel", "Roda de augs");
            }
            if (part == DXL_PADPART_LTRIGGER) {
                return tr("Scope / laser", "Mira / laser");
            }
            if (part == DXL_PADPART_RTRIGGER) {
                return tr("Fire", "Atirar");
            }
            if (btnIdx >= 0) {
                std::string cmd = getPadBindingCmd(kPadButtons[btnIdx]);
                return cmd.empty() ? "\xe2\x80\x94" : std::string(describePadCmd(cmd));
            }
            return "\xe2\x80\x94";
        }

        struct PadDeviceInfo {
            SDL_JoystickID id = 0;
            bool isGamepad = false;
            std::string name;
        };

        std::vector<PadDeviceInfo> enumeratePads()
        {
            std::vector<PadDeviceInfo> out;
            int count = 0;
            SDL_JoystickID* ids = SDL_GetJoysticks(&count);
            if (!ids) {
                return out;
            }
            for (int i = 0; i < count; ++i) {
                PadDeviceInfo info;
                info.id = ids[i];
                info.isGamepad = SDL_IsGamepad(ids[i]);
                const char* nm = info.isGamepad ? SDL_GetGamepadNameForID(ids[i]) : SDL_GetJoystickNameForID(ids[i]);
                info.name = (nm && *nm) ? nm : "Controller";
                out.push_back(std::move(info));
            }
            SDL_free(ids);
            return out;
        }

        void drawRing(dxl_ui* ui, float cx, float cy, float radius, float width, dxl_rgba color)
        {
            constexpr int kSeg = 36;
            for (int i = 0; i < kSeg; ++i) {
                float a0 = static_cast<float>(i) / static_cast<float>(kSeg) * 6.2831853f;
                float a1 = static_cast<float>(i + 1) / static_cast<float>(kSeg) * 6.2831853f;
                dxl_line(ui, cx + std::cos(a0) * radius, cy + std::sin(a0) * radius,
                         cx + std::cos(a1) * radius, cy + std::sin(a1) * radius, width, color);
            }
        }

        void drawStickMeter(dxl_ui* ui, float cx, float cy, float radius, float x, float y, float dz, const char* label)
        {
            const dxl_theme* t = &ui->theme;
            dxl_disc(ui, cx, cy, radius, dxl_rgba_fade(t->panel_bottom, 0.9f));
            drawRing(ui, cx, cy, radius, 1.0f, dxl_rgba_fade(t->accent, 0.55f));
            if (dz > 0.01f) {
                drawRing(ui, cx, cy, radius * dz, 1.0f, dxl_rgba_fade(t->help, 0.7f));
            }
            float mag = std::sqrt(x * x + y * y);
            float gx = 0.0f;
            float gy = 0.0f;
            if (mag > dz && mag > 1e-4f) {
                float norm = std::clamp((mag - dz) / std::max(1.0f - dz, 0.01f), 0.0f, 1.0f);
                gx = (x / mag) * norm;
                gy = (y / mag) * norm;
            }
            dxl_disc(ui, cx + x * radius, cy + y * radius, 3.0f, dxl_rgba_fade(t->text, 0.55f));
            dxl_disc(ui, cx + gx * radius, cy + gy * radius, 4.0f, t->accent);
            dxl_rect lr { cx - 58.0f, cy + radius + 2.0f, 116.0f, 16.0f };
            dxl_label(ui, lr, DXL_FONT_SMALL, label, t->help, DXL_W_CENTER | DXL_W_MAY_CUT);
        }

        void drawTriggerMeter(dxl_ui* ui, dxl_rect r, float val, float threshold, const char* label)
        {
            const dxl_theme* t = &ui->theme;
            dxl_rect bar { r.x, r.y, r.w, r.h - 18.0f };
            dxl_fill(ui, bar, dxl_rgba_fade(t->panel_bottom, 0.9f));
            float clamped = std::clamp(val, 0.0f, 1.0f);
            bool active = clamped >= threshold;
            dxl_rect fill { bar.x, bar.y + bar.h * (1.0f - clamped), bar.w, bar.h * clamped };
            dxl_fill(ui, fill, active ? t->accent : dxl_rgba_fade(t->accent, 0.45f));
            float markY = bar.y + bar.h * (1.0f - std::clamp(threshold, 0.05f, 0.95f));
            dxl_line(ui, bar.x - 3.0f, markY, bar.x + bar.w + 3.0f, markY, 1.2f, t->text);
            dxl_rect lr { r.x - 20.0f, bar.y + bar.h + 2.0f, r.w + 40.0f, 16.0f };
            dxl_label(ui, lr, DXL_FONT_SMALL, label, t->help, DXL_W_CENTER | DXL_W_MAY_CUT);
        }

        void drawPad3DPanel(LauncherState& st, dxl_rect r, const std::vector<PadDeviceInfo>& pads, int shown,
                            dxl_pad_family fam, int chosen, bool testing)
        {
            dxl_ui* ui = &st.ui;
            const dxl_theme* t = &ui->theme;
            dxl_well(ui, r);
            dxl_rect in = dxl_inset(r, 10.0f);

            const bool hasPad = shown >= 0 && shown < static_cast<int>(pads.size());
            SDL_Gamepad* g = (hasPad && pads[static_cast<size_t>(shown)].isGamepad)
                ? SDL_GetGamepadFromID(pads[static_cast<size_t>(shown)].id) : nullptr;

            dxl_rect topRow = dxl_cut_top(&in, 26.0f);
            const char* padTitle = hasPad ? pads[static_cast<size_t>(shown)].name.c_str()
                                          : tr("No controller connected", "Nenhum controle conectado");
            dxl_label(ui, topRow, DXL_FONT_LABEL, padTitle, hasPad ? t->text : t->help, DXL_W_LEFT | DXL_W_MAY_CUT);

            const char* subLine = g
                ? tr("SDL3 mapping  \xc2\xb7  DXController + custom bindings  \xc2\xb7  drag to rotate",
                     "Mapeamento SDL3  \xc2\xb7  DXController + botões customizados  \xc2\xb7  arraste para girar")
                : tr("Showing Xbox layout  \xc2\xb7  connect a controller to test live  \xc2\xb7  drag to rotate",
                     "Exibindo layout Xbox  \xc2\xb7  conecte um controle para testar ao vivo  \xc2\xb7  arraste para girar");
            dxl_label(ui, dxl_cut_top(&in, 18.0f), DXL_FONT_SMALL, subLine, t->help, DXL_W_LEFT | DXL_W_MAY_CUT);
            dxl_cut_top(&in, 4.0f);

            dxl_pad3d_state pstate {};
            pstate.chosen = chosen;
            if (g) {
                for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT && b + 1 < DXL_PADPART_LTRIGGER; ++b) {
                    pstate.down[b + 1] = SDL_GetGamepadButton(g, static_cast<SDL_GamepadButton>(b)) ? 1 : 0;
                }
                pstate.lx = static_cast<float>(SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTX)) / 32767.0f;
                pstate.ly = static_cast<float>(SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTY)) / 32767.0f;
                pstate.rx = static_cast<float>(SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_RIGHTX)) / 32767.0f;
                pstate.ry = static_cast<float>(SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_RIGHTY)) / 32767.0f;
                pstate.lt = std::max(0.0f, static_cast<float>(SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFT_TRIGGER)) / 32767.0f);
                pstate.rt = std::max(0.0f, static_cast<float>(SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)) / 32767.0f);
            }

            constexpr float kStripH = 78.0f;
            dxl_rect strip = dxl_cut_bottom(&in, kStripH);

            constexpr float kLeaderGap = 16.5f;
            float labelW = std::clamp(in.w * 0.25f, 88.0f, 155.0f);
            float needH = std::max(dxl_pad3d_fit_height(std::max(120.0f, in.w - 2.0f * labelW)), 8.0f * kLeaderGap + 16.0f) + 12.0f;
            if (in.h > needH) {
                float spare = in.h - needH;
                in.y += spare * 0.5f;
                in.h = needH;
                strip.y -= spare * 0.5f;
            }

            float sway = ui->reduced_motion ? 0.0f : static_cast<float>(std::sin(ui->now * 0.6)) * 0.12f;
            if (!ui->reduced_motion || g != nullptr) {
                dxl_ui_animate(ui);
            }

            st.padModelRect = in;
            uint32_t modelId = dxl_ui_id(ui, testing ? "pad test model" : "pad model");
            int hits = dxl_ui_clickable(ui, modelId, in);
            if (ui->held == modelId) {
                st.padYaw += (ui->in.px - st.padDragX) * 0.01f;
                st.padPitch += (ui->in.py - st.padDragY) * 0.01f;
            }
            st.padDragX = ui->in.px;
            st.padDragY = ui->in.py;
            if (g && !testing && !ui->capture_id && !ui->text_id && !ui->pads_off) {
                if (std::fabs(pstate.rx) > 0.30f) {
                    st.padYaw += pstate.rx * ui->dt * 1.4f;
                }
            }
            st.padPitch = std::clamp(st.padPitch, -0.45f, 0.55f);
            pstate.yaw = st.padYaw + sway;
            pstate.pitch = 0.36f + st.padPitch;

            dxl_rect modelR { in.x + labelW, in.y, std::max(80.0f, in.w - 2.0f * labelW), in.h };
            if (st.padMeshFamily != static_cast<int>(fam)) {
                dxl_padmesh_free(&st.padMesh);
                dxl_padmesh_build(&st.padMesh, static_cast<int>(fam));
                st.padMeshFamily = static_cast<int>(fam);
            }
            dxl_pad3d_spot spots[DXL_PADPART_COUNT] {};
            dxl_pad3d_draw(ui, &st.padMesh, modelR, &pstate, spots);

            float trigThresh = std::clamp(static_cast<float>(readSysInt(kPadSettingsSec, "TriggerThreshold", 64)) / 255.0f, 0.05f, 0.90f);
            int pressed[DXL_PADPART_COUNT] {};
            for (int p = 0; p < DXL_PADPART_COUNT; ++p) {
                pressed[p] = pstate.down[p];
            }
            pressed[DXL_PADPART_LTRIGGER] = pstate.lt >= trigThresh ? 1 : 0;
            pressed[DXL_PADPART_RTRIGGER] = pstate.rt >= trigThresh ? 1 : 0;
            pressed[DXL_PADPART_LSTICK] = (pressed[DXL_PADPART_LSTICK] || std::fabs(pstate.lx) > 0.30f || std::fabs(pstate.ly) > 0.30f) ? 1 : 0;
            pressed[DXL_PADPART_RSTICK] = (pressed[DXL_PADPART_RSTICK] || std::fabs(pstate.rx) > 0.30f || std::fabs(pstate.ry) > 0.30f) ? 1 : 0;

            // Leader callout lines on left & right of 3D controller
            struct LeaderItem {
                int part = 0;
                float y = 0.0f;
                std::string text;
            };
            std::vector<LeaderItem> sides[2];
            float midX = modelR.x + modelR.w * 0.5f;
            for (int p = 0; p < DXL_PADPART_COUNT; ++p) {
                if (!spots[p].seen || p == DXL_PADPART_GUIDE) {
                    continue;
                }
                int sideIdx = spots[p].x > midX ? 1 : 0;
                sides[sideIdx].push_back({ p, spots[p].y, padPartLeaderText(p) });
            }
            for (int sIdx = 0; sIdx < 2; ++sIdx) {
                auto& items = sides[sIdx];
                if (!items.empty() && static_cast<float>(items.size() - 1) * kLeaderGap + 16.0f > in.h) {
                    std::vector<LeaderItem> kept;
                    for (const auto& it : items) {
                        if (it.part == chosen || pressed[it.part]) {
                            kept.push_back(it);
                        }
                    }
                    items = std::move(kept);
                }
                std::sort(items.begin(), items.end(), [](const LeaderItem& a, const LeaderItem& b) {
                    return a.y < b.y;
                });
                for (size_t idx = 0; idx < items.size(); ++idx) {
                    if (items[idx].y < in.y + 8.0f) {
                        items[idx].y = in.y + 8.0f;
                    }
                    if (idx > 0 && items[idx].y < items[idx - 1].y + kLeaderGap) {
                        items[idx].y = items[idx - 1].y + kLeaderGap;
                    }
                }
                if (!items.empty() && items.back().y > in.y + in.h - 8.0f) {
                    float shiftUp = items.back().y - (in.y + in.h - 8.0f);
                    for (auto& it : items) {
                        it.y -= shiftUp;
                    }
                }
                for (const auto& it : items) {
                    bool lit = (it.part == chosen) || (pressed[it.part] != 0);
                    dxl_rgba col = lit ? t->text_focus : dxl_rgba_fade(t->help, 0.90f);
                    float edgeX = sIdx ? (modelR.x + modelR.w) : modelR.x;
                    dxl_line(ui, spots[it.part].x, spots[it.part].y, edgeX, it.y, lit ? 1.5f : 1.0f, dxl_rgba_fade(col, lit ? 0.95f : 0.65f));
                    dxl_disc(ui, spots[it.part].x, spots[it.part].y, lit ? 3.2f : 2.3f, col);
                    dxl_rect lr = sIdx
                        ? dxl_rect{ edgeX + 4.0f, it.y - 8.0f, in.x + in.w - edgeX - 4.0f, 16.0f }
                        : dxl_rect{ in.x, it.y - 8.0f, edgeX - in.x - 4.0f, 16.0f };
                    dxl_label(ui, lr, DXL_FONT_SMALL, it.text.c_str(), col, (sIdx ? DXL_W_LEFT : DXL_W_RIGHT) | DXL_W_MAY_CUT);
                }
            }

            // Clicking/tapping a button directly on the 3D controller selects its binding row
            if (!testing && (hits & DXL_HIT_ACTIVATED)) {
                int hitPart = dxl_pad3d_hit(&st.padMesh, spots, ui->in.px, ui->in.py);
                if (hitPart >= 0) {
                    if (findPadButtonByPart(hitPart) >= 0) {
                        st.padFindPart = hitPart;
                    } else if (hitPart != DXL_PADPART_BODY && hitPart != DXL_PADPART_TRIM && hitPart != DXL_PADPART_GUIDE) {
                        showToast(st, std::string(padPartLabel(fam, static_cast<dxl_padpart>(hitPart))) + ": "
                                      + padPartLeaderText(hitPart) + tr(" (DXController)", " (DXController)"));
                    }
                }
            }

            // Bottom live analog stick & trigger meters
            float dzL = std::clamp(static_cast<float>(readSysInt(kPadSettingsSec, "StickDeadzoneLeft", 5200)) / 32767.0f, 0.0f, 0.45f);
            float dzR = std::clamp(static_cast<float>(readSysInt(kPadSettingsSec, "StickDeadzoneRight", 5200)) / 32767.0f, 0.0f, 0.45f);
            float stickR = std::max(16.0f, (strip.h - 22.0f) * 0.5f);
            float stickCy = strip.y + stickR + 2.0f;
            drawStickMeter(ui, strip.x + strip.w * 0.17f, stickCy, stickR, pstate.lx, pstate.ly, dzL, tr("Left stick", "Analógico esq."));
            drawStickMeter(ui, strip.x + strip.w * 0.83f, stickCy, stickR, pstate.rx, pstate.ry, dzR, tr("Right stick", "Analógico dir."));
            drawTriggerMeter(ui, dxl_rect{ strip.x + strip.w * 0.41f - 8.0f, strip.y + 2.0f, 16.0f, strip.h - 4.0f },
                             pstate.lt, trigThresh, padPartLabel(fam, DXL_PADPART_LTRIGGER));
            drawTriggerMeter(ui, dxl_rect{ strip.x + strip.w * 0.59f - 8.0f, strip.y + 2.0f, 16.0f, strip.h - 4.0f },
                             pstate.rt, trigThresh, padPartLabel(fam, DXL_PADPART_RTRIGGER));
        }

        void drawControllerTab(LauncherState& st, dxl_rect area, dxl_rect& btnBar)
        {
            dxl_ui* ui = &st.ui;
            const dxl_theme* t = &ui->theme;

            std::vector<PadDeviceInfo> pads = enumeratePads();
            int shown = -1;
            for (int i = 0; i < static_cast<int>(pads.size()); ++i) {
                if (pads[static_cast<size_t>(i)].id == st.padSelectedId) {
                    shown = i;
                    break;
                }
            }
            if (shown < 0) {
                for (int i = 0; i < static_cast<int>(pads.size()); ++i) {
                    if (pads[static_cast<size_t>(i)].isGamepad) {
                        shown = i;
                        break;
                    }
                }
            }
            if (shown < 0 && !pads.empty()) {
                shown = 0;
            }
            if (shown >= 0) {
                st.padSelectedId = pads[static_cast<size_t>(shown)].id;
            }

            SDL_Gamepad* g = (shown >= 0 && pads[static_cast<size_t>(shown)].isGamepad)
                ? SDL_GetGamepadFromID(pads[static_cast<size_t>(shown)].id) : nullptr;
            dxl_pad_family fam = g ? dxl_ui_pad_family(g) : DXL_PADFAMILY_XBOX;

            float leftW = std::floor(area.w * 0.54f);
            dxl_rect leftR { area.x, area.y, leftW, area.h };
            dxl_rect rightR { area.x + leftW + 12.0f, area.y, area.w - leftW - 12.0f, area.h };

            // Rows: [0] Test controller, [1] Find button, [2..12] 11 buttons, [13..20] 8 tuning controls
            constexpr int kFirstButtonRow = 2;
            constexpr int kFirstTuneRow = kFirstButtonRow + kPadButtonCount;
            constexpr int kTotalRows = kFirstTuneRow + 8;

            dxl_well(ui, rightR);
            dxl_list list {};
            dxl_list_begin(ui, &list, "pad rows", dxl_inset(rightR, 4.0f), kTotalRows, 44.0f, DXL_LIST_WHOLE_ROWS);

            if (st.padFindPart >= 0) {
                int bIdx = findPadButtonByPart(st.padFindPart);
                if (bIdx >= 0) {
                    dxl_list_focus(ui, &list, kFirstButtonRow + bIdx);
                }
                st.padFindPart = -1;
            }

            int chosenPart = -1;
            auto padRowHit = [&](int rowIdx, const char* helpText) -> dxl_rect {
                dxl_rect rowR {};
                if (dxl_list_rect(&list, rowIdx, &rowR)) {
                    uint32_t rid = dxl_list_row_id(&list, rowIdx);
                    if (ui->focus == rid || ui->hot == rid) {
                        st.settingsSelectedRow = rowIdx;
                        st.settingsHelpText = helpText;
                    }
                    return rowR;
                }
                return dxl_rect{ 0.0f, 0.0f, 0.0f, 0.0f };
            };

            // Row 0: Test controller modal
            if (dxl_rect rowR = padRowHit(0, tr("Open full-size 3D controller tester with live button, stick and trigger rumble feedback.",
                                                "Abre o testador 3D em tela cheia com resposta ao vivo dos botões, analógicos e vibração nos gatilhos.")); rowR.w > 0.0f) {
                if (dxl_button_id(ui, dxl_list_row_id(&list, 0), dxl_inset(rowR, 3.0f), tr("Test the controller...", "Testar o controle..."), 0)) {
                    if (g) {
                        st.padTestId = pads[static_cast<size_t>(shown)].id;
                        st.padTestHoldSince = -1.0;
                        st.padTestRumbling = false;
                        st.ui.pads_off = 1;
                        st.dialog = DialogKind::PadTest;
                        dxl_ui_forget(ui);
                    } else {
                        showToast(st, tr("No controller is connected.", "Nenhum controle conectado."));
                    }
                }
            }

            // Row 1: Find a button by pressing it
            if (dxl_rect rowR = padRowHit(1, tr("Activate and press any button on your controller to jump straight to its binding.",
                                                "Ative e pressione qualquer botão no controle para ir direto à configuração dele.")); rowR.w > 0.0f) {
                uint32_t id = dxl_list_row_id(&list, 1);
                bool capturing = (ui->capture_id == id);
                if (capturing && ui->in.captured == DXL_CAPTURED_PAD) {
                    int b = ui->in.captured_code;
                    int part = (b >= 0 && b < SDL_GAMEPAD_BUTTON_MISC1) ? (b + 1) : -1;
                    if (part >= 0 && findPadButtonByPart(part) >= 0) {
                        st.padFindPart = part;
                    } else if (part >= 0) {
                        showToast(st, std::string(padPartLabel(fam, static_cast<dxl_padpart>(part))) + ": " + padPartLeaderText(part) + " (DXController)");
                    }
                    dxl_ui_capture_end(ui);
                } else if (capturing && ui->in.captured == DXL_CAPTURED_TRIGGER) {
                    int trigPart = ui->in.captured_code ? DXL_PADPART_RTRIGGER : DXL_PADPART_LTRIGGER;
                    showToast(st, std::string(padPartLabel(fam, static_cast<dxl_padpart>(trigPart))) + ": " + padPartLeaderText(trigPart) + " (DXController)");
                    dxl_ui_capture_end(ui);
                } else if (capturing && (ui->in.captured || ui->in.back)) {
                    dxl_ui_capture_end(ui);
                    ui->in.back = 0;
                }
                int hits = dxl_ui_focusable(ui, id, rowR, 0);
                if (hits & DXL_HIT_FOCUSED) {
                    dxl_fill(ui, rowR, t->row_focus);
                } else if (hits & DXL_HIT_HOVERED) {
                    dxl_fill(ui, rowR, t->row_highlight);
                }
                const char* capText = capturing
                    ? tr("Press a button on the controller...", "Pressione um botão no controle...")
                    : tr("Find a button: press it", "Encontrar botão: pressione");
                dxl_rgba capCol = capturing ? t->accent : ((hits & DXL_HIT_FOCUSED) ? t->text_focus : t->text);
                dxl_label(ui, dxl_rect{ rowR.x + 14.0f, rowR.y, rowR.w - 28.0f, rowR.h }, DXL_FONT_BODY, capText, capCol, DXL_W_LEFT | DXL_W_MAY_CUT);
                if ((hits & DXL_HIT_ACTIVATED) && !capturing) {
                    dxl_ui_capture_begin(ui, id);
                }
            }

            // Rows 2..12: Remappable gamepad buttons
            for (int b = 0; b < kPadButtonCount; ++b) {
                int rowIdx = kFirstButtonRow + b;
                const PadButtonDef& def = kPadButtons[b];
                dxl_rect rowR = padRowHit(rowIdx, tr("Press A / tap to change what this button does in gameplay (highlighted on the 3D controller).",
                                                     "Pressione A / toque para trocar a ação deste botão no jogo (destacado no controle 3D)."));
                if (rowR.w <= 0.0f) {
                    continue;
                }
                uint32_t id = dxl_list_row_id(&list, rowIdx);
                int hits = dxl_ui_focusable(ui, id, rowR, 0);
                bool lit = (hits & DXL_HIT_FOCUSED) != 0;
                if (lit) {
                    chosenPart = static_cast<int>(def.part);
                    dxl_fill(ui, rowR, t->row_focus);
                } else if (hits & DXL_HIT_HOVERED) {
                    chosenPart = static_cast<int>(def.part);
                    dxl_fill(ui, rowR, t->row_highlight);
                }
                const char* btnLabel = padPartLabel(fam, def.part);
                std::string curCmd = getPadBindingCmd(def);
                const char* actionLabel = describePadCmd(curCmd);
                float lw = std::min(150.0f, rowR.w * 0.42f);
                dxl_label(ui, dxl_rect{ rowR.x + 14.0f, rowR.y, lw - 14.0f, rowR.h }, DXL_FONT_BODY, btnLabel, lit ? t->text_focus : t->text, DXL_W_LEFT | DXL_W_MAY_CUT);
                dxl_label(ui, dxl_rect{ rowR.x + lw + 6.0f, rowR.y, rowR.w - lw - 20.0f, rowR.h }, DXL_FONT_BODY, actionLabel, lit ? t->text_focus : t->help, DXL_W_RIGHT | DXL_W_MAY_CUT);

                if (hits & DXL_HIT_ACTIVATED) {
                    st.padPickKey = def.joyKey;
                    st.padPickLabel = btnLabel;
                    st.padPickPart = static_cast<int>(def.part);
                    st.dialog = DialogKind::PadPickAction;
                    dxl_ui_forget(ui);
                }
            }

            // Rows 13..20: Tuning controls ([DXController.ControllerSettings])
            static const char* const curveNames[] = { "Linear", "Power", "Expo", "Sigmoid" };
            auto curveIdxOf = [&](const char* key) {
                std::string val = dx::config::system().get(kPadSettingsSec, key).value_or("Sigmoid");
                for (int i = 0; i < 4; ++i) {
                    if (iequalsStr(val, curveNames[i])) return i;
                }
                return 3;
            };
            double lookScale = std::clamp(readSysFloat(kPadSettingsSec, "StickScaleRight", 0.50), 0.10, 1.00);
            double dzL = std::clamp(static_cast<double>(readSysInt(kPadSettingsSec, "StickDeadzoneLeft", 5200)) / 32767.0, 0.0, 0.45);
            double dzR = std::clamp(static_cast<double>(readSysInt(kPadSettingsSec, "StickDeadzoneRight", 5200)) / 32767.0, 0.0, 0.45);
            double trig = std::clamp(static_cast<double>(readSysInt(kPadSettingsSec, "TriggerThreshold", 64)) / 255.0, 0.05, 0.90);
            double pwrR = std::clamp(readSysFloat(kPadSettingsSec, "StickCurvePowerRight", 2.0), 1.0, 4.0);
            char scBuf[16], dlBuf[16], drBuf[16], trBuf[16], pwBuf[16];
            std::snprintf(scBuf, sizeof(scBuf), "%.0f%%", lookScale * 100.0);
            std::snprintf(dlBuf, sizeof(dlBuf), "%.0f%%", dzL * 100.0);
            std::snprintf(drBuf, sizeof(drBuf), "%.0f%%", dzR * 100.0);
            std::snprintf(trBuf, sizeof(trBuf), "%.0f%%", trig * 100.0);
            std::snprintf(pwBuf, sizeof(pwBuf), "%.1f", pwrR);

            auto tuneSlider = [&](int offset, const char* label, const char* help, double curVal, double minV, double maxV, double stepV, const char* shownVal, const auto& setter) {
                dxl_rect rowR = padRowHit(kFirstTuneRow + offset, help);
                if (rowR.w <= 0.0f) return;
                double v = curVal;
                if (dxl_slider(ui, dxl_list_row_id(&list, kFirstTuneRow + offset), rowR, label, &v, minV, maxV, stepV, shownVal, 0)) {
                    setter(v);
                }
            };
            auto tuneSpinner = [&](int offset, const char* label, const char* help, int curIdx, const char* const* choices, int countChoices, const auto& setter) {
                dxl_rect rowR = padRowHit(kFirstTuneRow + offset, help);
                if (rowR.w <= 0.0f) return;
                int v = std::clamp(curIdx, 0, countChoices - 1);
                int act = dxl_spinner(ui, dxl_list_row_id(&list, kFirstTuneRow + offset), rowR, label, &v, choices, countChoices, 0);
                if (act == DXL_SPIN_CHANGED) {
                    setter(v);
                } else if (act == DXL_SPIN_LIST && countChoices > 1) {
                    v = (v + 1) % countChoices;
                    setter(v);
                    ui->cues |= DXL_CUE_SLIDE;
                }
            };
            auto tuneToggle = [&](int offset, const char* label, const char* help, bool cur, const auto& setter) {
                dxl_rect rowR = padRowHit(kFirstTuneRow + offset, help);
                if (rowR.w <= 0.0f) return;
                int v = cur ? 1 : 0;
                if (dxl_toggle(ui, dxl_list_row_id(&list, kFirstTuneRow + offset), rowR, label, &v, 0)) {
                    setter(v != 0);
                }
            };

            tuneSlider(0, tr("Right Stick Look Speed", "Sensib. Analógico Dir."), tr("Look sensitivity multiplier for right stick.", "Multiplicador de mira do analógico direito."), lookScale, 0.10, 1.00, 0.05, scBuf, [&](double v) {
                writeSysFloat(kPadSettingsSec, "StickScaleRight", v);
            });
            tuneSlider(1, tr("Left Deadzone", "Zona Morta Esq."), tr("Inner deadzone for movement stick (shown on left meter).", "Zona morta do analógico esquerdo (visível no medidor)."), dzL, 0.0, 0.45, 0.02, dlBuf, [&](double v) {
                writeSysInt(kPadSettingsSec, "StickDeadzoneLeft", static_cast<int>(std::lround(v * 32767.0)));
            });
            tuneSlider(2, tr("Right Deadzone", "Zona Morta Dir."), tr("Inner deadzone for look stick (shown on right meter).", "Zona morta do analógico direito (visível no medidor)."), dzR, 0.0, 0.45, 0.02, drBuf, [&](double v) {
                writeSysInt(kPadSettingsSec, "StickDeadzoneRight", static_cast<int>(std::lround(v * 32767.0)));
            });
            tuneSpinner(3, tr("Left Stick Curve", "Curva Analógico Esq."), tr("Response curve for movement.", "Curva de resposta do analógico esquerdo."), curveIdxOf("StickCurveLeft"), curveNames, 4, [&](int idx) {
                dx::config::system().set(kPadSettingsSec, "StickCurveLeft", curveNames[idx]);
            });
            tuneSpinner(4, tr("Right Stick Curve", "Curva Analógico Dir."), tr("Response curve for aiming.", "Curva de resposta da mira."), curveIdxOf("StickCurveRight"), curveNames, 4, [&](int idx) {
                dx::config::system().set(kPadSettingsSec, "StickCurveRight", curveNames[idx]);
            });
            tuneSlider(5, tr("Right Curve Power", "Expoente Curva Dir."), tr("Power curve steepness.", "Intensidade da curva Power."), pwrR, 1.0, 4.0, 0.2, pwBuf, [&](double v) {
                writeSysFloat(kPadSettingsSec, "StickCurvePowerRight", v, 1);
            });
            tuneSlider(6, tr("Trigger Threshold", "Limiar dos Gatilhos"), tr("How far LT/RT must be pulled (marked on trigger bars).", "Curso mínimo para acionar LT/RT (marcado nas barras)."), trig, 0.05, 0.90, 0.05, trBuf, [&](double v) {
                writeSysInt(kPadSettingsSec, "TriggerThreshold", static_cast<int>(std::lround(v * 255.0)));
            });
            tuneToggle(7, tr("Invert Controller Y", "Inverter Y Controle"), tr("Inverts right stick vertical look.", "Inverte o eixo vertical do analógico direito."), readSysBool(kPadSettingsSec, "InvertLookY", false), [&](bool v) {
                writeSysBool(kPadSettingsSec, "InvertLookY", v);
            });

            dxl_list_end(ui, &list);

            if (chosenPart != st.padChosenPart) {
                st.padChosenPart = chosenPart;
                if (g && chosenPart >= 0) {
                    SDL_RumbleGamepad(g, 0x2000, 0x4000, 70);
                }
            }

            drawPad3DPanel(st, leftR, pads, shown, fam, chosenPart, false);

            if (dxl_button(ui, "pad reset", dxl_cut_left(&btnBar, 240.0f), tr("Back to defaults", "Restaurar padrões"), 0)) {
                dx::config::user().clearSection(kPadBindSec);
                dx::config::user().save();
                showToast(st, tr("Controller bindings reset to DXController defaults.", "Botões restaurados para o padrão do DXController."));
            }
        }

        void drawSettingsPage(LauncherState& st, dxl_rect r)
        {
            dxl_ui* ui = &st.ui;
            const dxl_theme* t = &ui->theme;

            const char* tabLabelsEn[] = { "Graphics", "Display", "Audio", "Touch & Gyro", "Controller", "Gameplay & HUD", "Launcher" };
            const char* tabLabelsPt[] = { "Gráficos", "Tela", "Áudio", "Touch & Gyro", "Controle", "HUD e Jogo", "Launcher" };
            const char** tabs = isPtLanguage() ? tabLabelsPt : tabLabelsEn;
            constexpr int kTabCount = 7;

            dxl_rect tabRect = dxl_cut_top(&r, 38.0f);
            if (dxl_tabs(ui, "settings tabs", tabRect, tabs, kTabCount, &st.settingsTab)) {
                st.settingsSelectedRow = 0;
                st.settingsHelpText.clear();
            }
            dxl_cut_top(&r, 8.0f);

            dxl_rect btnBar = dxl_cut_bottom(&r, 44.0f);
            dxl_cut_bottom(&r, 8.0f);
            dxl_rect helpBox = dxl_cut_bottom(&r, 42.0f);
            dxl_cut_bottom(&r, 8.0f);

            if (st.settingsTab == 4) {
                drawControllerTab(st, r, btnBar);

                // Help bar
                dxl_panel(ui, helpBox);
                if (!st.settingsHelpText.empty()) {
                    dxl_label(ui, dxl_inset(helpBox, 10.0f), DXL_FONT_SMALL, st.settingsHelpText.c_str(), t->help, DXL_W_LEFT | DXL_W_MAY_CUT);
                }

                if (dxl_button(ui, "set done", dxl_cut_right(&btnBar, 160.0f), tr("Done", "Concluir"), DXL_W_PRIMARY) || ui->in.back) {
                    ui->in.back = 0;
                    goPage(st, Page::Home);
                }
                return;
            }

            dxl_panel(ui, r);
            dxl_rect inside = dxl_inset(r, 10.0f);

            std::string vidSec = videoSection();
            std::string renSec = renderSection();
            std::string audSec = audioSection();

            // Each tab declares rows dynamically using a small lambda helper inside a fixed-count dxl_list
            const int rowCounts[] = { 12, 6, 6, 8, 8, 12, 3 };
            int count = rowCounts[std::clamp(st.settingsTab, 0, kTabCount - 1)];

            dxl_list list {};
            dxl_list_begin(ui, &list, "settings list", inside, count, 46.0f, DXL_LIST_WHOLE_ROWS);

            auto rowHit = [&](int rowIdx, const char* helpText) -> dxl_rect {
                dxl_rect rowR {};
                if (dxl_list_rect(&list, rowIdx, &rowR)) {
                    uint32_t rid = dxl_list_row_id(&list, rowIdx);
                    if (ui->focus == rid || ui->hot == rid) {
                        st.settingsSelectedRow = rowIdx;
                        st.settingsHelpText = helpText;
                    }
                    return rowR;
                }
                return dxl_rect{ 0.0f, 0.0f, 0.0f, 0.0f };
            };

            auto doToggle = [&](int idx, const char* label, const char* help, bool cur, const auto& setter) {
                dxl_rect rowR = rowHit(idx, help);
                if (rowR.w <= 0.0f) return;
                int v = cur ? 1 : 0;
                if (dxl_toggle(ui, dxl_list_row_id(&list, idx), rowR, label, &v, 0)) {
                    setter(v != 0);
                }
            };
            auto doSpinner = [&](int idx, const char* label, const char* help, int curIdx, const char* const* choices, int n, const auto& setter) {
                dxl_rect rowR = rowHit(idx, help);
                if (rowR.w <= 0.0f) return;
                int idxVal = std::clamp(curIdx, 0, n - 1);
                int act = dxl_spinner(ui, dxl_list_row_id(&list, idx), rowR, label, &idxVal, choices, n, 0);
                if (act == DXL_SPIN_CHANGED) {
                    setter(idxVal);
                } else if (act == DXL_SPIN_LIST && n > 1) {
                    idxVal = (idxVal + 1) % n;
                    setter(idxVal);
                    ui->cues |= DXL_CUE_SLIDE;
                }
            };
            auto doSlider = [&](int idx, const char* label, const char* help, double curVal, double minV, double maxV, double stepV, const char* shown, const auto& setter) {
                dxl_rect rowR = rowHit(idx, help);
                if (rowR.w <= 0.0f) return;
                double v = curVal;
                if (dxl_slider(ui, dxl_list_row_id(&list, idx), rowR, label, &v, minV, maxV, stepV, shown, 0)) {
                    setter(v);
                }
            };

            switch (st.settingsTab) {
            case 0: { // Graphics
                static const char* const msaaLabels[] = { "Off", "2x", "4x", "8x" };
                static const char* const anisoLabels[] = { "Off", "2x", "4x", "8x", "16x" };
                static const char* const aoLabelsEn[] = { "Off", "Low", "Medium", "High" };
                static const char* const aoLabelsPt[] = { "Desl.", "Baixo", "Médio", "Alto" };
                static const char* const detLabelsEn[] = { "Normal", "Far", "Very Far" };
                static const char* const detLabelsPt[] = { "Normal", "Longe", "Muito Longe" };
                static const char* const presetLabels[] = { "As in 2000", "Enhanced", "Performance", "Custom" };

                int curMsaa = std::clamp(readSysInt(vidSec.c_str(), "Multisampling", 0), 0, 3);
                int curAniso = std::clamp(readSysInt(vidSec.c_str(), "Anisotropy", 0), 0, 4);
                bool curFxaa = readSysBool(vidSec.c_str(), "FXAA", false);
                bool curTri = readSysBool(vidSec.c_str(), "UseTrilinear", false);
                bool curSmooth = readSysBool(vidSec.c_str(), "SmoothLightmaps", false);
                int curAo = std::clamp(readSysInt(vidSec.c_str(), "AmbientOcclusion", 0), 0, 3);
                int curDet = std::clamp(readSysInt(vidSec.c_str(), "DetailDistance", 0), 0, 2);
                int presetIdx = 3;
                if (curMsaa == 0 && !curFxaa && curAo == 0 && curAniso == 0) presetIdx = 2;
                else if (curMsaa == 2 && curFxaa && curAo >= 2 && curAniso >= 2) presetIdx = 1;
                else if (curMsaa == 0 && !curFxaa && curAo == 0 && curDet == 0) presetIdx = 0;

                doSpinner(0, tr("Preset", "Predefinição"), tr("Quickly applies a visual quality profile.", "Aplica rapidamente um perfil visual."), presetIdx, presetLabels, 4, [&](int v) {
                    if (v == 0) {
                        writeSysInt(vidSec.c_str(), "Multisampling", 0);
                        writeSysInt(vidSec.c_str(), "Anisotropy", 0);
                        writeSysBool(vidSec.c_str(), "FXAA", false);
                        writeSysBool(vidSec.c_str(), "SmoothLightmaps", false);
                        writeSysInt(vidSec.c_str(), "AmbientOcclusion", 0);
                        writeSysInt(vidSec.c_str(), "DetailDistance", 0);
                        writeSysBool(renSec.c_str(), "DetailTextures", true);
                    } else if (v == 1) {
                        writeSysInt(vidSec.c_str(), "Multisampling", 2);
                        writeSysInt(vidSec.c_str(), "Anisotropy", 3);
                        writeSysBool(vidSec.c_str(), "FXAA", true);
                        writeSysBool(vidSec.c_str(), "UseTrilinear", true);
                        writeSysBool(vidSec.c_str(), "SmoothLightmaps", true);
                        writeSysInt(vidSec.c_str(), "AmbientOcclusion", 2);
                        writeSysInt(vidSec.c_str(), "DetailDistance", 1);
                        writeSysBool(renSec.c_str(), "DetailTextures", true);
                    } else if (v == 2) {
                        writeSysInt(vidSec.c_str(), "Multisampling", 0);
                        writeSysInt(vidSec.c_str(), "Anisotropy", 0);
                        writeSysBool(vidSec.c_str(), "FXAA", false);
                        writeSysBool(vidSec.c_str(), "SmoothLightmaps", false);
                        writeSysInt(vidSec.c_str(), "AmbientOcclusion", 0);
                        writeSysInt(vidSec.c_str(), "DetailDistance", 0);
                    }
                });
                doSpinner(1, tr("Multisample AA (MSAA)", "Anti-Aliasing MSAA"), tr("Smooths geometry edges. Higher costs GPU.", "Suaviza bordas da geometria."), curMsaa, msaaLabels, 4, [&](int v) {
                    writeSysInt(vidSec.c_str(), "Multisampling", v);
                });
                doToggle(2, "FXAA", tr("Fast post-process edge smoothing (includes fences).", "Suavização rápida pós-processamento."), curFxaa, [&](bool v) {
                    writeSysBool(vidSec.c_str(), "FXAA", v);
                });
                doSpinner(3, tr("Anisotropic Filtering", "Filtragem Anisotrópica"), tr("Sharpens angled floor and wall textures.", "Nitidez em texturas em ângulo."), curAniso, anisoLabels, 5, [&](int v) {
                    writeSysInt(vidSec.c_str(), "Anisotropy", v);
                });
                doToggle(4, tr("Trilinear Filtering", "Filtragem Trilinear"), tr("Smooths mip transitions on surfaces.", "Suaviza transições de mipmaps."), curTri, [&](bool v) {
                    writeSysBool(vidSec.c_str(), "UseTrilinear", v);
                    writeSysBool(renSec.c_str(), "UseTrilinear", v);
                });
                doToggle(5, tr("Smooth Lighting", "Iluminação Suave"), tr("Bicubic filter on world lightmaps.", "Suaviza sombras de lightmaps."), curSmooth, [&](bool v) {
                    writeSysBool(vidSec.c_str(), "SmoothLightmaps", v);
                });
                doSpinner(6, tr("Ambient Occlusion", "Oclusão de Ambiente"), tr("Soft contact shadows under actors and props.", "Sombras de contato suaves."), curAo, isPtLanguage() ? aoLabelsPt : aoLabelsEn, 4, [&](int v) {
                    writeSysInt(vidSec.c_str(), "AmbientOcclusion", v);
                });
                doSpinner(7, tr("Detail Distance", "Distância de Detalhe"), tr("Distance where detail textures remain visible.", "Alcance das texturas de detalhe."), curDet, isPtLanguage() ? detLabelsPt : detLabelsEn, 3, [&](int v) {
                    writeSysInt(vidSec.c_str(), "DetailDistance", v);
                });
                doToggle(8, tr("Detail Textures", "Texturas de Detalhe"), tr("Fine surface grain on nearby walls and floors.", "Grão de detalhe em superfícies próximas."), readSysBool(renSec.c_str(), "DetailTextures", true), [&](bool v) {
                    writeSysBool(renSec.c_str(), "DetailTextures", v);
                });
                doToggle(9, tr("Volumetric Lighting", "Luz Volumétrica"), tr("Fog glow effects in lit areas.", "Efeitos de névoa e iluminação volumétrica."), readSysBool(renSec.c_str(), "VolumetricLighting", true), [&](bool v) {
                    writeSysBool(renSec.c_str(), "VolumetricLighting", v);
                });
                doToggle(10, tr("Decals", "Marcas / Decals"), tr("Bullet holes, scorch marks and blood.", "Marcas de bala e sangue."), readSysBool(vidSec.c_str(), "Decals", true), [&](bool v) {
                    writeSysBool(vidSec.c_str(), "Decals", v);
                });
                doToggle(11, tr("Limit Texture Animation", "Limitar Anim. Textura"), tr("Caps water/fire animated textures at 60Hz.", "Mantém água e fogo a 60Hz."), readSysBool(vidSec.c_str(), "LimitTextureAnimation", true), [&](bool v) {
                    writeSysBool(vidSec.c_str(), "LimitTextureAnimation", v);
                });
                break;
            }
            case 1: { // Display
                static const char* const vsyncLabelsEn[] = { "Off", "On", "Adaptive" };
                static const char* const vsyncLabelsPt[] = { "Desl.", "Lig.", "Adaptativo" };
                static const char* const fpsLabels[] = { "Off", "30", "60", "75", "120", "144", "165", "240" };
                static const char* const uiScaleLabels[] = { "Auto", "1.0x", "1.25x", "1.5x", "1.75x", "2.0x", "2.25x", "2.5x", "2.75x", "3.0x", "3.5x", "4.0x" };
                static const char* const langLabels[] = { "English", "Português (BR)" };

                double fov = readUserFloat("Engine.PlayerPawn", "DefaultFOV", 75.0);
                char fovBuf[32];
                std::snprintf(fovBuf, sizeof(fovBuf), "%.0f\xc2\xb0", fov);
                doSlider(0, tr("Field of View", "Campo de Visão"), tr("Horizontal FOV on a 4:3 frame (wide screens see wider).", "Campo de visão horizontal."), fov, 60.0, 120.0, 1.0, fovBuf, [&](double v) {
                    writeUserFloat("Engine.PlayerPawn", "DefaultFOV", v);
                    writeUserFloat("Engine.PlayerPawn", "DesiredFOV", v);
                });

                double bright = readSysFloat(vidSec.c_str(), "Brightness", 0.50);
                char bBuf[32];
                std::snprintf(bBuf, sizeof(bBuf), "%.2f", bright);
                doSlider(1, tr("Brightness", "Brilho"), tr("In-game gamma brightness level.", "Nível de brilho dentro do jogo."), bright, 0.10, 1.00, 0.02, bBuf, [&](double v) {
                    writeSysFloat(vidSec.c_str(), "Brightness", v);
                });

                doSpinner(2, tr("UI Scale", "Escala da Interface"), tr("Scales menus and HUD on high-res screens.", "Ajusta escala de menus e HUD."), readSysInt(vidSec.c_str(), "UIScale", 0), uiScaleLabels, 12, [&](int v) {
                    writeSysInt(vidSec.c_str(), "UIScale", v);
                });
                doSpinner(3, tr("Vertical Sync", "Sincronização Vertical"), tr("Prevents screen tearing.", "Evita corte na imagem."), readSysInt(vidSec.c_str(), "VSync", 1), isPtLanguage() ? vsyncLabelsPt : vsyncLabelsEn, 3, [&](int v) {
                    writeSysInt(vidSec.c_str(), "VSync", v);
                });
                doSpinner(4, tr("Frame Rate Limit", "Limite de Quadros"), tr("Caps FPS to save battery and reduce heat.", "Limita quadros para economizar bateria e calor."), readSysInt(vidSec.c_str(), "FrameRateLimit", 0), fpsLabels, 8, [&](int v) {
                    writeSysInt(vidSec.c_str(), "FrameRateLimit", v);
                });
                doSpinner(5, tr("Language", "Idioma"), tr("Language for launcher and Deus Ex menus.", "Idioma do launcher e menus do jogo."), isPtLanguage() ? 1 : 0, langLabels, 2, [&](int v) {
                    dx::config::setLanguage(v == 1 ? "ptb" : "int");
                    dx::config::system().save();
                });
                break;
            }
            case 2: { // Audio
                double gMusic = std::clamp(readSysFloat(audSec.c_str(), "MusicVolume", 160.0) / 255.0, 0.0, 1.0);
                double gSound = std::clamp(readSysFloat(audSec.c_str(), "SoundVolume", 200.0) / 255.0, 0.0, 1.0);
                double gSpeech = std::clamp(readSysFloat(audSec.c_str(), "SpeechVolume", 255.0) / 255.0, 0.0, 1.0);
                char mBuf[16], sBuf[16], spBuf[16], lmBuf[16], lsBuf[16];
                std::snprintf(mBuf, sizeof(mBuf), "%.0f%%", gMusic * 100.0);
                std::snprintf(sBuf, sizeof(sBuf), "%.0f%%", gSound * 100.0);
                std::snprintf(spBuf, sizeof(spBuf), "%.0f%%", gSpeech * 100.0);
                std::snprintf(lmBuf, sizeof(lmBuf), "%.0f%%", st.launcherMusicVol * 100.0);
                std::snprintf(lsBuf, sizeof(lsBuf), "%.0f%%", st.launcherSoundVol * 100.0);

                doSlider(0, tr("Game Music Volume", "Música do Jogo"), tr("In-game music volume.", "Volume da música do jogo."), gMusic, 0.0, 1.0, 0.05, mBuf, [&](double v) {
                    writeSysInt(audSec.c_str(), "MusicVolume", static_cast<int>(std::lround(v * 255.0)));
                });
                doSlider(1, tr("Game SFX Volume", "Efeitos Sonoros"), tr("In-game sound effects volume.", "Volume dos efeitos sonoros."), gSound, 0.0, 1.0, 0.05, sBuf, [&](double v) {
                    writeSysInt(audSec.c_str(), "SoundVolume", static_cast<int>(std::lround(v * 255.0)));
                });
                doSlider(2, tr("Speech Volume", "Vozes / Diálogos"), tr("In-game voice volume.", "Volume das falas no jogo."), gSpeech, 0.0, 1.0, 0.05, spBuf, [&](double v) {
                    writeSysInt(audSec.c_str(), "SpeechVolume", static_cast<int>(std::lround(v * 255.0)));
                });
                doToggle(3, tr("Digital Music", "Música Digital"), tr("Enable in-game tracker music.", "Ativa trilha sonora no jogo."), readSysBool(audSec.c_str(), "UseDigitalMusic", true), [&](bool v) {
                    writeSysBool(audSec.c_str(), "UseDigitalMusic", v);
                });
                doSlider(4, tr("Launcher Music", "Música do Launcher"), tr("Title music volume in DXLauncher.", "Volume da música tema no launcher."), st.launcherMusicVol, 0.0, 1.0, 0.05, lmBuf, [&](double v) {
                    st.launcherMusicVol = v;
                    if (st.audio) dxl_audio_volumes(st.audio, static_cast<float>(st.launcherMusicVol), static_cast<float>(st.launcherSoundVol));
                    saveLauncherIni(st);
                });
                doSlider(5, tr("Launcher SFX", "Sons do Launcher"), tr("Menu UI click volume in DXLauncher.", "Volume dos cliques do launcher."), st.launcherSoundVol, 0.0, 1.0, 0.05, lsBuf, [&](double v) {
                    st.launcherSoundVol = v;
                    if (st.audio) dxl_audio_volumes(st.audio, static_cast<float>(st.launcherMusicVol), static_cast<float>(st.launcherSoundVol));
                    saveLauncherIni(st);
                });
                break;
            }
            case 3: { // Touch & Gyro
                static const char* const opacLabels[] = { "25%", "40%", "55%", "75%", "95%" };
                static const char* const sizeLabels[] = { "75%", "100%", "125%", "150%" };
                static const char* const sensLabels[] = { "0.5x", "0.75x", "1.0x", "1.5x", "2.0x" };
                static const char* const gyroModeEn[] = { "Off", "Always On", "Aim / Scope" };
                static const char* const gyroModePt[] = { "Desl.", "Sempre Lig.", "Ao Mirar" };
                static const char* const gyroSens[] = { "0.5x", "0.75x", "1.0x", "1.5x", "2.0x", "3.0x" };
                static const char* const gyroAxisEn[] = { "Auto (Gravity)", "Yaw (Turn)", "Roll (Steer)" };
                static const char* const gyroAxisPt[] = { "Auto (Gravid.)", "Girar (Yaw)", "Volante (Roll)" };

                doToggle(0, tr("Touch Overlays", "Overlays Touch"), tr("Show virtual controls on screen tap.", "Exibe botões virtuais ao tocar."), readSysInt(vidSec.c_str(), "TouchOverlaysEnabled", 1) != 0, [&](bool v) {
                    writeSysInt(vidSec.c_str(), "TouchOverlaysEnabled", v ? 1 : 0);
                });
                doSpinner(1, tr("Touch Opacity", "Opacidade Touch"), tr("Virtual button opacity.", "Transparência dos controles na tela."), readSysInt(vidSec.c_str(), "TouchOverlayOpacity", 2), opacLabels, 5, [&](int v) {
                    writeSysInt(vidSec.c_str(), "TouchOverlayOpacity", v);
                });
                doSpinner(2, tr("Touch Button Size", "Tamanho Touch"), tr("Virtual control scale.", "Escala dos botões virtuais."), readSysInt(vidSec.c_str(), "TouchOverlayScale", 1), sizeLabels, 4, [&](int v) {
                    writeSysInt(vidSec.c_str(), "TouchOverlayScale", v);
                });
                doSpinner(3, tr("Touch Look Speed", "Sensib. Câmera Touch"), tr("Drag-to-look sensitivity.", "Velocidade da câmera por toque."), readSysInt(vidSec.c_str(), "TouchLookSensitivity", 2), sensLabels, 5, [&](int v) {
                    writeSysInt(vidSec.c_str(), "TouchLookSensitivity", v);
                });
                doSpinner(4, tr("Gyroscope Aim", "Mira Giroscópio"), tr("Enables motion aiming via device or controller gyro.", "Ativa mira por movimento usando giroscópio."), readSysInt(vidSec.c_str(), "GyroAimMode", 0), isPtLanguage() ? gyroModePt : gyroModeEn, 3, [&](int v) {
                    writeSysInt(vidSec.c_str(), "GyroAimMode", v);
                });
                doSpinner(5, tr("Gyro Sensitivity", "Sensib. Giroscópio"), tr("Camera rotation speed from motion.", "Sensibilidade de movimento."), readSysInt(vidSec.c_str(), "GyroSensitivity", 2), gyroSens, 6, [&](int v) {
                    writeSysInt(vidSec.c_str(), "GyroSensitivity", v);
                });
                doSpinner(6, tr("Gyro Yaw Axis", "Eixo Horiz. Gyro"), tr("Chooses horizontal turning motion axis.", "Define eixo de giro horizontal."), readSysInt(vidSec.c_str(), "GyroAxisMode", 0), isPtLanguage() ? gyroAxisPt : gyroAxisEn, 3, [&](int v) {
                    writeSysInt(vidSec.c_str(), "GyroAxisMode", v);
                });
                doToggle(7, tr("Invert Gyro Y", "Inverter Y Gyro"), tr("Inverts pitch direction for gyro aiming.", "Inverte eixo vertical do giroscópio."), readSysInt(vidSec.c_str(), "GyroInvertY", 0) != 0, [&](bool v) {
                    writeSysInt(vidSec.c_str(), "GyroInvertY", v ? 1 : 0);
                });
                break;
            }
            case 4: // Handled above by drawControllerTab
                break;
            case 5: { // Gameplay & HUD
                constexpr const char* kPlrSec = "DeusEx.DeusExPlayer";
                static const char* const themeList[] = {
                    "Default", "Amber", "Blue & Gold", "Cool Green", "Cops", "Cyan", "Dark Blue",
                    "Desert Storm", "Dried Blood", "Dusk", "Green", "Grey", "Ion Storm", "Lava",
                    "Night Vision", "Purple", "Red", "Seawater", "Steel", "Terminator", "Violet"
                };
                auto findThemeIdx = [&](const char* key) {
                    std::string cur = dx::config::user().get(kPlrSec, key).value_or("Default");
                    for (int i = 0; i < 21; ++i) {
                        if (iequalsStr(cur, themeList[i])) return i;
                    }
                    return 0;
                };

                doToggle(0, tr("Subtitles", "Legendas"), tr("Show conversation subtitles.", "Exibe legendas nas conversas."), readUserBool(kPlrSec, "bSubtitles", true), [&](bool v) {
                    writeUserBool(kPlrSec, "bSubtitles", v);
                });
                doToggle(1, tr("Always Run", "Sempre Correr"), tr("Run by default, hold Walk to slow down.", "Corre por padrão."), readUserBool(kPlrSec, "bAlwaysRun", true), [&](bool v) {
                    writeUserBool(kPlrSec, "bAlwaysRun", v);
                });
                doToggle(2, tr("Toggle Crouch", "Alternar Agachado"), tr("Press once to crouch, again to stand.", "Um toque agacha, outro levanta."), readUserBool(kPlrSec, "bToggleCrouch", true), [&](bool v) {
                    writeUserBool(kPlrSec, "bToggleCrouch", v);
                });
                doToggle(3, tr("Auto Reload", "Recarga Automática"), tr("Automatically reload empty weapon clips.", "Recarrega automaticamente ao esvaziar o pente."), readUserBool(kPlrSec, "bAutoReload", true), [&](bool v) {
                    writeUserBool(kPlrSec, "bAutoReload", v);
                });
                doToggle(4, tr("Object Names", "Nomes de Objetos"), tr("Show targeted item names under crosshair.", "Mostra nome do objeto na mira."), readUserBool(kPlrSec, "bObjectNames", true), [&](bool v) {
                    writeUserBool(kPlrSec, "bObjectNames", v);
                });
                doToggle(5, tr("Crosshair", "Mira / Crosshair"), tr("Display aiming reticle.", "Mostra retículo central."), readUserBool(kPlrSec, "bCrosshairVisible", true), [&](bool v) {
                    writeUserBool(kPlrSec, "bCrosshairVisible", v);
                });
                doToggle(6, tr("Compass", "Bússola"), tr("Show directional compass.", "Mostra bússola no HUD."), readUserBool(kPlrSec, "bCompassVisible", true), [&](bool v) {
                    writeUserBool(kPlrSec, "bCompassVisible", v);
                });
                doToggle(7, tr("Object Belt", "Cinto de Itens"), tr("Show 10-slot belt bar.", "Mostra barra rápida do cinto."), readUserBool(kPlrSec, "bObjectBeltVisible", true), [&](bool v) {
                    writeUserBool(kPlrSec, "bObjectBeltVisible", v);
                });
                doToggle(8, tr("Body Damage", "Dano Corporal"), tr("Show health/bio diagram.", "Exibe estado físico no HUD."), readUserBool(kPlrSec, "bHitDisplayVisible", true), [&](bool v) {
                    writeUserBool(kPlrSec, "bHitDisplayVisible", v);
                });
                doToggle(9, tr("Ammo Display", "Contador de Munição"), tr("Show weapon ammo readout.", "Mostra munição restante."), readUserBool(kPlrSec, "bAmmoDisplayVisible", true), [&](bool v) {
                    writeUserBool(kPlrSec, "bAmmoDisplayVisible", v);
                });
                doSpinner(10, tr("Menu Colour Theme", "Tema dos Menus"), tr("Tints DXLauncher and in-game menus.", "Tema de cores dos menus e do DXLauncher."), findThemeIdx("MenuThemeName"), themeList, 21, [&](int idx) {
                    dx::config::user().set(kPlrSec, "MenuThemeName", themeList[idx]);
                    st.appliedThemeName.clear();
                    applyMenuTheme(st);
                });
                doSpinner(11, tr("HUD Colour Theme", "Tema do HUD"), tr("Tints in-game HUD overlays.", "Tema de cores do HUD em jogo."), findThemeIdx("HUDThemeName"), themeList, 21, [&](int idx) {
                    dx::config::user().set(kPlrSec, "HUDThemeName", themeList[idx]);
                });
                break;
            }
            case 6: { // Launcher
                doToggle(0, tr("Reduced Motion", "Movimento Reduzido"), tr("Disables page slide and bracket glide animations.", "Desativa animações de transição."), st.reducedMotion, [&](bool v) {
                    st.reducedMotion = v;
                    st.ui.reduced_motion = v ? 1 : 0;
                    saveLauncherIni(st);
                });
                doToggle(1, tr("Return Here After Playing", "Voltar ao Launcher ao Sair"), tr("Re-opens DXLauncher cleanly when exiting Deus Ex.", "Retorna para o DXLauncher ao fechar o jogo."), st.returnAfterPlaying, [&](bool v) {
                    st.returnAfterPlaying = v;
                    saveLauncherIni(st);
                });
                dxl_rect rowR = rowHit(2, tr("Install Deus Ex from GOG/Steam/folder or check GitHub OTA updates.", "Instala o Deus Ex via GOG/Steam/pasta ou verifica atualizações OTA."));
                if (rowR.w > 0.0f && dxl_value_row(ui, dxl_list_row_id(&list, 2), rowR, tr("Install & Updates", "Instalação e Atualizações"), tr("Open...", "Abrir..."), t->accent, 0)) {
                    dx::config::system().save();
                    dx::config::user().save();
                    saveLauncherIni(st);
                    goPage(st, Page::Install);
                }
                break;
            }
            }

            dxl_list_end(ui, &list);

            // Help bar
            dxl_panel(ui, helpBox);
            if (!st.settingsHelpText.empty()) {
                dxl_label(ui, dxl_inset(helpBox, 10.0f), DXL_FONT_SMALL, st.settingsHelpText.c_str(), t->help, DXL_W_LEFT | DXL_W_MAY_CUT);
            }

            if (dxl_button(ui, "set done", dxl_cut_right(&btnBar, 160.0f), tr("Done", "Concluir"), DXL_W_PRIMARY) || ui->in.back) {
                ui->in.back = 0;
                goPage(st, Page::Home);
            }
        }

        void drawProgressBar(dxl_ui* ui, dxl_rect r, int permille)
        {
            const dxl_theme* t = &ui->theme;
            dxl_well(ui, r);
            dxl_rect inner = dxl_inset(r, 3.0f);
            if (permille >= 0) {
                float frac = std::clamp(static_cast<float>(permille) / 1000.0f, 0.0f, 1.0f);
                if (frac > 0.0f) {
                    dxl_rect fillR { inner.x, inner.y, inner.w * frac, inner.h };
                    dxl_fill(ui, fillR, t->accent);
                }
            } else {
                dxl_ui_animate(ui);
                float segW = inner.w * 0.28f;
                float phase = std::fmod(static_cast<float>(ui->now) * 0.85f, 1.0f);
                float pos = phase * (inner.w - segW);
                dxl_rect fillR { inner.x + pos, inner.y, segW, inner.h };
                dxl_fill(ui, fillR, t->accent);
            }
        }

        void drawInstallPage(LauncherState& st, dxl_rect r)
        {
            dxl_ui* ui = &st.ui;
            const dxl_theme* t = &ui->theme;

            dxl_rect bar = dxl_cut_bottom(&r, 44.0f);
            dxl_cut_bottom(&r, 10.0f);

            float colGap = 18.0f;
            float leftW = std::floor((r.w - colGap) * 0.52f);
            dxl_rect leftCol = dxl_cut_left(&r, leftW);
            dxl_cut_left(&r, colGap);
            dxl_rect rightCol = r;

            // Left column: Game Data Setup (Storage / Scan / GOG / Steam / Folder)
            {
                dxl_rect in = dxl_window(ui, leftCol, tr("Deus Ex Game Data", "Instalação do Deus Ex"));
                const float nw = 104.0f;
                const float lh = 26.0f;

                std::string pathDesc = st.gameValid ? st.gameRoot.string() : tr("Not configured yet", "Ainda não configurado");
                dxl_field(ui, &in, lh, nw, tr("Folder", "Pasta"), pathDesc.c_str(), st.gameValid ? t->text : t->accent, DXL_W_TAIL | DXL_W_MAY_CUT);
                std::string permDesc = st.bridge.perm ? tr("Granted", "Permitido") : tr("Permission required", "Permissão necessária");
                dxl_field(ui, &in, lh, nw, tr("Storage", "Arquivos"), permDesc.c_str(), st.bridge.perm ? t->text_focus : t->accent, DXL_W_MAY_CUT);
                if (!st.bridge.gogLabel.empty()) {
                    dxl_field(ui, &in, lh, nw, "GOG", st.bridge.gogLabel.c_str(), t->accent, DXL_W_MAY_CUT);
                } else if (!st.bridge.status.empty() && !st.bridge.busy) {
                    dxl_field(ui, &in, lh, nw, tr("Status", "Estado"), st.bridge.status.c_str(), t->help, DXL_W_MAY_CUT);
                }

                dxl_cut_top(&in, 10.0f);

                const int rows = (!st.bridge.perm ? 1 : 0) + (!st.bridge.gogPath.empty() ? 1 : 0) + 4;
                float bh = std::min(40.0f, std::max(28.0f, (in.h - static_cast<float>(rows - 1) * 7.0f) / static_cast<float>(rows)));
                float gap = 7.0f;

                if (!st.bridge.perm) {
                    if (dxl_button(ui, "inst perm", dxl_cut_top(&in, bh), tr("Allow File Access (Android)", "Permitir Acesso aos Arquivos"), DXL_W_PRIMARY)) {
                        callBridgeVoid("bridgeRequestStoragePermission");
                    }
                    dxl_cut_top(&in, gap);
                }
                if (!st.bridge.gogPath.empty()) {
                    std::string extractLabel = std::string(tr("Install Found GOG: ", "Instalar GOG Encontrado: ")) + st.bridge.gogLabel;
                    if (dxl_button(ui, "inst gog found", dxl_cut_top(&in, bh), extractLabel.c_str(), DXL_W_PRIMARY | DXL_W_MAY_CUT)) {
                        callBridgeVoid("bridgeStartGogExtractFound");
                    }
                    dxl_cut_top(&in, gap);
                }
                if (dxl_button(ui, "inst gog pick", dxl_cut_top(&in, bh), tr("Select GOG Installer (.exe)...", "Selecionar Instalador GOG (.exe)..."), st.bridge.perm && st.bridge.gogPath.empty() && !st.gameValid ? DXL_W_PRIMARY : 0)) {
                    callBridgeVoid("bridgePickGogInstaller");
                }
                dxl_cut_top(&in, gap);
                if (dxl_button(ui, "inst steam", dxl_cut_top(&in, bh), tr("Download from Steam (App 6910)...", "Baixar pela Steam (App 6910)..."), 0)) {
                    st.dialog = DialogKind::SteamMethod;
                    dxl_ui_forget(ui);
                }
                dxl_cut_top(&in, gap);
                if (dxl_button(ui, "inst folder", dxl_cut_top(&in, bh), tr("Pick Extracted Game Folder...", "Selecionar Pasta do Jogo..."), 0)) {
                    callBridgeVoid("bridgePickGameFolder");
                }
                dxl_cut_top(&in, gap);
                if (dxl_button(ui, "inst scan", dxl_cut_top(&in, bh), tr("Scan Storage Again", "Procurar no Armazenamento"), 0)) {
                    callBridgeVoid("bridgeStartAutoScan");
                }
            }

            // Right column: App Updates (GitHub OTA)
            {
                dxl_rect in = dxl_window(ui, rightCol, tr("OpenDE Updates (GitHub)", "Atualizações do OpenDE (GitHub)"));
                const float nw = 104.0f;
                const float lh = 26.0f;
                std::string verStr = st.bridge.appVersion.empty() ? st.params.versionLabel : ("v" + st.bridge.appVersion);
                dxl_field(ui, &in, lh, nw, tr("Installed", "Instalada"), verStr.c_str(), t->text_focus, 0);
                dxl_field(ui, &in, lh, nw, tr("Source", "Origem"), "JMAccorsi/OpenDE-Android", t->help, DXL_W_MAY_CUT);

                std::string updDesc;
                dxl_rgba updColor = t->text;
                if (st.bridge.updAvail) {
                    double mb = static_cast<double>(st.bridge.updSize) / (1024.0 * 1024.0);
                    char mbBuf[96];
                    if (st.bridge.updSize > 0) {
                        std::snprintf(mbBuf, sizeof(mbBuf), "%s (%.1f MB)", st.bridge.updTag.c_str(), mb);
                    } else {
                        std::snprintf(mbBuf, sizeof(mbBuf), "%s", st.bridge.updTag.c_str());
                    }
                    updDesc = std::string(tr("New release: ", "Nova versão: ")) + mbBuf;
                    updColor = t->accent;
                } else {
                    updDesc = !st.bridge.status.empty() && st.bridge.task.empty() && st.gameValid
                        ? st.bridge.status
                        : tr("Tap Check to look for updates", "Toque para buscar atualizações");
                }
                dxl_field(ui, &in, lh, nw, tr("Status", "Estado"), updDesc.c_str(), updColor, DXL_W_MAY_CUT);

                dxl_cut_top(&in, 8.0f);
                dxl_rect btnArea = dxl_cut_bottom(&in, st.bridge.needInstallPerm || st.bridge.updAvail ? 92.0f : 42.0f);
                dxl_cut_bottom(&in, 8.0f);

                dxl_well(ui, in);
                dxl_rect notesIn = dxl_inset(in, 10.0f);
                std::string notesText = !st.bridge.updNotes.empty()
                    ? st.bridge.updNotes
                    : tr("Releases include signed APK updates directly from GitHub. Installed mods, saves and configs stay intact.",
                         "As atualizações baixam o APK assinado diretamente do GitHub, mantendo seus mods, saves e configurações.");
                dxl_paragraph(ui, notesIn, DXL_FONT_SMALL, notesText.c_str(), t->help);

                if (st.bridge.needInstallPerm) {
                    if (dxl_button(ui, "upd allow apk", dxl_cut_top(&btnArea, 40.0f), tr("Allow APK Installs (Android Settings)", "Permitir Instalar APK (Ajustes)"), DXL_W_PRIMARY)) {
                        callBridgeVoid("bridgeOpenInstallUnknownPermission");
                    }
                    dxl_cut_top(&btnArea, 8.0f);
                } else if (st.bridge.updAvail) {
                    std::string dlLabel = std::string(tr("Download & Install ", "Baixar e Instalar ")) + st.bridge.updTag;
                    if (dxl_button(ui, "upd download", dxl_cut_top(&btnArea, 40.0f), dlLabel.c_str(), DXL_W_PRIMARY | DXL_W_MAY_CUT)) {
                        callBridgeVoid("bridgeDownloadUpdate");
                    }
                    dxl_cut_top(&btnArea, 8.0f);
                }
                if (dxl_button(ui, "upd check", dxl_cut_top(&btnArea, 40.0f), tr("Check for Updates", "Buscar Atualizações"), st.bridge.updAvail ? 0 : DXL_W_PRIMARY)) {
                    callBridgeBoolArg("bridgeCheckUpdates", true);
                }
            }

            if (dxl_button(ui, "inst back", dxl_cut_left(&bar, 170.0f), tr("Back", "Voltar"), 0) || ui->in.back) {
                ui->in.back = 0;
                goPage(st, Page::Home);
            }
        }

        void drawAboutPage(LauncherState& st, dxl_rect r)
        {
            dxl_ui* ui = &st.ui;
            const dxl_theme* t = &ui->theme;
            dxl_rect backBar = dxl_cut_bottom(&r, 44.0f);
            dxl_cut_bottom(&r, 10.0f);

            dxl_rect in = dxl_window(ui, r, "OpenDE Android  +  DXLauncher");
            const float nw = 140.0f;
            const float lh = 28.0f;
            std::string engVer = "OpenDE " + st.params.versionLabel + " (C++23 / OpenGL ES 3.2)";
            dxl_field(ui, &in, lh, nw, tr("Engine", "Motor"), engVer.c_str(), t->text_focus, DXL_W_MAY_CUT);
            dxl_field(ui, &in, lh, nw, "SDL", "SDL 3.4.20 + SDL3_ttf 3.2.2 + FreeType 2.14.3 + libxmp-lite 4.7.3", t->text, DXL_W_MAY_CUT);
            dxl_field(ui, &in, lh, nw, tr("Game Data", "Arquivos Base"), st.gameValid ? st.gameRoot.c_str() : tr("Not configured", "Não configurado"), t->text, DXL_W_TAIL | DXL_W_MAY_CUT);
            if (st.gameValid) {
                dxl_field(ui, &in, lh, nw, tr("Saves Folder", "Pasta de Saves"), currentSaveDir().c_str(), t->text, DXL_W_TAIL | DXL_W_MAY_CUT);
                dxl_field(ui, &in, lh, nw, tr("Config (System)", "Config (DeusEx.ini)"), dx::config::system().path.c_str(), t->text, DXL_W_TAIL | DXL_W_MAY_CUT);
            }
            dxl_cut_top(&in, 12.0f);
            dxl_paragraph(ui, in, DXL_FONT_SMALL,
                tr("Engine: jarmitage99/OpenDE  |  Launcher UI: JuggyMcNutty/deusex-launcher  |  Gamepad Mod: dsgls/DXController  |  Steam: Longi94/JavaSteam.",
                   "Motor: jarmitage99/OpenDE  |  Interface: JuggyMcNutty/deusex-launcher  |  Controle: dsgls/DXController  |  Steam: Longi94/JavaSteam."),
                t->help);

            if (dxl_button(ui, "about back", dxl_cut_left(&backBar, 160.0f), tr("Back", "Voltar"), DXL_W_PRIMARY) || ui->in.back) {
                ui->in.back = 0;
                goPage(st, Page::Home);
            }
        }

        void drawDialogs(LauncherState& st, dxl_rect above)
        {
            dxl_ui* ui = &st.ui;
            const dxl_theme* t = &ui->theme;

            // Priority 1: On-screen keyboard when active for gamepad text entry
            if (st.oskOpen && st.oskTarget) {
                dxl_ui_scope(ui, 1);
                int r = dxl_osk(ui, &st.oskState, above, st.oskTitle.c_str(), st.oskTarget, st.oskTargetSize, ui->in.back);
                if (r != 0) {
                    ui->in.back = 0;
                    st.oskOpen = false;
                    st.oskTarget = nullptr;
                    st.oskTargetSize = 0;
                    dxl_ui_forget(ui);
                }
                dxl_ui_scope(ui, 0);
                return;
            }

            // Priority 2: Steam Guard prompt requested by background Steam auth thread
            if (st.bridge.guard) {
                dxl_ui_animate(ui);
                dxl_ui_scope(ui, 1);
                dxl_rect in = dxl_dialog_begin(ui, above, 520.0f, 250.0f, "Steam Guard");
                dxl_rect btns = dxl_cut_bottom(&in, 42.0f);
                dxl_cut_bottom(&in, 10.0f);

                std::string msg;
                if (st.bridge.guardBad) {
                    msg += tr("Incorrect code.\n", "Código incorreto.\n");
                }
                if (st.bridge.guardEmail) {
                    msg += tr("Enter the Steam Guard code sent to your email", "Digite o código Steam Guard enviado ao seu e-mail");
                    if (!st.bridge.guardDomain.empty()) {
                        msg += " (" + st.bridge.guardDomain + ")";
                    }
                    msg += ":";
                } else {
                    msg += tr("Enter the Steam Guard code from your Steam Mobile app:", "Digite o código Steam Guard do seu app Steam:");
                }

                float mh = dxl_paragraph(ui, in, DXL_FONT_BODY, msg.c_str(), t->text);
                dxl_cut_top(&in, mh + 10.0f);

                int tf = dxl_text_field(ui, dxl_ui_id(ui, "steam guard code"), dxl_cut_top(&in, 44.0f), tr("Code", "Código"),
                    st.steamGuard, sizeof(st.steamGuard), "ABCDE", 0);
                if (tf == DXL_TEXT_OSK) {
                    st.oskOpen = true;
                    st.oskTarget = st.steamGuard;
                    st.oskTargetSize = sizeof(st.steamGuard);
                    st.oskTitle = "Steam Guard";
                    dxl_osk_open(&st.oskState, st.steamGuard);
                    dxl_ui_forget(ui);
                }

                float bw = (btns.w - 12.0f) * 0.5f;
                bool submit = dxl_button(ui, "guard ok", dxl_cut_left(&btns, bw), tr("Submit", "Enviar"), DXL_W_PRIMARY) != 0 || tf == DXL_TEXT_DONE;
                dxl_cut_left(&btns, 12.0f);
                bool cancel = dxl_button(ui, "guard cancel", dxl_cut_left(&btns, bw), tr("Cancel", "Cancelar"), 0) != 0 || ui->in.back;
                if (submit && st.steamGuard[0]) {
                    callBridgeStrArg("bridgeSubmitSteamGuard", st.steamGuard);
                    st.steamGuard[0] = '\0';
                    dxl_ui_forget(ui);
                } else if (cancel) {
                    ui->in.back = 0;
                    st.steamGuard[0] = '\0';
                    callBridgeStrArg("bridgeSubmitSteamGuard", "");
                    dxl_ui_forget(ui);
                }
                dxl_dialog_end(ui);
                dxl_ui_scope(ui, 0);
                return;
            }

            // Priority 3: Active background operation (Scan / GOG / Steam + QR / Mod / OTA)
            if (st.bridge.busy) {
                dxl_ui_animate(ui);
                dxl_ui_scope(ui, 1);
                bool hasQr = (st.qrTex != nullptr && st.qrW > 0 && st.qrH > 0);
                const char* title = tr("Working...", "Processando...");
                if (st.bridge.task == "scan") title = tr("Searching for Deus Ex...", "Procurando Deus Ex...");
                else if (st.bridge.task == "gog") title = tr("Extracting GOG Installer...", "Extraindo Instalador GOG...");
                else if (st.bridge.task == "steam") title = tr("Steam - Deus Ex (6910)", "Steam - Deus Ex (6910)");
                else if (st.bridge.task == "mod") title = tr("Installing Mod...", "Instalando Mod...");
                else if (st.bridge.task == "update_check") title = tr("Checking Updates...", "Verificando Atualizações...");
                else if (st.bridge.task == "update_dl") title = tr("Downloading Update...", "Baixando Atualização...");

                float dw = hasQr ? 540.0f : 520.0f;
                float dh = hasQr ? 390.0f : 230.0f;
                dxl_rect in = dxl_dialog_begin(ui, above, dw, dh, title);

                dxl_rect btns = dxl_cut_bottom(&in, 42.0f);
                dxl_cut_bottom(&in, 10.0f);

                if (hasQr) {
                    float qrSize = std::min(190.0f, in.h - 70.0f);
                    dxl_rect qrOuter = dxl_cut_top(&in, qrSize + 16.0f);
                    dxl_rect qrBox { qrOuter.x + (qrOuter.w - qrSize - 16.0f) * 0.5f, qrOuter.y, qrSize + 16.0f, qrSize + 16.0f };
                    dxl_fill(ui, qrBox, dxl_rgba{ 255, 255, 255, 255 });
                    dxl_rect qrPx = dxl_px(ui, dxl_inset(qrBox, 8.0f));
                    SDL_RenderTexture(ui->renderer, st.qrTex, nullptr, &qrPx);
                    dxl_cut_top(&in, 8.0f);
                } else {
                    dxl_rect progR = dxl_cut_top(&in, 24.0f);
                    drawProgressBar(ui, progR, st.bridge.permille);
                    dxl_cut_top(&in, 12.0f);
                }

                if (!st.bridge.status.empty()) {
                    dxl_label(ui, dxl_cut_top(&in, 26.0f), DXL_FONT_BODY, st.bridge.status.c_str(), t->text_focus, DXL_W_CENTER | DXL_W_MAY_CUT);
                }
                if (!st.bridge.detail.empty()) {
                    dxl_label(ui, dxl_cut_top(&in, 22.0f), DXL_FONT_SMALL, st.bridge.detail.c_str(), t->help, DXL_W_CENTER | DXL_W_TAIL | DXL_W_MAY_CUT);
                }

                if (dxl_button(ui, "busy cancel", btns, tr("Cancel", "Cancelar"), 0) || ui->in.back) {
                    ui->in.back = 0;
                    callBridgeVoid("bridgeCancelTask");
                }
                dxl_dialog_end(ui);
                dxl_ui_scope(ui, 0);
                return;
            }

            // Priority 4: Bridge error banner modal
            if (!st.bridge.error.empty()) {
                dxl_ui_scope(ui, 1);
                dxl_rect in = dxl_dialog_begin(ui, above, 500.0f, 200.0f, tr("Error", "Erro"));
                dxl_rect btns = dxl_cut_bottom(&in, 42.0f);
                dxl_cut_bottom(&in, 10.0f);
                dxl_paragraph(ui, in, DXL_FONT_BODY, st.bridge.error.c_str(), t->text);
                if (dxl_button(ui, "err ok", btns, "OK", DXL_W_PRIMARY) || ui->in.back) {
                    ui->in.back = 0;
                    callBridgeVoid("bridgeClearError");
                    dxl_ui_forget(ui);
                }
                dxl_dialog_end(ui);
                dxl_ui_scope(ui, 0);
                return;
            }

            if (st.dialog == DialogKind::None) {
                return;
            }

            dxl_ui_scope(ui, 1);
            if (st.dialog == DialogKind::SteamMethod) {
                dxl_rect in = dxl_dialog_begin(ui, above, 500.0f, 236.0f, tr("Sign in with Steam", "Entrar com Steam"));
                if (dxl_button(ui, "stm qr", dxl_cut_top(&in, 44.0f), tr("QR Code (Steam Mobile App)", "QR Code (App Steam no celular)"), DXL_W_PRIMARY)) {
                    st.dialog = DialogKind::None;
                    dxl_ui_forget(ui);
                    callBridgeVoid("bridgeStartSteamQr");
                }
                dxl_cut_top(&in, 10.0f);
                if (dxl_button(ui, "stm creds", dxl_cut_top(&in, 44.0f), tr("Username & Password (+ Steam Guard)", "Usuário e Senha (+ Steam Guard)"), 0)) {
                    st.dialog = DialogKind::SteamCredentials;
                    dxl_ui_forget(ui);
                }
                dxl_cut_top(&in, 12.0f);
                if (dxl_button(ui, "stm cancel", dxl_cut_top(&in, 42.0f), tr("Cancel", "Cancelar"), 0) || ui->in.back) {
                    ui->in.back = 0;
                    st.dialog = DialogKind::None;
                    dxl_ui_forget(ui);
                }
                dxl_dialog_end(ui);
                dxl_ui_scope(ui, 0);
                return;
            }

            if (st.dialog == DialogKind::SteamCredentials) {
                dxl_rect in = dxl_dialog_begin(ui, above, 540.0f, 240.0f, tr("Steam Login", "Login Steam"));
                dxl_rect btns = dxl_cut_bottom(&in, 42.0f);
                dxl_cut_bottom(&in, 10.0f);

                int uHit = dxl_text_field(ui, dxl_ui_id(ui, "steam user"), dxl_cut_top(&in, 44.0f), tr("Username", "Usuário"),
                    st.steamUser, sizeof(st.steamUser), tr("Steam account", "Conta Steam"), 0);
                if (uHit == DXL_TEXT_OSK) {
                    st.oskOpen = true;
                    st.oskTarget = st.steamUser;
                    st.oskTargetSize = sizeof(st.steamUser);
                    st.oskTitle = tr("Steam Username", "Usuário Steam");
                    dxl_osk_open(&st.oskState, st.steamUser);
                    dxl_ui_forget(ui);
                }
                dxl_cut_top(&in, 8.0f);
                int pHit = dxl_text_field(ui, dxl_ui_id(ui, "steam pass"), dxl_cut_top(&in, 44.0f), tr("Password", "Senha"),
                    st.steamPass, sizeof(st.steamPass), "••••••••", 0);
                if (pHit == DXL_TEXT_OSK) {
                    st.oskOpen = true;
                    st.oskTarget = st.steamPass;
                    st.oskTargetSize = sizeof(st.steamPass);
                    st.oskTitle = tr("Steam Password", "Senha Steam");
                    dxl_osk_open(&st.oskState, st.steamPass);
                    dxl_ui_forget(ui);
                }

                float bw = (btns.w - 12.0f) * 0.5f;
                bool signIn = dxl_button(ui, "cred go", dxl_cut_left(&btns, bw), tr("Sign In", "Entrar"), DXL_W_PRIMARY) != 0;
                dxl_cut_left(&btns, 12.0f);
                bool cancel = dxl_button(ui, "cred cancel", dxl_cut_left(&btns, bw), tr("Cancel", "Cancelar"), 0) != 0 || ui->in.back;

                if (signIn) {
                    if (st.steamUser[0] && st.steamPass[0]) {
                        callBridgeStr2Arg("bridgeStartSteamCredentials", st.steamUser, st.steamPass);
                        st.dialog = DialogKind::None;
                        dxl_ui_forget(ui);
                    } else {
                        showToast(st, tr("Enter username and password.", "Informe usuário e senha."));
                    }
                } else if (cancel) {
                    ui->in.back = 0;
                    st.dialog = DialogKind::None;
                    dxl_ui_forget(ui);
                }
                dxl_dialog_end(ui);
                dxl_ui_scope(ui, 0);
                return;
            }

            if (st.dialog == DialogKind::PadPickAction) {
                std::string title = st.padPickLabel + " — " + tr("Choose action", "Escolher ação");
                float dlgW = std::min(520.0f, above.w - 40.0f);
                float dlgH = std::min(540.0f, above.h - 40.0f);
                dxl_rect in = dxl_dialog_begin(ui, above, dlgW, dlgH, title.c_str());
                dxl_rect btns = dxl_cut_bottom(&in, 42.0f);
                dxl_cut_bottom(&in, 10.0f);

                int btnIdx = findPadButtonByPart(st.padPickPart);
                std::string curCmd = btnIdx >= 0 ? getPadBindingCmd(kPadButtons[btnIdx]) : std::string();

                dxl_well(ui, in);
                dxl_list plist {};
                dxl_list_begin(ui, &plist, "pad action pick", dxl_inset(in, 4.0f), kPadActionCount, 40.0f, DXL_LIST_WHOLE_ROWS);
                for (int i = 0; i < kPadActionCount; ++i) {
                    dxl_rect rr {};
                    if (!dxl_list_rect(&plist, i, &rr)) {
                        continue;
                    }
                    uint32_t rid = dxl_list_row_id(&plist, i);
                    int hits = dxl_ui_focusable(ui, rid, rr, 0);
                    bool selected = iequalsStr(curCmd, kPadActions[i].command);
                    bool lit = (hits & DXL_HIT_FOCUSED) != 0;
                    if (lit) {
                        dxl_fill(ui, rr, ui->theme.row_focus);
                    } else if (hits & DXL_HIT_HOVERED) {
                        dxl_fill(ui, rr, ui->theme.row_highlight);
                    }
                    const char* lbl = isPtLanguage() ? kPadActions[i].labelPt : kPadActions[i].labelEn;
                    dxl_rgba col = lit ? ui->theme.text_focus : (selected ? ui->theme.accent : ui->theme.text);
                    dxl_label(ui, dxl_rect{ rr.x + 14.0f, rr.y, rr.w - 100.0f, rr.h }, DXL_FONT_BODY, lbl, col, DXL_W_LEFT | DXL_W_MAY_CUT);
                    if (selected) {
                        dxl_label(ui, dxl_rect{ rr.x + rr.w - 92.0f, rr.y, 78.0f, rr.h }, DXL_FONT_SMALL, tr("Active", "Atual"), ui->theme.accent, DXL_W_RIGHT);
                    }
                    if (hits & DXL_HIT_ACTIVATED) {
                        dx::config::user().set(kPadBindSec, st.padPickKey, kPadActions[i].command);
                        dx::config::user().save();
                        st.padFindPart = st.padPickPart;
                        st.dialog = DialogKind::None;
                        dxl_ui_forget(ui);
                    }
                }
                dxl_list_end(ui, &plist);

                bool cancel = dxl_button(ui, "pad pick cancel", dxl_cut_right(&btns, 160.0f), tr("Cancel", "Cancelar"), 0) != 0 || ui->in.back;
                if (cancel) {
                    ui->in.back = 0;
                    st.padFindPart = st.padPickPart;
                    st.dialog = DialogKind::None;
                    dxl_ui_forget(ui);
                }
                dxl_dialog_end(ui);
                dxl_ui_scope(ui, 0);
                return;
            }

            if (st.dialog == DialogKind::PadTest) {
                auto closeTest = [&]() {
                    SDL_Gamepad* tg = SDL_GetGamepadFromID(st.padTestId);
                    if (tg && st.padTestRumbling) {
                        SDL_RumbleGamepad(tg, 0, 0, 0);
                    }
                    st.padTestRumbling = false;
                    st.padTestHoldSince = -1.0;
                    st.ui.pads_off = 0;
                    st.dialog = DialogKind::None;
                    dxl_ui_forget(ui);
                };

                SDL_Gamepad* tg = SDL_GetGamepadFromID(st.padTestId);
                if (!tg) {
                    closeTest();
                    showToast(st, tr("Controller disconnected.", "Controle desconectado."));
                    dxl_ui_scope(ui, 0);
                    return;
                }

                dxl_pad_family tfam = dxl_ui_pad_family(tg);
                SDL_GamepadButton backBtn = (tfam == DXL_PADFAMILY_NINTENDO) ? SDL_GAMEPAD_BUTTON_SOUTH : SDL_GAMEPAD_BUTTON_EAST;
                float hold = 0.0f;
                if (SDL_GetGamepadButton(tg, backBtn)) {
                    if (st.padTestHoldSince < 0.0) {
                        st.padTestHoldSince = ui->now;
                    }
                    hold = static_cast<float>(ui->now - st.padTestHoldSince);
                } else {
                    st.padTestHoldSince = -1.0;
                }
                if (hold >= 1.0f) {
                    closeTest();
                    dxl_ui_scope(ui, 0);
                    return;
                }

                float lt = std::max(0.0f, static_cast<float>(SDL_GetGamepadAxis(tg, SDL_GAMEPAD_AXIS_LEFT_TRIGGER)) / 32767.0f);
                float rt = std::max(0.0f, static_cast<float>(SDL_GetGamepadAxis(tg, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)) / 32767.0f);
                if (lt > 0.05f || rt > 0.05f) {
                    SDL_RumbleGamepad(tg,
                        static_cast<Uint16>(std::min(lt, 1.0f) * 65535.0f),
                        static_cast<Uint16>(std::min(rt, 1.0f) * 65535.0f), 250);
                    st.padTestRumbling = true;
                } else if (st.padTestRumbling) {
                    SDL_RumbleGamepad(tg, 0, 0, 0);
                    st.padTestRumbling = false;
                }
                dxl_ui_animate(ui);

                std::vector<PadDeviceInfo> pads = enumeratePads();
                int shown = 0;
                for (int i = 0; i < static_cast<int>(pads.size()); ++i) {
                    if (pads[static_cast<size_t>(i)].id == st.padTestId) {
                        shown = i;
                        break;
                    }
                }

                float dlgW = std::min(960.0f, above.w - 36.0f);
                float dlgH = std::min(640.0f, above.h - 28.0f);
                dxl_rect in = dxl_dialog_begin(ui, above, dlgW, dlgH, tr("Testing the controller", "Testando o controle"));
                const char* backName = padPartLabel(tfam, backBtn == SDL_GAMEPAD_BUTTON_SOUTH ? DXL_PADPART_SOUTH : DXL_PADPART_EAST);
                std::string hintLine = isPtLanguage()
                    ? (std::string("Todos os botões, analógicos e gatilhos acendem no modelo 3D; os gatilhos testam a vibração. Segure ") + backName + " por 1s ou toque em Parar.")
                    : (std::string("Every button, stick and trigger lights up on the 3D model; triggers test rumble. Hold ") + backName + " for 1s or tap Stop to finish.");
                float ph = dxl_paragraph(ui, in, DXL_FONT_SMALL, hintLine.c_str(), ui->theme.help);
                dxl_cut_top(&in, ph + 6.0f);
                dxl_rect holdBar = dxl_cut_top(&in, 4.0f);
                if (hold > 0.0f) {
                    dxl_fill(ui, holdBar, dxl_rgba_fade(ui->theme.panel_light, 0.25f));
                    dxl_fill(ui, dxl_rect{ holdBar.x, holdBar.y, holdBar.w * std::min(hold, 1.0f), holdBar.h }, ui->theme.accent);
                }
                dxl_cut_top(&in, 8.0f);
                dxl_rect btns = dxl_cut_bottom(&in, 44.0f);
                dxl_cut_bottom(&in, 8.0f);

                drawPad3DPanel(st, in, pads, shown, tfam, -1, true);
                bool stop = dxl_button(ui, "pad test stop", dxl_cut_right(&btns, 180.0f), tr("Stop testing", "Parar teste"), DXL_W_PRIMARY) != 0;
                dxl_dialog_end(ui);
                if (stop || ui->in.back) {
                    ui->in.back = 0;
                    closeTest();
                }
                dxl_ui_scope(ui, 0);
                return;
            }

            dxl_rect in = dxl_dialog_begin(ui, above, 460.0f, 180.0f, st.dialogTitle.c_str());
            dxl_rect btns = dxl_cut_bottom(&in, 42.0f);
            dxl_cut_bottom(&in, 10.0f);
            dxl_paragraph(ui, in, DXL_FONT_BODY, st.dialogBody.c_str(), ui->theme.text);

            float bw = (btns.w - 12.0f) * 0.5f;
            bool confirm = dxl_button(ui, "dlg yes", dxl_cut_left(&btns, bw), tr("Delete", "Excluir"), DXL_W_PRIMARY) != 0;
            dxl_cut_left(&btns, 12.0f);
            bool cancel = dxl_button(ui, "dlg no", dxl_cut_left(&btns, bw), tr("Cancel", "Cancelar"), 0) != 0 || ui->in.back;

            if (confirm) {
                dxl_err err {};
                if (st.dialog == DialogKind::ConfirmDeleteSave && st.deleteTargetSlot >= 0 && st.deleteTargetSlot < st.saveList.count) {
                    std::filesystem::path saveDir = currentSaveDir();
                    if (char* folder = dxl_save_path(saveDir.c_str(), &st.saveList.items[st.deleteTargetSlot])) {
                        if (dxl_delete(folder, &err) == 0) {
                            showToast(st, tr("Save deleted.", "Save excluído."));
                        }
                        std::free(folder);
                    }
                    reloadSaves(st);
                } else if (st.dialog == DialogKind::ConfirmDeleteShot && st.deleteShotIndex >= 0 && st.deleteShotIndex < st.shotList.count) {
                    if (dxl_delete(st.shotList.items[st.deleteShotIndex].path, &err) == 0) {
                        showToast(st, tr("Screenshot deleted.", "Captura excluída."));
                    }
                    reloadShots(st);
                } else if (st.dialog == DialogKind::ConfirmDeleteMod && !st.deleteModId.empty()) {
                    if (callBridgeDeleteMod(st.deleteModId)) {
                        if (iequalsStr(st.activeModId, st.deleteModId)) {
                            st.activeModId.clear();
                        }
                        scanMods(st);
                        showToast(st, tr("Mod deleted.", "Mod excluído."));
                    }
                }
                st.dialog = DialogKind::None;
                dxl_ui_forget(ui);
            } else if (cancel) {
                ui->in.back = 0;
                st.dialog = DialogKind::None;
                dxl_ui_forget(ui);
            }
            dxl_dialog_end(ui);
            dxl_ui_scope(ui, 0);
        }

        void drawToast(LauncherState& st, float margin)
        {
            if (st.toastText.empty()) {
                return;
            }
            double age = st.ui.now - st.toastAt;
            if (age >= 3.2) {
                st.toastText.clear();
                return;
            }
            dxl_ui* ui = &st.ui;
            const dxl_theme* t = &ui->theme;
            float a = age < 0.15 ? static_cast<float>(age / 0.15) : (age > 2.8 ? static_cast<float>((3.2 - age) / 0.4) : 1.0f);
            a = std::clamp(a, 0.0f, 1.0f);
            if (a < 1.0f) {
                dxl_ui_animate(ui);
            }
            float w = std::min(380.0f, ui->w - 2.0f * margin);
            float th = dxl_text_size(ui, DXL_FONT_SMALL, st.toastText.c_str(), w - 28.0f).y;
            dxl_rect r { ui->w - margin - w, 18.0f, w, th + 20.0f };
            dxl_bevel_style bs { dxl_rgba_fade(t->panel_top, a), dxl_rgba_fade(t->panel_bottom, a), dxl_rgba_fade(t->panel_light, a), dxl_rgba_fade(t->panel_shadow, a), 6.0f };
            dxl_bevel(ui, r, &bs);
            dxl_fill(ui, dxl_rect{ r.x + 6.0f, r.y + 8.0f, 3.0f, r.h - 16.0f }, dxl_rgba_fade(t->accent, a));
            dxl_text_draw(ui, DXL_FONT_SMALL, st.toastText.c_str(), r.x + 18.0f, r.y + 10.0f, w - 28.0f, dxl_rgba_fade(t->text_focus, a), 1.0f);
        }

        void cleanupLauncher(LauncherState& st)
        {
            if (st.audio) {
                if (st.outcome.action == LauncherAction::Play && dxl_audio_music_playing(st.audio)) {
                    dxl_audio_fade(st.audio, 0.25f);
                    SDL_Delay(220);
                }
                dxl_audio_close(st.audio);
                st.audio = nullptr;
            }
            std::free(st.musicBuf);
            st.musicBuf = nullptr;
            for (int i = 0; i < DXL_SOUND_COUNT; ++i) {
                std::free(st.soundBufs[i]);
                st.soundBufs[i] = nullptr;
            }
            if (st.saveSnap) {
                SDL_DestroyTexture(st.saveSnap);
                st.saveSnap = nullptr;
            }
            if (st.qrTex) {
                SDL_DestroyTexture(st.qrTex);
                st.qrTex = nullptr;
            }
            dxl_saves_free(&st.saveList);
            dxl_shots_free(&st.shotList);
            dxl_thumbs_free(st.shotThumbs);
            dxl_thumbs_free(st.shotWhole);
            dxl_padmesh_free(&st.padMesh);
            dxl_art_free(&st.art);
            dxl_ui_free(&st.ui);
            if (st.renderer) {
                SDL_DestroyRenderer(st.renderer);
                st.renderer = nullptr;
            }
            if (st.window) {
                SDL_DestroyWindow(st.window);
                st.window = nullptr;
            }
            TTF_Quit();
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
        }

    } // namespace

    LauncherOutcome runDXLauncher(const LauncherParams& params)
    {
        LauncherState st;
        st.params = params;
        if (!params.gameRoot.empty()) {
            st.gameRoot = std::filesystem::path(params.gameRoot);
        }
#ifndef __ANDROID__
        if (st.gameRoot.empty()) {
            st.gameRoot = dx::config::findGameRoot(DEUSEX_GAME_DATA);
        }
#endif
        st.systemDir = resolveSubdirCi(st.gameRoot, "System");
        if (st.systemDir.empty() && !st.gameRoot.empty()) {
            st.systemDir = st.gameRoot / "System";
        }
        st.gameValid = isDirectoryValidGameRoot(st.gameRoot);
        st.activeModId = params.initialMod;

        SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "1");
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "DXLauncher: audio init failed: %s", SDL_GetError());
        }
        if (!TTF_Init()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "DXLauncher: TTF_Init failed: %s", SDL_GetError());
            cleanupLauncher(st);
            st.outcome.action = LauncherAction::Play;
            st.outcome.gameRoot = st.gameRoot.string();
            st.outcome.modId = st.activeModId;
            return st.outcome;
        }

#ifdef __ANDROID__
        SDL_WindowFlags winFlags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_FULLSCREEN;
#else
        SDL_WindowFlags winFlags = SDL_WINDOW_RESIZABLE;
#endif
        st.window = SDL_CreateWindow("Deus Ex - DXLauncher", 1280, 720, winFlags);
        if (!st.window) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "DXLauncher: SDL_CreateWindow failed: %s", SDL_GetError());
            cleanupLauncher(st);
            st.outcome.action = LauncherAction::Play;
            st.outcome.gameRoot = st.gameRoot.string();
            st.outcome.modId = st.activeModId;
            return st.outcome;
        }
        st.renderer = SDL_CreateRenderer(st.window, nullptr);
        if (!st.renderer) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "DXLauncher: SDL_CreateRenderer failed: %s", SDL_GetError());
            cleanupLauncher(st);
            st.outcome.action = LauncherAction::Play;
            st.outcome.gameRoot = st.gameRoot.string();
            st.outcome.modId = st.activeModId;
            return st.outcome;
        }
        SDL_SetRenderVSync(st.renderer, 1);

        if (dxl_ui_init(&st.ui, st.window, st.renderer, params.fontsDir.c_str(), 1.0f) != 0) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "DXLauncher: dxl_ui_init failed (%s): %s", params.fontsDir.c_str(), SDL_GetError());
            cleanupLauncher(st);
            st.outcome.action = LauncherAction::Play;
            st.outcome.gameRoot = st.gameRoot.string();
            st.outcome.modId = st.activeModId;
            return st.outcome;
        }
        dxl_ui_set_big(&st.ui, 1);

        dxl_theme defTheme {};
        dxl_theme_default(&defTheme);
        dxl_ui_set_theme(&st.ui, &defTheme);

        loadLauncherIni(st);
        if (st.gameValid) {
            scanMods(st);
            loadPackagesAudioAndThemes(st);
            applyMenuTheme(st);
        }
        pollAndroidBridge(st);
        if (!st.gameValid) {
            st.page = Page::Install;
        }

        // Activate any initial gamepad already connected
        int padCount = 0;
        if (SDL_JoystickID* pads = SDL_GetGamepads(&padCount)) {
            for (int i = 0; i < padCount; ++i) {
                SDL_Event addEv {};
                addEv.type = SDL_EVENT_GAMEPAD_ADDED;
                addEv.gdevice.which = pads[i];
                dxl_ui_event(&st.ui, &addEv);
            }
            if (padCount > 0) {
                st.ui.mode = DXL_INPUT_PAD;
            }
            SDL_free(pads);
        }

        st.pageSince = static_cast<double>(SDL_GetTicksNS()) / 1e9;

        while (!st.finished) {
            dxl_ui_input_begin(&st.ui);

            SDL_Event ev;
            while (SDL_PollEvent(&ev)) {
                if (ev.type == SDL_EVENT_FINGER_DOWN) {
                    if (!st.touchTracking) {
                        st.touchTracking = true;
                        st.touchSwiping = false;
                        st.touchFinger = ev.tfinger.fingerID;
                        st.touchStartX = ev.tfinger.x;
                        st.touchStartY = ev.tfinger.y;
                        st.touchLastY = ev.tfinger.y;
                    }
                } else if (ev.type == SDL_EVENT_FINGER_MOTION && st.touchTracking && ev.tfinger.fingerID == st.touchFinger) {
                    bool onPad3D = (st.page == Page::Settings && st.settingsTab == 4) || st.dialog == DialogKind::PadTest;
                    float startDpX = st.touchStartX * st.ui.w;
                    float startDpY = st.touchStartY * st.ui.h;
                    bool startedOnModel = onPad3D
                        && startDpX >= st.padModelRect.x && startDpX <= st.padModelRect.x + st.padModelRect.w
                        && startDpY >= st.padModelRect.y && startDpY <= st.padModelRect.y + st.padModelRect.h;
                    float totalDyPx = std::fabs(ev.tfinger.y - st.touchStartY) * static_cast<float>(st.ui.h_px);
                    float totalDxPx = std::fabs(ev.tfinger.x - st.touchStartX) * static_cast<float>(st.ui.w_px);
                    if (!startedOnModel && !st.touchSwiping && totalDyPx > 20.0f && totalDyPx > totalDxPx * 1.2f) {
                        st.touchSwiping = true;
                    }
                    if (st.touchSwiping) {
                        float stepPx = (ev.tfinger.y - st.touchLastY) * static_cast<float>(st.ui.h_px);
                        st.ui.in.wheel += stepPx / 32.0f;
                        st.ui.in.any = 1;
                    }
                    st.touchLastY = ev.tfinger.y;
                } else if (ev.type == SDL_EVENT_FINGER_UP && st.touchTracking && ev.tfinger.fingerID == st.touchFinger) {
                    bool wasSwipe = st.touchSwiping;
                    st.touchTracking = false;
                    st.touchSwiping = false;
                    if (wasSwipe) {
                        st.ui.held = 0;
                        continue; // Don't trigger button click after swipe scroll
                    }
                }

                if (st.touchSwiping && (ev.type == SDL_EVENT_MOUSE_MOTION || ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN || ev.type == SDL_EVENT_MOUSE_BUTTON_UP)) {
                    continue;
                }
                if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == SDLK_AC_BACK) {
                    st.ui.in.back++;
                    st.ui.in.any = 1;
                    continue;
                }
                dxl_ui_event(&st.ui, &ev);
            }

            double now = static_cast<double>(SDL_GetTicksNS()) / 1e9;
            dxl_ui_begin(&st.ui, now);
            pollAndroidBridge(st);
            if (st.ui.in.quit) {
                st.outcome.action = LauncherAction::Quit;
                st.finished = true;
            }

            const bool modalActive = (st.dialog != DialogKind::None) || st.bridge.busy || st.bridge.guard || !st.bridge.error.empty() || st.oskOpen;

            constexpr float kFootH = 52.0f;
            float margin = std::max(28.0f, (st.ui.w - 1360.0f) * 0.5f);
            margin = std::clamp(margin, 28.0f, 120.0f);
            dxl_rect above { 0.0f, 0.0f, st.ui.w, st.ui.h - kFootH };
            dxl_rect bottom { 0.0f, st.ui.h - kFootH, st.ui.w, kFootH };

            dxl_backdrop(&st.ui);
            dxl_ui_scope(&st.ui, modalActive ? 1 : 0);
            drawFooter(st, bottom, margin);
            dxl_ui_scope(&st.ui, 0);

            int savedBack = st.ui.in.back;
            if (modalActive) {
                st.ui.in.back = 0;
            }

            float t = dxl_ui_tween(&st.ui, st.pageSince, 0.18f);
            float shift = (1.0f - t) * 24.0f;
            float headH = drawHeader(st, margin, shift);
            dxl_rect content { margin + shift, headH, st.ui.w - 2.0f * margin, st.ui.h - headH - kFootH - 16.0f };

            switch (st.page) {
            case Page::Home: drawHomePage(st, content); break;
            case Page::Saves: drawSavesPage(st, content); break;
            case Page::Mods: drawModsPage(st, content); break;
            case Page::Shots: drawShotsPage(st, content); break;
            case Page::Settings: drawSettingsPage(st, content); break;
            case Page::Install: drawInstallPage(st, content); break;
            case Page::About: drawAboutPage(st, content); break;
            }

            // On Home, Back quits only on controller? No, Android back or Quit button exits cleanly
            if (!modalActive && st.page == Page::Home && st.ui.in.back) {
                st.ui.in.back = 0;
            }

            if (t < 1.0f) {
                dxl_gradient(&st.ui, above, dxl_rgba_fade(st.ui.theme.backdrop_top, 1.0f - t), dxl_rgba_fade(st.ui.theme.backdrop_bottom, 1.0f - t));
            }

            if (modalActive) {
                st.ui.in.back = savedBack;
                drawDialogs(st, above);
            }

            drawToast(st, margin);
            dxl_ui_end(&st.ui);
            playCueSound(st);

            SDL_RenderPresent(st.renderer);
        }

        LauncherOutcome result = st.outcome;
        cleanupLauncher(st);
        return result;
    }

} // namespace game
