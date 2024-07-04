//
// Created by 李振 on 2024/7/5.
//

#include "VETrace.h"
#include "Log.h"
#include <unwind.h>
#include <dlfcn.h>
#include <cxxabi.h>

struct BacktraceState {
    void** current;
    void** end;
};

static _Unwind_Reason_Code trace_function(struct _Unwind_Context* context, void* arg) {
    BacktraceState* state = static_cast<BacktraceState*>(arg);
    uintptr_t pc = _Unwind_GetIP(context);
    if (pc) {
        if (state->current == state->end) {
            return _URC_END_OF_STACK;
        } else {
            *state->current++ = reinterpret_cast<void*>(pc);
        }
    }
    return _URC_NO_REASON;
}

size_t capture_backtrace(void** buffer, size_t max) {
    BacktraceState state = {buffer, buffer + max};
    _Unwind_Backtrace(trace_function, &state);
    return state.current - buffer;
}

void dump_backtrace(void** buffer, size_t count) {
    for (size_t idx = 0; idx < count; ++idx) {
        const void* addr = buffer[idx];
        const char* symbol = "";
        char* demangled = NULL;

        Dl_info info;
        if (dladdr(addr, &info) && info.dli_sname) {
            symbol = info.dli_sname;

            // 使用 __cxa_demangle 解码符号名
            int status;
            demangled = abi::__cxa_demangle(symbol, nullptr, nullptr, &status);
            if (status == 0 && demangled) {
                symbol = demangled;
            }
        }

        ALOGD("backtrace #%zu: %p %s", idx, addr, symbol);

        if (demangled) {
            free(demangled);
        }
    }
}

void backTrace(){
    const size_t max_frames = 64;
    void* buffer[max_frames];
    size_t count = capture_backtrace(buffer, max_frames);
    dump_backtrace(buffer, count);
}