#include "TaskbarProgress.h"

#include <QWidget>

#ifdef Q_OS_WIN
#include <windows.h>

#include <shobjidl.h>

namespace {
// Defined locally so no extra import library is needed.
const CLSID kClsidTaskbarList = {0x56FDF344, 0xFD6D, 0x11d0, {0x95, 0x8A, 0x00, 0x60, 0x97, 0xC9, 0xA0, 0x90}};
const IID kIidTaskbarList3 = {0xEA1AFB91, 0x9E28, 0x4B86, {0x90, 0xE9, 0x9E, 0x9F, 0x8A, 0x5E, 0xEF, 0xAF}};

ITaskbarList3 *tb(void *p) { return static_cast<ITaskbarList3 *>(p); }
HWND hwnd(QWidget *w) { return reinterpret_cast<HWND>(w->window()->winId()); }
} // namespace
#endif

TaskbarProgress::TaskbarProgress(QWidget *window)
    : m_window(window)
{}

TaskbarProgress::~TaskbarProgress()
{
#ifdef Q_OS_WIN
    if (m_taskbar)
        tb(m_taskbar)->Release();
#endif
}

bool TaskbarProgress::ensure()
{
#ifdef Q_OS_WIN
    if (m_taskbar)
        return true;
    void *p = nullptr;
    if (SUCCEEDED(CoCreateInstance(kClsidTaskbarList, nullptr, CLSCTX_INPROC_SERVER, kIidTaskbarList3, &p)) && p) {
        if (SUCCEEDED(tb(p)->HrInit())) {
            m_taskbar = p;
            return true;
        }
        tb(p)->Release();
    }
#endif
    return false;
}

void TaskbarProgress::setProgress(double fraction)
{
#ifdef Q_OS_WIN
    if (!ensure())
        return;
    if (m_state == State::None)
        setState(State::Normal);
    const ULONGLONG done = ULONGLONG(qBound(0.0, fraction, 1.0) * 1000.0);
    tb(m_taskbar)->SetProgressValue(hwnd(m_window), done, 1000);
#else
    Q_UNUSED(fraction);
#endif
}

void TaskbarProgress::setState(State state)
{
    m_state = state;
#ifdef Q_OS_WIN
    if (!ensure())
        return;
    TBPFLAG flag = TBPF_NOPROGRESS;
    switch (state) {
    case State::None:
        flag = TBPF_NOPROGRESS;
        break;
    case State::Normal:
        flag = TBPF_NORMAL;
        break;
    case State::Paused:
        flag = TBPF_PAUSED;
        break;
    case State::Error:
        flag = TBPF_ERROR;
        break;
    }
    tb(m_taskbar)->SetProgressState(hwnd(m_window), flag);
#endif
}
