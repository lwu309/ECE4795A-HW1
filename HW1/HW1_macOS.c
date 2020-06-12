#include <stdio.h>

#include "../renderer/renderer.h"

int main(int argc, char * argv[])
{
    if (argc != 3) {
        puts("Usage:\n    ./HW1 [path to RAW triangle file] [path to output PNG file]");
        return 0;
    }
    readconfigurations();
    if (geterror() != RENDERER_ERROR_NONE) {
        fputs(geterrortext(geterror()), stderr);
        return 1;
    }
    triangles rawtriangles = {0};
    loadrawtriangles(argv[1], &rawtriangles);
    if (geterror() != RENDERER_ERROR_NONE) {
        fputs(geterrortext(geterror()), stderr);
        return 1;
    }
    surface * rendertarget = createrendertarget();
    if (geterror() != RENDERER_ERROR_NONE) {
        fputs(geterrortext(geterror()), stderr);
        return 1;
    }
    rendersurface(&rawtriangles, rendertarget);
    if (geterror() != RENDERER_ERROR_NONE) {
        fputs(geterrortext(geterror()), stderr);
        return 1;
    }
    releasetriangles(&rawtriangles);
    savesurfacetopngfile(rendertarget, argv[2]);
    if (geterror() != RENDERER_ERROR_NONE) {
        fputs(geterrortext(geterror()), stderr);
        return 1;
    }
    releasesurface(&rendertarget);
    return 0;
}
