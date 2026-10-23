#include "ScriptingService.hpp"
#include "App/Depot/ResourceReference.hpp"
#include "App/Engine/CurveData.hpp"

namespace
{
constexpr auto ISerializableTypeName = Red::GetTypeName<Red::ISerializable>();
constexpr auto IScriptableTypeName = Red::GetTypeName<Red::IScriptable>();

constexpr auto ResourceWrapperName = Red::GetTypeName<App::ResourceWrapper>();
constexpr auto ResourceAsyncWrapperName = Red::GetTypeName<App::ResourceAsyncWrapper>();
constexpr auto ResourceScriptReferenceAlias = Red::CName(Red::redResourceReferenceScriptToken::ALIAS);
constexpr auto ResourceReferencePrefix = Red::GetTypePrefixStr<Red::ResourceReference>();
constexpr auto ResourceAsyncReferencePrefix = Red::GetTypePrefixStr<Red::ResourceAsyncReference>();

constexpr auto CurveDataWrapperFloatName = Red::GetTypeName<App::CurveDataWrapper<float>>();
constexpr auto CurveDataWrapperVector2Name = Red::GetTypeName<App::CurveDataWrapper<Red::Vector2>>();
constexpr auto CurveDataWrapperVector3Name = Red::GetTypeName<App::CurveDataWrapper<Red::Vector3>>();
constexpr auto CurveDataWrapperVector4Name = Red::GetTypeName<App::CurveDataWrapper<Red::Vector4>>();
constexpr auto CurveDataWrapperHDRColorName = Red::GetTypeName<App::CurveDataWrapper<Red::HDRColor>>();
constexpr auto CurveDataPrefix = Red::GetTypePrefixStr<Red::CurveData>();

constexpr auto inkWidgetLibraryResourceTypeName = Red::CName("inkWidgetLibraryResource");
constexpr auto gameuiBaseUIDataTypeName = Red::CName("gameuiBaseUIData");
constexpr auto scnChatterTypeName = Red::CName("scnChatter");
constexpr auto audioEventElementTypeName = Red::CName("audioAudioEventMetadataArrayElement");
constexpr auto entLODDefinitionTypeName = Red::CName("entLODDefinition");
}

App::ScriptingService::ScriptingService(const std::filesystem::path& aStateDir)
{
    s_stateDir = aStateDir;

    std::error_code error;
    if (!std::filesystem::exists(s_stateDir, error))
    {
        if (!std::filesystem::create_directories(s_stateDir, error))
        {
            LogError("Cannot create directory \"{}\" for persistent data: {}.", s_stateDir.string(), error.message());
        }
    }
}

void App::ScriptingService::OnBootstrap()
{
    LogInfo("[ScriptingService] Attaching script hooks...");
    LogInfo("[ScriptingService] Hook ScriptBundle::Destruct");
    HookAfter<Raw::ScriptBundle::Destruct>(&OnInitializeScripts).OrThrow();
    LogInfo("[ScriptingService] Hook CGameFramework::InitializeGameInstance");
    HookAfter<Raw::CGameFramework::InitializeGameInstance>(&OnInitializeGameInstance).OrThrow();
    LogInfo("[ScriptingService] Hook ScriptValidator::Validate");
    HookBefore<Raw::ScriptValidator::Validate>(&OnValidateScripts).OrThrow();
    LogInfo("[ScriptingService] Hook ScriptValidator::ValidateProperty");
    Hook<Raw::ScriptValidator::ValidateProperty>(&OnValidateProperty).OrThrow();
    LogInfo("[ScriptingService] Hook ScriptValidator::CompareTypeName");
    HookAfter<Raw::ScriptValidator::CompareTypeName>(&OnValidateTypeName).OrThrow();
    LogInfo("[ScriptingService] Hook ScriptOpCodes::Register");
    HookAfter<Raw::ScriptOpCodes::Register>(&OnRegisterScriptOpCodes).OrThrow();
    LogInfo("[ScriptingService] Hook CClass::CreateInstance");
    HookAfter<Raw::CClass::CreateInstance>(&OnCreateInstance).OrThrow();
    LogInfo("[ScriptingService] Hook GetScriptGameInstance");
    Hook<Raw::GetScriptGameInstance>(&GetScriptGameInstance).OrThrow();
    LogInfo("[ScriptingService] Script hooks attached");
}

void App::ScriptingService::OnInitializeScripts()
{
    LogInfo("[ScriptingService] OnInitializeScripts enter");

    auto global = Red::GetGlobalFunction("InitializeScripts;");
    if (!global)
    {
        LogInfo("[ScriptingService] OnInitializeScripts skipped (no global)");
        return;
    }

    auto engine = Red::CGameEngine::Get();
    if (!engine)
    {
        LogError("[ScriptingService] OnInitializeScripts skipped (no engine)");
        return;
    }

    if (!engine->scriptsLoaded)
    {
        auto& container = ScriptableServiceContainer::Build(s_stateDir);
        if (!container)
        {
            LogError("[ScriptingService] OnInitializeScripts skipped (container build failed)");
            return;
        }
        container->OnInitializeScripts();
    }
    else
    {
        auto& container = ScriptableServiceContainer::Get();
        if (!container)
        {
            LogError("[ScriptingService] OnInitializeScripts skipped (no container)");
            return;
        }
        container->OnInitializeScripts();
    }

    LogInfo("[ScriptingService] OnInitializeScripts done");
}

void App::ScriptingService::OnInitializeGameInstance()
{
    LogInfo("[ScriptingService] OnInitializeGameInstance enter");

    auto& container = ScriptableServiceContainer::Get();
    if (!container)
    {
        LogError("[ScriptingService] OnInitializeGameInstance skipped (no container)");
        return;
    }

    container->OnInitializeGameInstance();
    LogInfo("[ScriptingService] OnInitializeGameInstance done");
}

void App::ScriptingService::OnValidateScripts(void* aValidator, Red::ScriptBundle* aBundle, void* aReport)
{
    LogInfo("[ScriptingService] OnValidateScripts enter");

    if (!aBundle)
    {
        LogError("[ScriptingService] OnValidateScripts skipped (no bundle)");
        return;
    }

    if (auto serializableClass = Red::GetClass<ISerializableTypeName>())
    {
        if (auto baseUiClass = Red::GetClass<gameuiBaseUIDataTypeName>())
        {
            baseUiClass->parent = serializableClass;
        }
        if (auto chatterClass = Red::GetClass<scnChatterTypeName>())
        {
            chatterClass->parent = serializableClass;
        }
    }

    uint32_t index = 0;
    const auto count = aBundle->classes.Size();

    for (auto& classDef : aBundle->classes)
    {
        if ((index % 500) == 0)
        {
            LogInfo("[ScriptingService] OnValidateScripts {}/{}", index, count);
        }
        ++index;

        if (!classDef)
        {
            continue;
        }

        if (classDef->flags.isNative)
        {
            if (classDef->name == ISerializableTypeName)
            {
                classDef->flags.isStruct = true;
                classDef->parent = nullptr;
            }
            else if (classDef->name == audioEventElementTypeName || classDef->name == entLODDefinitionTypeName)
            {
                classDef->flags.isStruct = true;
                if (auto serializableDef = aBundle->definitionsByName.Get(ISerializableTypeName))
                {
                    if (*serializableDef)
                    {
                        classDef->parent = reinterpret_cast<Red::ScriptClass*>(*serializableDef);
                    }
                }
            }
            else if (!classDef->flags.isStruct)
            {
                auto parentDef = classDef->parent;
                while (parentDef)
                {
                    if (parentDef->name == IScriptableTypeName)
                    {
                        break;
                    }
                    if (parentDef->name == ISerializableTypeName)
                    {
                        classDef->flags.isStruct = true;
                        break;
                    }
                    parentDef = parentDef->parent;
                }
            }

            if (classDef->flags.isStruct && classDef->name != inkWidgetLibraryResourceTypeName)
            {
                if (auto nativeClass = Red::GetScriptClass(classDef->name))
                {
                    classDef->flags.isAbstract = nativeClass->flags.isAbstract;

                    if (!classDef->parent && nativeClass->parent)
                    {
                        auto nativeParent = nativeClass->parent;
                        auto parentDef = aBundle->definitionsByName.Get(nativeParent->name);

                        if (!parentDef)
                        {
                            if (auto parentAlias = Red::GetScriptAlias(nativeParent->name))
                            {
                                parentDef = aBundle->definitionsByName.Get(parentAlias);
                            }
                        }

                        if (parentDef && *parentDef)
                        {
                            classDef->parent = reinterpret_cast<Red::ScriptClass*>(*parentDef);
                        }
                    }
                }
            }
        }
    }

    LogInfo("[ScriptingService] OnValidateScripts done");
}

bool App::ScriptingService::OnValidateProperty(void* aValidator, Red::ScriptProperty* aScriptProp,
                                               Red::CProperty* aNativeProp)
{
    if (!aScriptProp || !aNativeProp || !aScriptProp->type || !aNativeProp->type)
    {
        return false;
    }

    auto* scriptType = aScriptProp->type;
    auto* nativeType = aNativeProp->type;

    if (scriptType->metaType == Red::EScriptType::DynArray ||
        scriptType->metaType == Red::EScriptType::StaticArray)
    {
        return OnValidateScriptType(nativeType, scriptType);
    }

    if (nativeType->GetType() == Red::ERTTIType::BitField && scriptType->metaType == Red::EScriptType::Primitive)
    {
        return true;
    }

    return Raw::ScriptValidator::ValidateProperty(aValidator, aScriptProp, aNativeProp);
}

bool App::ScriptingService::OnValidateScriptType(Red::CBaseRTTIType* aNativeType, Red::ScriptType* aScriptType)
{
    if (!aNativeType || !aScriptType)
    {
        return false;
    }

    if (aScriptType->metaType == Red::EScriptType::DynArray)
    {
        if (aNativeType->GetType() == Red::ERTTIType::Array)
        {
            auto nativeInnerType = reinterpret_cast<Red::CRTTIBaseArrayType*>(aNativeType)->innerType;
            auto scriptInnerType = reinterpret_cast<Red::ScriptType*>(aScriptType->innerType);

            return OnValidateScriptType(nativeInnerType, scriptInnerType);
        }
        return false;
    }

    if (aScriptType->metaType == Red::EScriptType::StaticArray)
    {
        if (aNativeType->GetType() == Red::ERTTIType::StaticArray ||
            aNativeType->GetType() == Red::ERTTIType::NativeArray ||
            aNativeType->GetType() == Red::ERTTIType::FixedArray)
        {
            auto nativeInnerType = reinterpret_cast<Red::CRTTIBaseArrayType*>(aNativeType)->innerType;
            auto scriptInnerType = reinterpret_cast<Red::ScriptType*>(aScriptType->innerType);

            return OnValidateScriptType(nativeInnerType, scriptInnerType);
        }
        return false;
    }

    return Raw::ScriptValidator::CompareType(aNativeType, aScriptType);
}

void App::ScriptingService::OnValidateTypeName(bool& aValid, Red::CName aScriptTypeName, Red::CName aNativeTypeName)
{
    if (!aValid)
    {
        if (aScriptTypeName == ResourceWrapperName)
        {
            aValid = strncmp(ResourceReferencePrefix.data(), aNativeTypeName.ToString(),
                             ResourceReferencePrefix.size() - 1) == 0;
        }
        else if (aScriptTypeName == ResourceAsyncWrapperName || aScriptTypeName == ResourceScriptReferenceAlias)
        {
            aValid = strncmp(ResourceAsyncReferencePrefix.data(), aNativeTypeName.ToString(),
                             ResourceAsyncReferencePrefix.size() - 1) == 0;
        }
        else if (aScriptTypeName == CurveDataWrapperFloatName || aScriptTypeName == CurveDataWrapperVector2Name ||
                 aScriptTypeName == CurveDataWrapperVector3Name || aScriptTypeName == CurveDataWrapperVector4Name ||
                 aScriptTypeName == CurveDataWrapperHDRColorName)
        {
            aValid = strncmp(CurveDataPrefix.data(), aNativeTypeName.ToString(), CurveDataPrefix.size() - 1) == 0;
        }
    }
}

void App::ScriptingService::OnRegisterScriptOpCodes()
{
    LogInfo("[ScriptingService] OnRegisterScriptOpCodes enter");
    Raw::ScriptOpCodes::Handlers[Raw::ScriptOpCodes::New] = &ScriptOpCodeNew;
    Raw::ScriptOpCodes::Handlers[Raw::ScriptOpCodes::Cast] = &ScriptOpCodeCast;
    LogInfo("[ScriptingService] OnRegisterScriptOpCodes done");
}

void App::ScriptingService::ScriptOpCodeNew(Red::IScriptable*, Red::CStackFrame* aFrame, void* aRet,
                                            Red::CBaseRTTIType*)
{
    if (!aFrame || !aFrame->code)
    {
        return;
    }

    auto instanceType = *reinterpret_cast<Red::CClass**>(aFrame->code);
    aFrame->code += sizeof(Red::CClass*);

    if (aRet)
    {
        *reinterpret_cast<Red::Handle<Red::ISerializable>*>(aRet) = Red::MakeScriptedHandle(instanceType);
    }
}

void App::ScriptingService::ScriptOpCodeCast(Red::IScriptable*, Red::CStackFrame* aFrame, void* aLhs,
                                             Red::CBaseRTTIType*)
{
    if (!aFrame || !aFrame->code)
    {
        return;
    }

    auto lhsType = *reinterpret_cast<Red::CClass**>(aFrame->code);
    aFrame->code += sizeof(Red::CClass*);

    auto isWeak = *reinterpret_cast<bool*>(aFrame->code);
    aFrame->code += sizeof(bool);

    if (isWeak)
    {
        Red::WeakHandle<Red::ISerializable> rhsWeak;
        Red::GetParameter(aFrame, &rhsWeak);

        if (aLhs)
        {
            auto rhs = rhsWeak.Lock();
            if (rhs && !rhs->GetType()->IsA(lhsType))
            {
                rhs.Reset();
            }

            *reinterpret_cast<Red::WeakHandle<Red::ISerializable>*>(aLhs) = rhs;
        }
    }
    else
    {
        Red::Handle<Red::ISerializable> rhs;
        Red::GetParameter(aFrame, &rhs);

        if (aLhs)
        {
            if (rhs && !rhs->GetType()->IsA(lhsType))
            {
                rhs.Reset();
            }

            *reinterpret_cast<Red::Handle<Red::ISerializable>*>(aLhs) = rhs;
        }
    }
}

void App::ScriptingService::OnCreateInstance(Red::IScriptable*& aInstance, Red::CClass* aClass, uint64_t, bool)
{
    if (aInstance && aClass && aClass->flags.isScriptedClass)
    {
        Red::CallVirtual(aInstance, aClass, "OnConstruct");
    }
}

void App::ScriptingService::GetScriptGameInstance(Red::IScriptable*, Red::CStackFrame* aFrame,
                                                  Red::ScriptGameInstance* aRet, Red::CBaseRTTIType*)
{
    if (!aFrame || !aFrame->code)
    {
        return;
    }

    ++aFrame->code;

    if (aRet)
    {
        *aRet = Red::ScriptGameInstance{};
    }
}
