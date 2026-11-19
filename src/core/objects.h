#pragma once

#include "core/object.h"
#include "engine/package.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dx {
    class Linker {
    public:
        Linker(Name name, std::unique_ptr<Package> file);

        UObject* object(int32_t ref);
        void rebind(int32_t ref, UObject* object);
        Name name(int32_t index) const;
        UObject* readObject(Reader& reader);
        Name readName(Reader& reader);
        int32_t findExport(Name objectName, Name className, std::string_view group) const;
        const Package& file() const;

        Name packageName;
        UObject* packageObject = nullptr;
        bool savedGame = false;
        uint64_t loadOrder = 0;

    private:
        UObject* createExport(int32_t ref);
        UObject* resolveImport(int32_t ref);

        std::unique_ptr<Package> package;
        std::vector<Name> names;
        std::vector<UObject*> exports;
        std::vector<UObject*> imports;
        std::vector<bool> importResolved;
    };

    using ObjectFactory = std::function<UObject*()>;

    namespace objects {
        void initialize(const std::filesystem::path& gameRoot);
        const std::filesystem::path& gameRoot();
        std::vector<std::filesystem::path> packageFiles(std::string_view extension);
        Linker* loadPackage(Name packageName);
        Linker* loadPackageFile(Name packageName, const std::filesystem::path& path, bool savedGame);
        void setPackageFile(Name packageName, const std::filesystem::path& path);
        Linker* findLinker(Name packageName);
        void unloadPackage(Name packageName);
        UObject* packageObject(Name packageName);
        const std::vector<std::string>& loadMismatches();
        void setEditorMode(bool enabled);
        bool editorMode();
        void registerFactory(Name nativeClass, ObjectFactory factory);
        void registerObject(UObject* object);
        extern UObject* const* gHandleData;
        extern size_t gHandleSize;
        void ensureLoaded(UObject* object);
        inline UObject* fromHandle(uint32_t handle)
        {
            if (handle == 0 || handle >= gHandleSize) {
                return nullptr;
            }
            UObject* object = gHandleData[handle];
            if (object && !object->loaded) {
                ensureLoaded(object);
            }
            return object;
        }
        const std::vector<UObject*>& all();
        void flushLoads();
        void queueLoad(UObject* object);
        UClass* findClass(std::string_view path);
        UClass* intrinsicClass(Name className);
        bool isIntrinsic(const UObject* object);
        bool isIntrinsicClassName(std::string_view className);
        UObject* findObject(std::string_view path, UClass* cls);
        UObject* loadObject(std::string_view path, UClass* cls);
        UObject* newObject(UClass* cls, UObject* outer, Name objectName, uint32_t flags);
        UClass* createSubclass(UClass* parent, Name className);
        UObject* createNative(UClass* cls);
        void destroyObject(UObject* object);
        uint64_t destroyCount();
        Name uniqueName(UClass* cls);
        void readTaggedProperties(UStruct* structType, uint8_t* data, Reader& reader, Linker& linker);
        void syncObjectProperties(UObject* object);
        UClass* classClass();
        UClass* objectClass();
    }
}
