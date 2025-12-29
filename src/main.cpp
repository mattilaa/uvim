#include "editor_lsp_query.h"
#include <string>
#include <sys/stat.h>
#include <vector>

static bool isDirectory(const char* path)
{
    struct stat st;
    if(stat(path, &st) != 0)
        return false;
    return S_ISDIR(st.st_mode);
}

int main(int argc, char* argv[])
{
    bool useClangd = false;
    std::string ccdir;
    std::string clangdPath = "clangd";
    std::string queryDriver;
    std::vector<std::string> args;

    // Parse flags first; remaining args are files/dirs.
    for(int i = 1; i < argc; ++i)
    {
        std::string a = argv[i];
        if(a == "--clangd")
        {
            useClangd = true;
        }
        else if(a == "--ccdir" && i + 1 < argc)
        {
            ccdir = argv[++i];
        }
        else if(a == "--clangd-path" && i + 1 < argc)
        {
            clangdPath = argv[++i];
        }
        else if(a == "--query-driver" && i + 1 < argc)
        {
            queryDriver = argv[++i];
        }
        else
        {
            args.push_back(a);
        }
    }

    // Create editor with flag indicating whether we have files to open
    Editor editor(!args.empty());

    if(useClangd)
    {
        editor.enableClangdLsp(true, ccdir, clangdPath, queryDriver);
    }

    if(!args.empty())
    {
        // If first argument is a directory, open file browser
        if(isDirectory(args[0].c_str()))
        {
            editor.openFileBrowser(args[0]);
        }
        else
        {
            // Open all files as separate buffers
            for(const auto& f : args)
            {
                editor.openFile(f);
            }
        }
    }

    editor.run();
    return 0;
}
