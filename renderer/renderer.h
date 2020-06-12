#ifndef RENDERER_H
#define RENDERER_H

#include <inttypes.h>

#define RENDERER_ERROR_NONE 0
#define RENDERER_ERROR_INVALIDVALUE 1
#define RENDERER_ERROR_FILEOPENFAILED 2
#define RENDERER_ERROR_FILECLOSEFAILED 3
#define RENDERER_ERROR_INSUFFICIENTMEMORY 4
#define RENDERER_ERROR_LINETOOLONG 5
#define RENDERER_ERROR_CONFIGWRONGFORMAT 6

typedef struct point {
    float x;
    float y;
    float z;
} point;

typedef struct triangle {
    point v1;
    float w1;
    point v2;
    float w2;
    point v3;
    float w3;
} triangle;

typedef struct triangles {
    size_t size;
    triangle * data;
} triangles;

typedef struct configurations {
    float lightsourcepositionx;
    float lightsourcepositiony;
    float lightsourcepositionz;
    float camerapositionx;
    float camerapositiony;
    float camerapositionz;
    float cameralookatpointx;
    float cameralookatpointy;
    float cameralookatpointz;
    float upvectorx;
    float upvectory;
    float upvectorz;
    float objectpositionx;
    float objectpositiony;
    float objectpositionz;
    float objectrotationx;
    float objectrotationy;
    float objectrotationz;
    float objectscalingx;
    float objectscalingy;
    float objectscalingz;
    float fieldofview;
    float znear;
    float zfar;
    unsigned int outputwidth;
    unsigned int outputheight;
    int materialdiffusereflectancered;
    int materialdiffusereflectancegreen;
    int materialdiffusereflectanceblue;
    int backfaceculling;
    int usezbuffer;
} configurations;

typedef struct surface {
    uint16_t width;
    uint16_t height;
    uint32_t pixels[1];
} surface;

int geterror(void);
const char * geterrortext(int);

size_t loadrawtriangles(const char *, triangles *);
void releasetriangles(triangles *);

void readconfigurations(void);
void getconfigurations(configurations *);

surface * createsurface(uint16_t, uint16_t);
surface * createrendertarget(void);
void releasesurface(surface * *);
void rendersurface(const triangles *, surface *);
void savesurfacetopngfile(const surface *, const char *);

#endif
