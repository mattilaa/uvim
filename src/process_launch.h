#pragma once

#include <mutex>

// Serializes the short interval between creating inheritable child pipe ends
// and launching a process. No process may inherit another launcher's temporary
// handles/descriptors. Never hold this lock while waiting for a child.
inline std::mutex& processLaunchMutex()
{
    static std::mutex mutex;
    return mutex;
}
