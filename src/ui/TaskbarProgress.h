#pragma once

class QWidget;

// Shows encode progress on the window's Windows taskbar button (no-op on other platforms).
class TaskbarProgress
{
public:
    enum class State { None, Normal, Paused, Error };

    explicit TaskbarProgress(QWidget *window);
    ~TaskbarProgress();
    TaskbarProgress(const TaskbarProgress &) = delete;
    TaskbarProgress &operator=(const TaskbarProgress &) = delete;

    void setProgress(double fraction);
    void setState(State state);

private:
    bool ensure();

    QWidget *m_window;
    void *m_taskbar = nullptr; // ITaskbarList3*
    State m_state = State::None;
};
