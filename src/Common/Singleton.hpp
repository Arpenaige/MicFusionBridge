#pragma once
#include <memory>
#include <mutex>
#include <format>

//#include "GlobalSystemTimer.hpp"

#include <shlobj_core.h >

#include "P7/P7_Client.h"
#include "P7/P7_Trace.h"

#include <wil/resource.h>

template<typename T>
class Singleton
{
public:
    const static T& GetInstance()
    {
        std::call_once(m_Once, []()
            {
                m_Instance = std::make_unique<T>();
            });

        return *m_Instance.get();
    }

private:
    Singleton() = default;
    ~Singleton() = default;
    Singleton<T>(const Singleton&) = delete;
    Singleton<T>(Singleton&&) = delete;
    Singleton& operator=(const Singleton&) = delete;
    Singleton& operator=(Singleton&&) = delete;

    static std::once_flag m_Once;
    static std::unique_ptr<T> m_Instance;
};
template<typename T> std::unique_ptr<T> Singleton<T>::m_Instance;
template<typename T> std::once_flag Singleton<T>::m_Once;


//TODO: сделать в будущем void Set_Verbosity(System.IntPtr i_hModule, Traces.Level i_eLevel)
//TODO: так же думаю в будущем можно разделить на файлы(TRACE, ERROR и тд.).
//TODO: еще можно в LockForProcess получать имя аудиоустройства и выводить его в логе в имени потока с помощью P7_Trace_Register_Thread

static std::wstring GetCurrentModuleName()
{
    HMODULE CurrentModuleHandle;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
        reinterpret_cast<LPCWSTR>(&GetCurrentModuleName), &CurrentModuleHandle))
    {
        std::wstring CurrentModulePath(MAX_PATH, L'\0');
        if (GetModuleFileNameW(CurrentModuleHandle, CurrentModulePath.data(), CurrentModulePath.size()))
        {
            if (size_t Off = CurrentModulePath.find_last_of(L'\\');
                Off != std::wstring::npos)
            {
                CurrentModulePath.erase(0, Off + 1llu);

                if (Off = CurrentModulePath.find_last_of(L'.');
                    Off != std::wstring::npos)
                {
                    CurrentModulePath.erase(Off, CurrentModulePath.size() - Off);

                    if (Off = CurrentModulePath.find_last_of(L' ');
                        Off != std::wstring::npos)
                    {
                        CurrentModulePath.erase(Off, 1);
                    }
                    return CurrentModulePath;
                }
            }
        }
    }
    return {};
}

class AbstractLogger
{
public:
    AbstractLogger(const wchar_t* NameTempTodoRename)
    {
        //TODO: archive into .zip all old log in current directory and delete old logs
        //Можно сделать архивирование и в другом потоке, сформировать просто std::vector<std::string> и в другом потоке архивировать, можно даже с максимальным сжатием для экономии места у пользователя

        P7_Set_Crash_Handler();

        bool PathGettingError;

        wil::unique_cotaskmem_ptr<wchar_t[]> File;
        if (SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, wil::out_param(File)) == S_OK)
        {
            PathGettingError = false;
        }
        else
        {
            PathGettingError = true;
        }

        std::wstring ClientConfiguration;
        if (NameTempTodoRename)
        {
            ClientConfiguration = std::format(L"/P7.Sink={} /P7.Dir={}\\Temp\\{}\\Logs /P7.Format={}",
                PathGettingError ? L"Null" : L"FileTxt",
                PathGettingError ? L"" : File.get(),
                NameTempTodoRename,
                L"\"%lv | %tf | TID = %ti : %tn | %fn | %ms | %fs: %fl\"");
        }
        else
        {
            std::wstring CurrentModuleName = GetCurrentModuleName();
            if (CurrentModuleName.empty())
            {
                CurrentModuleName = L"MicFusionBridgeAbstract";
            }

            ClientConfiguration = std::format(L"/P7.Sink={} /P7.Dir={}\\Temp\\{}\\Logs /P7.Format={}",
                PathGettingError ? L"Null" : L"FileTxt",
                PathGettingError ? L"" : File.get(),
                CurrentModuleName,
                L"\"%lv | %tf | TID = %ti : %tn | %fn | %ms | %fs: %fl\"");
        }

        if (m_pClient = P7_Create_Client(ClientConfiguration.c_str());
            !m_pClient)
        {
            //ERROR
            //TODO: на будущее, тут можно логировать будет GetLastError и прям думаю сразу в реестр как вариант(если будет доступно по правам)
        }

        if (m_pClient)
        {
            if (m_pTrace = P7_Create_Trace(m_pClient, TM("MainTrace"));
                !m_pTrace)
            {
                //ERROR
            }
        }
    }

    virtual ~AbstractLogger()
    {
        P7_Flush();

        if (m_pTrace)
        {
            m_pTrace->Release();
        }

        if (m_pClient)
        {
            m_pClient->Release();
        }
    }

    const IP7_Client* GetClientPtr() const
    {
        return m_pClient;
    }

    IP7_Trace* GetTracePtr() const
    {
        return m_pTrace;
    }

private:
    IP7_Client* m_pClient = nullptr;
    IP7_Trace* m_pTrace = nullptr;
};

//Based on current modulname, e.x. if MicFusionBridge.dll loaded in audiodg.exe - folder name for logging = MicFusionBridge,
//if Microphone Streamer.vst3 loaded in FL64.exe - folder name for logging = Microphone Streamer,
//If MicFusionBridgeControl.exe - folder name for logging = MicFusionBridgeControl
class VariableMainLogger : public AbstractLogger
{
public:
    VariableMainLogger() : AbstractLogger(nullptr)
    {

    }

    virtual ~VariableMainLogger()
    {

    }

private:

};

#define SafePointerDereference(x, y) if(auto RawPointer = x; RawPointer) {RawPointer->##y;}

//TODO: Test this macro
#define LoggerTimeout(LoggerContext, SetTimeOut) { \
static_assert(std::is_floating_point<decltype(SetTimeOut)>::value, "LoggerTimeout need float argument only [seconds]"); \
static GlobalSystemTimer<float> gst; static float fNextCallTimeTarget = gst.GetOffsetedSystemTimeFromStart(); \
if(const float fLastCallTimeThisBlock = gst.GetOffsetedSystemTimeFromStart(); fLastCallTimeThisBlock >= fNextCallTimeTarget) \
  { ##LoggerContext; fNextCallTimeTarget = gst.GetOffsetedSystemTimeFromStart() + SetTimeOut;} \
}