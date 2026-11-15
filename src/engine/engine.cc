#include "audio/audio.h"
#include "core/config.h"
#include "core/natives.h"
#include "core/objects.h"
#include "engine/props_consys.h"
#include "engine/props_deusex.h"
#include "engine/props_engine.h"
#include "engine/props_extension.h"
#include "engine/props_fire.h"
#include "engine/mesh.h"
#include "engine/resources.h"
#include "engine/actor.h"
#include "engine/console.h"
#include "engine/world.h"
#include "extension/extension.h"

#include <cstdlib>
#include <filesystem>

namespace dx::engine {
    void initializeCore(const std::string& gameRoot)
    {
        static bool initialized = false;
        if (initialized) {
            return;
        }
        initialized = true;
        std::filesystem::path root(gameRoot);
        config::initialize(config::resolveSubdir(root, "System"));
        registerEngineResources();
        registerMeshResources();
        registerObjectNatives();
        registerEngineNatives();
        extension::registerNatives();
        registerDeusExNatives();
        objects::initialize(root);
    }

    void initialize(const std::string& gameRoot)
    {
        static bool initialized = false;
        if (initialized) {
            return;
        }
        initialized = true;
        initializeCore(gameRoot);
        if (!iequals(config::language(), "int")) {
            for (const char* locPkg : { "DeusExText", "DeusExConText" }) {
                std::filesystem::path overridePath = config::localizedPackagePath(locPkg);
                if (!overridePath.empty()) {
                    objects::setPackageFile(Name(locPkg), overridePath);
                }
            }
        }
        const char* packages[] = { "Engine", "Fire", "Extension", "ConSys", "DeusExText", "DeusEx" };
        for (const char* package : packages) {
            objects::loadPackage(Name(package));
        }
        Linker* dxController = objects::loadPackage(Name("DXController"));
        if (dxController) {
            config::system().set("Engine.Engine", "Console", "DXController.ControllerConsole");
            config::system().set("Engine.Engine", "Root", "DXController.ControllerRootWindow");
            constexpr const char* settingsSection = "DXController.ControllerSettings";
            const bool corrupted =
                config::system().get(settingsSection, "StickCurveLeft").value_or("").empty() ||
                config::system().get(settingsSection, "StickCurveRight").value_or("").empty() ||
                std::atof(config::system().get(settingsSection, "StickScaleRight").value_or("0").c_str()) <= 0.05 ||
                std::atof(config::system().get(settingsSection, "StickCurvePowerLeft").value_or("0").c_str()) <= 0.05;
            auto seedSystem = [corrupted](const char* section, const char* key, const char* value) {
                auto current = config::system().get(section, key);
                if (!current.has_value() || current->empty() || (corrupted && iequals(section, settingsSection))) {
                    config::system().set(section, key, value);
                }
            };
            seedSystem("DXController.ControllerSettings", "StickDeadzoneLeft", "3500");
            seedSystem("DXController.ControllerSettings", "StickDeadzoneRight", "3500");
            seedSystem("DXController.ControllerSettings", "TriggerThreshold", "30");
            seedSystem("DXController.ControllerSettings", "MouseActivityPx", "4");
            seedSystem("DXController.ControllerSettings", "PadActiveGraceMs", "500");
            seedSystem("DXController.ControllerSettings", "StickCurveLeft", "Power");
            seedSystem("DXController.ControllerSettings", "StickCurveRight", "Sigmoid");
            seedSystem("DXController.ControllerSettings", "StickCurvePowerLeft", "2.000000");
            seedSystem("DXController.ControllerSettings", "StickCurvePowerRight", "2.000000");
            seedSystem("DXController.ControllerSettings", "StickCurveExpoLeft", "0.600000");
            seedSystem("DXController.ControllerSettings", "StickCurveExpoRight", "0.600000");
            seedSystem("DXController.ControllerSettings", "StickCurveSigmoidSteepnessLeft", "6.000000");
            seedSystem("DXController.ControllerSettings", "StickCurveSigmoidSteepnessRight", "8.000000");
            seedSystem("DXController.ControllerSettings", "StickCurveSigmoidMidpointLeft", "0.600000");
            seedSystem("DXController.ControllerSettings", "StickCurveSigmoidMidpointRight", "0.700000");
            seedSystem("DXController.ControllerSettings", "StickCurveSigmoidStrengthLeft", "0.600000");
            seedSystem("DXController.ControllerSettings", "StickCurveSigmoidStrengthRight", "0.900000");
            seedSystem("DXController.ControllerSettings", "StickScaleRight", "0.400000");
            seedSystem("DXController.ControllerSettings", "InvertLookY", "False");
            seedSystem("DXController.AutoSaveManager", "bEnabled", "False");
            seedSystem("DXController.AutoSaveManager", "IntervalSeconds", "300");
            seedSystem("DXController.AutoSaveManager", "MaxSaves", "3");
            if (corrupted) {
                config::system().save();
            }
        }
        bindProps_engine();
        bindProps_fire();
        bindProps_extension();
        bindProps_consys();
        bindProps_deusex();
        objects::flushLoads();
        bindEngineClasses();
        console::loadBindings();
    }
}
