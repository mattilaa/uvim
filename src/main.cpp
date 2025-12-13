#include "editor.h"
#include <sys/stat.h>

static bool isDirectory(const char* path)
{
    struct stat st;
    if(stat(path, &st) != 0)
        return false;
    return S_ISDIR(st.st_mode);
}

int main(int argc, char* argv[])
{
    Editor editor;

    if(argc >= 2)
    {
        // If first argument is a directory, open file browser
        if(isDirectory(argv[1]))
        {
            editor.openFileBrowser(argv[1]);
        }
        else
        {
            // Open all files as separate buffers
            for(int i = 1; i < argc; ++i)
            {
                editor.openFile(argv[i]);
            }
        }
    }

    editor.run();

    return 0;
}
