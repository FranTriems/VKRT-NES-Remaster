#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <avrt.h>
#include <chrono>
#include <stdexcept>
#include "vkrt/nes_native_s0/runtime.hpp"

namespace vkrt::nes::host {
class Handle {
public:
    explicit Handle(HANDLE h=nullptr):value(h){}
    ~Handle(){if(value)CloseHandle(value);}
    Handle(const Handle&)=delete;
    Handle& operator=(const Handle&)=delete;
    HANDLE value;
};
class Mmcss {
public:
    explicit Mmcss(const wchar_t* task) {
        module_=LoadLibraryW(L"avrt.dll");
        if(!module_)return;
        auto set=reinterpret_cast<decltype(&AvSetMmThreadCharacteristicsW)>(GetProcAddress(module_,"AvSetMmThreadCharacteristicsW"));
        revert_=reinterpret_cast<decltype(&AvRevertMmThreadCharacteristics)>(GetProcAddress(module_,"AvRevertMmThreadCharacteristics"));
        DWORD index=0;
        if(set&&revert_)registration_=set(task,&index);
    }
    ~Mmcss(){if(registration_)revert_(registration_);if(module_)FreeLibrary(module_);}
    Mmcss(const Mmcss&)=delete;
    Mmcss& operator=(const Mmcss&)=delete;
private:
    HMODULE module_{};HANDLE registration_{};
    decltype(&AvRevertMmThreadCharacteristics) revert_{};
};
using Clock=std::chrono::steady_clock;
inline std::uint64_t now_ns(){return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();}
class DeadlineTimer {
public:
    DeadlineTimer():timer_(CreateWaitableTimerExW(nullptr,nullptr,0x2,TIMER_ALL_ACCESS)) {
        if(!timer_.value)timer_.value=CreateWaitableTimerExW(nullptr,nullptr,0,TIMER_ALL_ACCESS);
        if(!timer_.value)throw std::runtime_error("waitable timer creation failed");
    }
    bool wait(std::uint64_t deadline,HANDLE stop) {
        const auto now=now_ns();if(now>=deadline)return WaitForSingleObject(stop,0)!=WAIT_OBJECT_0;
        LARGE_INTEGER due;due.QuadPart=runtime::relative_timer_due(deadline-now);
        if(!SetWaitableTimer(timer_.value,&due,0,nullptr,nullptr,FALSE))throw std::runtime_error("waitable timer arm failed");
        HANDLE events[]{stop,timer_.value};
        const auto result=WaitForMultipleObjects(2,events,FALSE,INFINITE);
        if(result==WAIT_FAILED)throw std::runtime_error("waitable timer wait failed");
        return result==WAIT_OBJECT_0+1;
    }
private:
    Handle timer_;
};
}
