#if _MSC_VER >= 1400
#define _CRT_SECURE_NO_DEPRECATE
#endif

#include "renderer.h"

#if defined(__APPLE__) && defined(__MACH__)
#include <Accelerate/Accelerate.h>
#else
#include <cblas.h>
#endif
#include <confini.h>
#if defined(__APPLE__) && defined(__MACH__)
#include <png/png.h>
#else
#include <png.h>
#endif

#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct vector {
    float x;
    float y;
    float z;
} vector;

typedef struct light {
    float red;
    float green;
    float blue;
} light;

typedef struct polygon {
    size_t size;
    point vertices[9];
} polygon;

int errornumber = 0;
const char * errortexts[] = {
    "No error",
    "Invalid argument value",
    "Failed to open file",
    "Failed to close file",
    "Out of memory",
    "RAW triangle file line too long (over 1024 characters)",
    "Configuration wrong format"
};

char inisection[64] = {'\0'};

const float identitymatrix[16] = {
    1.F, 0.F, 0.F, 0.F,
    0.F, 1.F, 0.F, 0.F,
    0.F, 0.F, 1.F, 0.F,
    0.F, 0.F, 0.F, 1.F
};

float previousmatrix[16];

point lightsourceposition = {0.F, 0.F, 0.F};
point cameraposition = {0.F, 0.F, 0.F};
point cameralookatpoint = {0.F, 0.F, 0.F};
vector up = {0.F, 1.F, 0.F};
point objectposition = {0.F, 0.F, 0.F};
float objectrotationx = 0.F;
float objectrotationy = 0.F;
float objectrotationz = 0.F;
float objectrotationxdegree = 0.F;
float objectrotationydegree = 0.F;
float objectrotationzdegree = 0.F;
float objectscalingx = 1.F;
float objectscalingy = 1.F;
float objectscalingz = 1.F;
float fieldofview = 3.14159265F / 2.F;
float fieldofviewdegree = 90.F;
float znear = 0.1F;
float zfar = 100.F;
unsigned int outputwidth = 600U;
unsigned int outputheight = 600U;
light materialdiffusereflectance = {1.F, 1.F, 1.F};
int materialdiffusereflectancered = 255;
int materialdiffusereflectancegreen = 255;
int materialdiffusereflectanceblue = 255;
bool backfaceculling = false;
bool usezbuffer = false;

/* Helper functions for INI parsing */
static int inicallback(IniDispatch *, void *);
static float degreetoradian(float);
static bool ishexadecimalcharacter(char);
static int hexadecimalcharactertovalue(char);

/* Helper functions for vector operations */
static float dotproduct(const vector *, const vector *);
static void crossproduct(vector *, const vector *, const vector *);
static void normalize(vector *);

/* Helper functions for matrix operations */
static void calculatenewtransformationmatrix(float *, const float *);

/* Helper function for Z-sorting using quicksort algorithm */
static void zsortingsubroutine(triangle *, light *, intptr_t, intptr_t);

int geterror(void)
{
    return errornumber;
}

const char * geterrortext(int number)
{
    if (number >= RENDERER_ERROR_NONE && number <= RENDERER_ERROR_CONFIGWRONGFORMAT) {
        return errortexts[number];
    } else {
        return NULL;
    }
}

size_t loadrawtriangles(const char * filename, triangles * rawtriangles)
{
    char line[1024];

    if (rawtriangles->size != 0 || rawtriangles->data != NULL) {
        errornumber = RENDERER_ERROR_INVALIDVALUE;
        return 0;
    }

    FILE * filepointer = fopen(filename, "r");
    if (filepointer == NULL) {
        errornumber = RENDERER_ERROR_FILEOPENFAILED;
        return 0;
    }

    for (;;) {
        int c = fgetc(filepointer);
        if (c == EOF) {
            break;
        } else if (c == '\n') {
            continue;
        } else {
            size_t i;
            line[0] = (char)c;
            for (i = 1; i < 1024; i += 1) {
                int c = fgetc(filepointer);
                if (c == EOF || c == '\n') {
                    line[i] = '\0';
                    break;
                } else {
                    line[i] = (char)c;
                }
            }
            if (i == 1024) {
                releasetriangles(rawtriangles);
                errornumber = RENDERER_ERROR_LINETOOLONG;
                return 0;
            }

            triangle newtriangle;
            if (sscanf(line, "%f%f%f%f%f%f%f%f%f", &newtriangle.v1.x, &newtriangle.v1.y, &newtriangle.v1.z, &newtriangle.v2.x, &newtriangle.v2.y, &newtriangle.v2.z, &newtriangle.v3.x, &newtriangle.v3.y, &newtriangle.v3.z) != 9) {
                continue;
            }
            newtriangle.w1 = 1.F;
            newtriangle.w2 = 1.F;
            newtriangle.w3 = 1.F;

            rawtriangles->size += 1;
            triangle * newrawtrianglesdata = realloc(rawtriangles->data, rawtriangles->size * sizeof(triangle));
            if (newrawtrianglesdata == NULL) {
                releasetriangles(rawtriangles);
                fclose(filepointer);
                errornumber = RENDERER_ERROR_INSUFFICIENTMEMORY;
                return 0;
            }
            rawtriangles->data = newrawtrianglesdata;
            rawtriangles->data[rawtriangles->size - 1] = newtriangle;
        }
    }

    if (fclose(filepointer) == EOF) {
        errornumber = RENDERER_ERROR_FILECLOSEFAILED;
        if (rawtriangles->data != NULL) {
            releasetriangles(rawtriangles);
        }
        return 0;
    }

    errornumber = RENDERER_ERROR_NONE;
    return rawtriangles->size;
}

void releasetriangles(triangles * rawtriangles)
{
    rawtriangles->size = 0;
    if (rawtriangles->data != NULL) {
        free(rawtriangles->data);
        rawtriangles->data = NULL;
    }
}

void readconfigurations(void)
{
    load_ini_path("renderer.ini", INI_DEFAULT_FORMAT, NULL, inicallback, NULL);
    memset(inisection, 0, sizeof inisection);
    if (errornumber == RENDERER_ERROR_NONE && znear >= zfar) {
        errornumber = RENDERER_ERROR_INVALIDVALUE;
    }
}

void getconfigurations(configurations * configstruct)
{
    if (configstruct != NULL) {
        configstruct->lightsourcepositionx = lightsourceposition.x;
        configstruct->lightsourcepositiony = lightsourceposition.y;
        configstruct->lightsourcepositionz = lightsourceposition.z;
        configstruct->camerapositionx = cameraposition.x;
        configstruct->camerapositiony = cameraposition.y;
        configstruct->camerapositionz = cameraposition.z;
        configstruct->cameralookatpointx = cameralookatpoint.x;
        configstruct->cameralookatpointy = cameralookatpoint.y;
        configstruct->cameralookatpointz = cameralookatpoint.z;
        configstruct->upvectorx = up.x;
        configstruct->upvectory = up.y;
        configstruct->upvectorz = up.z;
        configstruct->objectpositionx = objectposition.x;
        configstruct->objectpositiony = objectposition.y;
        configstruct->objectpositionz = objectposition.z;
        configstruct->objectrotationx = objectrotationxdegree;
        configstruct->objectrotationy = objectrotationydegree;
        configstruct->objectrotationz = objectrotationzdegree;
        configstruct->objectscalingx = objectscalingx;
        configstruct->objectscalingy = objectscalingy;
        configstruct->objectscalingz = objectscalingz;
        configstruct->fieldofview = fieldofviewdegree;
        configstruct->znear = znear;
        configstruct->zfar = zfar;
        configstruct->outputwidth = outputwidth;
        configstruct->outputheight = outputheight;
        configstruct->materialdiffusereflectancered = materialdiffusereflectancered;
        configstruct->materialdiffusereflectancegreen = materialdiffusereflectancegreen;
        configstruct->materialdiffusereflectanceblue = materialdiffusereflectanceblue;
        configstruct->backfaceculling = backfaceculling ? 1 : 0;
        configstruct->usezbuffer = usezbuffer ? 1 : 0;
        errornumber = RENDERER_ERROR_NONE;
    } else {
        errornumber = RENDERER_ERROR_INVALIDVALUE;
    }
}

surface * createsurface(uint16_t width, uint16_t height)
{
    surface * newsurface = malloc(sizeof(surface) + ((size_t)width * (size_t)height - 1) * sizeof(uint32_t));
    if (newsurface == NULL) {
        errornumber = RENDERER_ERROR_INSUFFICIENTMEMORY;
        return NULL;
    }
    newsurface->width = (uint16_t)width;
    newsurface->height = (uint16_t)height;
    memset(newsurface->pixels, 0, (size_t)newsurface->width * (size_t)newsurface->height * sizeof(uint32_t));
    errornumber = RENDERER_ERROR_NONE;
    return newsurface;
}

surface * createrendertarget(void)
{
    return createsurface(outputwidth, outputheight);
}

void releasesurface(surface * * s)
{
    if (*s != NULL) {
        free(*s);
        *s = NULL;
    }
}

void rendersurface(const triangles * rawtriangles, surface * target)
{
    /* Clear render target surface */
    memset(target->pixels, 0, (size_t)target->width * (size_t)target->height * sizeof(uint32_t));

    triangles transformedtriangles;
    transformedtriangles.size = 0;
    transformedtriangles.data = malloc(rawtriangles->size * sizeof(triangle));
    if (transformedtriangles.data == NULL) {
        releasetriangles(&transformedtriangles);
        errornumber = RENDERER_ERROR_INSUFFICIENTMEMORY;
        return;
    }

    /* Model-space backface culling */
    point modelspacecameraposition;
    point intermediatepoint;
    float sinthetax = sinf(objectrotationx);
    float costhetax = cosf(objectrotationx);
    float sinthetay = sinf(objectrotationy);
    float costhetay = cosf(objectrotationy);
    float sinthetaz = sinf(objectrotationz);
    float costhetaz = cosf(objectrotationz);
    void * reallocpointer = NULL;
    if (backfaceculling) {
        intermediatepoint.x = cameraposition.x - objectposition.x;
        intermediatepoint.y = cameraposition.y - objectposition.y;
        intermediatepoint.z = cameraposition.z - objectposition.z;
        modelspacecameraposition.x = costhetaz * intermediatepoint.x + sinthetaz * intermediatepoint.y;
        modelspacecameraposition.y = -sinthetaz * intermediatepoint.x + costhetaz * intermediatepoint.y;
        modelspacecameraposition.z = intermediatepoint.z;
        intermediatepoint.x = costhetay * modelspacecameraposition.x + -sinthetay * modelspacecameraposition.z;
        intermediatepoint.y = modelspacecameraposition.y;
        intermediatepoint.z = sinthetay * modelspacecameraposition.x + costhetay * modelspacecameraposition.z;
        modelspacecameraposition.x = intermediatepoint.x / objectscalingx;
        modelspacecameraposition.y = (costhetax * intermediatepoint.y + sinthetax * intermediatepoint.z) / objectscalingy;
        modelspacecameraposition.z = (-sinthetax * intermediatepoint.y + costhetax * intermediatepoint.z) / objectscalingz;
        for (size_t triangleindex = 0; triangleindex < rawtriangles->size; triangleindex += 1) {
            vector v1 = {
                rawtriangles->data[triangleindex].v2.x - rawtriangles->data[triangleindex].v1.x,
                rawtriangles->data[triangleindex].v2.y - rawtriangles->data[triangleindex].v1.y,
                rawtriangles->data[triangleindex].v2.z - rawtriangles->data[triangleindex].v1.z
            };
            vector v2 = {
                rawtriangles->data[triangleindex].v3.x - rawtriangles->data[triangleindex].v1.x,
                rawtriangles->data[triangleindex].v3.y - rawtriangles->data[triangleindex].v1.y,
                rawtriangles->data[triangleindex].v3.z - rawtriangles->data[triangleindex].v1.z
            };
            vector surfacevector;
            crossproduct(&surfacevector, &v1, &v2);
            vector eyevector = {
                modelspacecameraposition.x - (rawtriangles->data[triangleindex].v1.x + rawtriangles->data[triangleindex].v2.x + rawtriangles->data[triangleindex].v3.x) / 3.F,
                modelspacecameraposition.y - (rawtriangles->data[triangleindex].v1.y + rawtriangles->data[triangleindex].v2.y + rawtriangles->data[triangleindex].v3.y) / 3.F,
                modelspacecameraposition.z - (rawtriangles->data[triangleindex].v1.z + rawtriangles->data[triangleindex].v2.z + rawtriangles->data[triangleindex].v3.z) / 3.F
            };
            if (dotproduct(&surfacevector, &eyevector) > FLT_EPSILON) {
                transformedtriangles.data[transformedtriangles.size] = rawtriangles->data[triangleindex];
                transformedtriangles.size += 1;
            }
        }
        if (transformedtriangles.size == 0) {
            releasetriangles(&transformedtriangles);
            return;
        }
        reallocpointer = realloc(transformedtriangles.data, transformedtriangles.size * sizeof(triangle));
        if (reallocpointer == NULL) {
            releasetriangles(&transformedtriangles);
            errornumber = RENDERER_ERROR_INSUFFICIENTMEMORY;
            return;
        }
        transformedtriangles.data = reallocpointer;
    } else {
        transformedtriangles.size = rawtriangles->size;
        memcpy(transformedtriangles.data, rawtriangles->data, rawtriangles->size * sizeof(triangle));
    }

    float transformationmatrix[16];
    memcpy(transformationmatrix, identitymatrix, sizeof identitymatrix);
    float operatormatrix[16];

    /* Scaling */
    transformationmatrix[0] = objectscalingx;
    transformationmatrix[5] = objectscalingy;
    transformationmatrix[10] = objectscalingz;

    /* Rotation along X axis */
    operatormatrix[0] = 1.F;
    operatormatrix[1] = 0.F;
    operatormatrix[2] = 0.F;
    operatormatrix[3] = 0.F;
    operatormatrix[4] = 0.F;
    operatormatrix[5] = costhetax;
    operatormatrix[6] = sinthetax;
    operatormatrix[7] = 0.F;
    operatormatrix[8] = 0.F;
    operatormatrix[9] = -sinthetax;
    operatormatrix[10] = costhetax;
    operatormatrix[11] = 0.F;
    operatormatrix[12] = 0.F;
    operatormatrix[13] = 0.F;
    operatormatrix[14] = 0.F;
    operatormatrix[15] = 1.F;
    calculatenewtransformationmatrix(transformationmatrix, operatormatrix);

    /* Rotation along Y axis */
    operatormatrix[0] = costhetay;
    operatormatrix[1] = 0.F;
    operatormatrix[2] = -sinthetay;
    operatormatrix[3] = 0.F;
    operatormatrix[4] = 0.F;
    operatormatrix[5] = 1.F;
    operatormatrix[6] = 0.F;
    operatormatrix[7] = 0.F;
    operatormatrix[8] = sinthetay;
    operatormatrix[9] = 0.F;
    operatormatrix[10] = costhetay;
    operatormatrix[11] = 0.F;
    operatormatrix[12] = 0.F;
    operatormatrix[13] = 0.F;
    operatormatrix[14] = 0.F;
    operatormatrix[15] = 1.F;
    calculatenewtransformationmatrix(transformationmatrix, operatormatrix);

    /* Rotation along Z axis */
    operatormatrix[0] = costhetaz;
    operatormatrix[1] = sinthetaz;
    operatormatrix[2] = 0.F;
    operatormatrix[3] = 0.F;
    operatormatrix[4] = -sinthetaz;
    operatormatrix[5] = costhetaz;
    operatormatrix[6] = 0.F;
    operatormatrix[7] = 0.F;
    operatormatrix[8] = 0.F;
    operatormatrix[9] = 0.F;
    operatormatrix[10] = 1.F;
    operatormatrix[11] = 0.F;
    operatormatrix[12] = 0.F;
    operatormatrix[13] = 0.F;
    operatormatrix[14] = 0.F;
    operatormatrix[15] = 1.F;
    calculatenewtransformationmatrix(transformationmatrix, operatormatrix);

    /* Translation of object position */
    operatormatrix[0] = 1.F;
    operatormatrix[1] = 0.F;
    operatormatrix[2] = 0.F;
    operatormatrix[3] = 0.F;
    operatormatrix[4] = 0.F;
    operatormatrix[5] = 1.F;
    operatormatrix[6] = 0.F;
    operatormatrix[7] = 0.F;
    operatormatrix[8] = 0.F;
    operatormatrix[9] = 0.F;
    operatormatrix[10] = 1.F;
    operatormatrix[11] = 0.F;
    operatormatrix[12] = objectposition.x;
    operatormatrix[13] = objectposition.y;
    operatormatrix[14] = objectposition.z;
    operatormatrix[15] = 1.F;
    calculatenewtransformationmatrix(transformationmatrix, operatormatrix);

    /* View transformation matrix */
    vector zaxis = {cameralookatpoint.x - cameraposition.x, cameralookatpoint.y - cameraposition.y, cameralookatpoint.z - cameraposition.z};
    normalize(&zaxis);
    vector xaxis;
    crossproduct(&xaxis, &up, &zaxis);
    normalize(&xaxis);
    vector yaxis;
    crossproduct(&yaxis, &zaxis, &xaxis);
    operatormatrix[0] = xaxis.x;
    operatormatrix[1] = yaxis.x;
    operatormatrix[2] = zaxis.x;
    operatormatrix[3] = 0.F;
    operatormatrix[4] = xaxis.y;
    operatormatrix[5] = yaxis.y;
    operatormatrix[6] = zaxis.y;
    operatormatrix[7] = 0.F;
    operatormatrix[8] = xaxis.z;
    operatormatrix[9] = yaxis.z;
    operatormatrix[10] = zaxis.z;
    operatormatrix[11] = 0.F;
    operatormatrix[12] = -dotproduct(&xaxis, (const vector *)&cameraposition);
    operatormatrix[13] = -dotproduct(&yaxis, (const vector *)&cameraposition);
    operatormatrix[14] = -dotproduct(&zaxis, (const vector *)&cameraposition);
    operatormatrix[15] = 1.F;
    calculatenewtransformationmatrix(transformationmatrix, operatormatrix);

    /* Model-view transformation */
    triangle * newdata = malloc(transformedtriangles.size * sizeof(triangle));
    if (newdata == NULL) {
        releasetriangles(&transformedtriangles);
        errornumber = RENDERER_ERROR_INSUFFICIENTMEMORY;
        return;
    }
    cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans, (int)transformedtriangles.size * 3, 4, 4, 1.F, (float *)transformedtriangles.data, 4, transformationmatrix, 4, 0.F, (float *)newdata, 4);
    triangle * temp = transformedtriangles.data;
    transformedtriangles.data = newdata;
    newdata = temp;

    /* Calculate view-space position of light source */
    point viewspacelightsourceposition = {
        operatormatrix[0] * lightsourceposition.x + operatormatrix[4] * lightsourceposition.y + operatormatrix[8] * lightsourceposition.z + operatormatrix[12],
        operatormatrix[1] * lightsourceposition.x + operatormatrix[5] * lightsourceposition.y + operatormatrix[9] * lightsourceposition.z + operatormatrix[13],
        operatormatrix[2] * lightsourceposition.x + operatormatrix[6] * lightsourceposition.y + operatormatrix[10] * lightsourceposition.z + operatormatrix[14]
    };

    /* Create lighting table */
    light * lightingtable = malloc(transformedtriangles.size * sizeof(light));
    if (lightingtable == NULL) {
        free(newdata);
        releasetriangles(&transformedtriangles);
        errornumber = RENDERER_ERROR_INSUFFICIENTMEMORY;
        return;
    }

    /* Calculate lighting */
    for (size_t triangleindex = 0; triangleindex < transformedtriangles.size; triangleindex += 1) {
        vector v1 = {
            transformedtriangles.data[triangleindex].v2.x - transformedtriangles.data[triangleindex].v1.x,
            transformedtriangles.data[triangleindex].v2.y - transformedtriangles.data[triangleindex].v1.y,
            transformedtriangles.data[triangleindex].v2.z - transformedtriangles.data[triangleindex].v1.z
        };
        vector v2 = {
            transformedtriangles.data[triangleindex].v3.x - transformedtriangles.data[triangleindex].v1.x,
            transformedtriangles.data[triangleindex].v3.y - transformedtriangles.data[triangleindex].v1.y,
            transformedtriangles.data[triangleindex].v3.z - transformedtriangles.data[triangleindex].v1.z
        };
        vector normalvector;
        crossproduct(&normalvector, &v1, &v2);
        normalize(&normalvector);
        vector lightvector = {
            viewspacelightsourceposition.x - (transformedtriangles.data[triangleindex].v1.x + transformedtriangles.data[triangleindex].v2.x + transformedtriangles.data[triangleindex].v3.x) / 3.F,
            viewspacelightsourceposition.y - (transformedtriangles.data[triangleindex].v1.y + transformedtriangles.data[triangleindex].v2.y + transformedtriangles.data[triangleindex].v3.y) / 3.F,
            viewspacelightsourceposition.z - (transformedtriangles.data[triangleindex].v1.z + transformedtriangles.data[triangleindex].v2.z + transformedtriangles.data[triangleindex].v3.z) / 3.F
        };
        normalize(&lightvector);
        float lambertiancosine = fmaxf(0.F, dotproduct(&normalvector, &lightvector));
        lightingtable[triangleindex].red = materialdiffusereflectance.red * lambertiancosine;
        lightingtable[triangleindex].green = materialdiffusereflectance.green * lambertiancosine;
        lightingtable[triangleindex].blue = materialdiffusereflectance.blue * lambertiancosine;
    }

    /* Perspective projection */
    float aspectratio = (float)target->width / (float)target->height;
    float yscale = 1.0F / tanf(fieldofview / 2.F);
    transformationmatrix[0] = yscale / aspectratio;
    transformationmatrix[1] = 0.F;
    transformationmatrix[2] = 0.F;
    transformationmatrix[3] = 0.F;
    transformationmatrix[4] = 0.F;
    transformationmatrix[5] = yscale;
    transformationmatrix[6] = 0.F;
    transformationmatrix[7] = 0.F;
    transformationmatrix[8] = 0.F;
    transformationmatrix[9] = 0.F;
    transformationmatrix[10] = zfar / (zfar - znear);
    transformationmatrix[11] = 1.F;
    transformationmatrix[12] = 0.F;
    transformationmatrix[13] = 0.F;
    transformationmatrix[14] = -znear * zfar / (zfar - znear);
    transformationmatrix[15] = 0.F;
    cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans, (int)transformedtriangles.size * 3, 4, 4, 1.F, (float *)transformedtriangles.data, 4, transformationmatrix, 4, 0.F, (float *)newdata, 4);
    temp = transformedtriangles.data;
    transformedtriangles.data = newdata;
    newdata = temp;

    light * newlightingtable = malloc(transformedtriangles.size * sizeof(light));
    if (newlightingtable == NULL) {
        free(lightingtable);
        free(newdata);
        releasetriangles(&transformedtriangles);
        errornumber = RENDERER_ERROR_INSUFFICIENTMEMORY;
        return;
    }

    /* Perspective divide and clipping */
    size_t newsize = 0;
    for (size_t triangleindex = 0; triangleindex < transformedtriangles.size; triangleindex += 1) {
        if (transformedtriangles.data[triangleindex].w1 > 0.F && transformedtriangles.data[triangleindex].w2 > 0.F && transformedtriangles.data[triangleindex].w3 > 0.F) {
            if (transformedtriangles.data[triangleindex].v1.x >= -transformedtriangles.data[triangleindex].w1 && transformedtriangles.data[triangleindex].v1.x <= transformedtriangles.data[triangleindex].w1 && transformedtriangles.data[triangleindex].v1.y >= -transformedtriangles.data[triangleindex].w1 && transformedtriangles.data[triangleindex].v1.y <= transformedtriangles.data[triangleindex].w1 && transformedtriangles.data[triangleindex].v1.z >= 0.F && transformedtriangles.data[triangleindex].v1.z <= transformedtriangles.data[triangleindex].w1 &&
                transformedtriangles.data[triangleindex].v2.x >= -transformedtriangles.data[triangleindex].w2 && transformedtriangles.data[triangleindex].v2.x <= transformedtriangles.data[triangleindex].w2 && transformedtriangles.data[triangleindex].v2.y >= -transformedtriangles.data[triangleindex].w2 && transformedtriangles.data[triangleindex].v2.y <= transformedtriangles.data[triangleindex].w2 && transformedtriangles.data[triangleindex].v2.z >= 0.F && transformedtriangles.data[triangleindex].v2.z <= transformedtriangles.data[triangleindex].w2 &&
                transformedtriangles.data[triangleindex].v3.x >= -transformedtriangles.data[triangleindex].w3 && transformedtriangles.data[triangleindex].v3.x <= transformedtriangles.data[triangleindex].w3 && transformedtriangles.data[triangleindex].v3.y >= -transformedtriangles.data[triangleindex].w3 && transformedtriangles.data[triangleindex].v3.y <= transformedtriangles.data[triangleindex].w3 && transformedtriangles.data[triangleindex].v3.z >= 0.F && transformedtriangles.data[triangleindex].v3.z <= transformedtriangles.data[triangleindex].w3) {
                if (newsize + 1 > transformedtriangles.size) {
                    reallocpointer = realloc(newdata, (newsize + 1) * sizeof(triangle));
                    if (reallocpointer == NULL) {
                        free(newlightingtable);
                        free(lightingtable);
                        free(newdata);
                        releasetriangles(&transformedtriangles);
                        errornumber = RENDERER_ERROR_INSUFFICIENTMEMORY;
                        return;
                    }
                    newdata = reallocpointer;
                    reallocpointer = realloc(newlightingtable, (newsize + 1) * sizeof(light));
                    if (reallocpointer == NULL) {
                        free(newlightingtable);
                        free(lightingtable);
                        free(newdata);
                        releasetriangles(&transformedtriangles);
                        errornumber = RENDERER_ERROR_INSUFFICIENTMEMORY;
                        return;
                    }
                    newlightingtable = reallocpointer;
                }
                newdata[newsize].v1.x = transformedtriangles.data[triangleindex].v1.x / transformedtriangles.data[triangleindex].w1;
                newdata[newsize].v1.y = transformedtriangles.data[triangleindex].v1.y / transformedtriangles.data[triangleindex].w1;
                newdata[newsize].v1.z = transformedtriangles.data[triangleindex].v1.z / transformedtriangles.data[triangleindex].w1;
                newdata[newsize].w1 = 1.F;
                newdata[newsize].v2.x = transformedtriangles.data[triangleindex].v2.x / transformedtriangles.data[triangleindex].w2;
                newdata[newsize].v2.y = transformedtriangles.data[triangleindex].v2.y / transformedtriangles.data[triangleindex].w2;
                newdata[newsize].v2.z = transformedtriangles.data[triangleindex].v2.z / transformedtriangles.data[triangleindex].w2;
                newdata[newsize].w2 = 1.F;
                newdata[newsize].v3.x = transformedtriangles.data[triangleindex].v3.x / transformedtriangles.data[triangleindex].w3;
                newdata[newsize].v3.y = transformedtriangles.data[triangleindex].v3.y / transformedtriangles.data[triangleindex].w3;
                newdata[newsize].v3.z = transformedtriangles.data[triangleindex].v3.z / transformedtriangles.data[triangleindex].w3;
                newdata[newsize].w3 = 1.F;
                newlightingtable[newsize].red = lightingtable[triangleindex].red;
                newlightingtable[newsize].green = lightingtable[triangleindex].green;
                newlightingtable[newsize].blue = lightingtable[triangleindex].blue;
                newsize += 1;
            } else {
                polygon p1 = {
                    3,
                    {
                        {transformedtriangles.data[triangleindex].v1.x / transformedtriangles.data[triangleindex].w1, transformedtriangles.data[triangleindex].v1.y / transformedtriangles.data[triangleindex].w1, transformedtriangles.data[triangleindex].v1.z / transformedtriangles.data[triangleindex].w1},
                        {transformedtriangles.data[triangleindex].v2.x / transformedtriangles.data[triangleindex].w2, transformedtriangles.data[triangleindex].v2.y / transformedtriangles.data[triangleindex].w2, transformedtriangles.data[triangleindex].v2.z / transformedtriangles.data[triangleindex].w2},
                        {transformedtriangles.data[triangleindex].v3.x / transformedtriangles.data[triangleindex].w3, transformedtriangles.data[triangleindex].v3.y / transformedtriangles.data[triangleindex].w3, transformedtriangles.data[triangleindex].v3.z / transformedtriangles.data[triangleindex].w3}
                    }
                };
                polygon p2;
                bool inside;
                bool nextinside;
                point previous;

                /* Check p1 against x >= -1 and save clipped polygon in p2 */
                p2.size = 0;
                inside = p1.vertices[0].x >= -1.F;
                if (inside) {
                    p2.vertices[p2.size] = p1.vertices[0];
                    p2.size += 1;
                }
                previous = p1.vertices[0];
                for (size_t vertexindex = 1; vertexindex < p1.size; vertexindex += 1) {
                    nextinside = p1.vertices[vertexindex].x >= -1.F;
                    if ((inside && !nextinside) || (!inside && nextinside)) {
                        /* Append intersection point to p2 */
                        float ratio = (-1.F - previous.x) / (p1.vertices[vertexindex].x - previous.x);
                        p2.vertices[p2.size].x = -1.F;
                        p2.vertices[p2.size].y = previous.y + (p1.vertices[vertexindex].y - previous.y) * ratio;
                        p2.vertices[p2.size].z = previous.z + (p1.vertices[vertexindex].z - previous.z) * ratio;
                        p2.size += 1;
                    }
                    if (nextinside) {
                        p2.vertices[p2.size] = p1.vertices[vertexindex];
                        p2.size += 1;
                    }
                    inside = nextinside;
                    previous = p1.vertices[vertexindex];
                }
                nextinside = p1.vertices[0].x >= -1.F;
                if ((inside && !nextinside) || (!inside && nextinside)) {
                    /* Append intersection point to p2 */
                    float ratio = (-1.F - previous.x) / (p1.vertices[0].x - previous.x);
                    p2.vertices[p2.size].x = -1.F;
                    p2.vertices[p2.size].y = previous.y + (p1.vertices[0].y - previous.y) * ratio;
                    p2.vertices[p2.size].z = previous.z + (p1.vertices[0].z - previous.z) * ratio;
                    p2.size += 1;
                }
                if (p2.size < 3) {
                    continue;
                }

                /* Check p2 against x <= 1 and save clipped polygon in p1 */
                p1.size = 0;
                inside = p2.vertices[0].x <= 1.F;
                if (inside) {
                    p1.vertices[p1.size] = p2.vertices[0];
                    p1.size += 1;
                }
                previous = p2.vertices[0];
                for (size_t vertexindex = 1; vertexindex < p2.size; vertexindex += 1) {
                    nextinside = p2.vertices[vertexindex].x <= 1.F;
                    if ((inside && !nextinside) || (!inside && nextinside)) {
                        /* Append intersection point to p1 */
                        float ratio = (1.F - previous.x) / (p2.vertices[vertexindex].x - previous.x);
                        p1.vertices[p1.size].x = 1.F;
                        p1.vertices[p1.size].y = previous.y + (p2.vertices[vertexindex].y - previous.y) * ratio;
                        p1.vertices[p1.size].z = previous.z + (p2.vertices[vertexindex].z - previous.z) * ratio;
                        p1.size += 1;
                    }
                    if (nextinside) {
                        p1.vertices[p1.size] = p2.vertices[vertexindex];
                        p1.size += 1;
                    }
                    inside = nextinside;
                    previous = p2.vertices[vertexindex];
                }
                nextinside = p2.vertices[0].x <= 1.F;
                if ((inside && !nextinside) || (!inside && nextinside)) {
                    /* Append intersection point to p1 */
                    float ratio = (1.F - previous.x) / (p2.vertices[0].x - previous.x);
                    p1.vertices[p1.size].x = 1.F;
                    p1.vertices[p1.size].y = previous.y + (p2.vertices[0].y - previous.y) * ratio;
                    p1.vertices[p1.size].z = previous.z + (p2.vertices[0].z - previous.z) * ratio;
                    p1.size += 1;
                }
                if (p1.size < 3) {
                    continue;
                }

                /* Check p1 against y >= -1 and save clipped polygon in p2 */
                p2.size = 0;
                inside = p1.vertices[0].y >= -1.F;
                if (inside) {
                    p2.vertices[p2.size] = p1.vertices[0];
                    p2.size += 1;
                }
                previous = p1.vertices[0];
                for (size_t vertexindex = 1; vertexindex < p1.size; vertexindex += 1) {
                    nextinside = p1.vertices[vertexindex].y >= -1.F;
                    if ((inside && !nextinside) || (!inside && nextinside)) {
                        /* Append intersection point to p2 */
                        float ratio = (-1.F - previous.y) / (p1.vertices[vertexindex].y - previous.y);
                        p2.vertices[p2.size].x = previous.x + (p1.vertices[vertexindex].x - previous.x) * ratio;
                        p2.vertices[p2.size].y = -1.F;
                        p2.vertices[p2.size].z = previous.z + (p1.vertices[vertexindex].z - previous.z) * ratio;
                        p2.size += 1;
                    }
                    if (nextinside) {
                        p2.vertices[p2.size] = p1.vertices[vertexindex];
                        p2.size += 1;
                    }
                    inside = nextinside;
                    previous = p1.vertices[vertexindex];
                }
                nextinside = p1.vertices[0].y >= -1.F;
                if ((inside && !nextinside) || (!inside && nextinside)) {
                    /* Append intersection point to p2 */
                    float ratio = (-1.F - previous.y) / (p1.vertices[0].y - previous.y);
                    p2.vertices[p2.size].x = previous.x + (p1.vertices[0].x - previous.x) * ratio;
                    p2.vertices[p2.size].y = -1.F;
                    p2.vertices[p2.size].z = previous.z + (p1.vertices[0].z - previous.z) * ratio;
                    p2.size += 1;
                }
                if (p2.size < 3) {
                    continue;
                }

                /* Check p2 against y <= 1 and save clipped polygon in p1 */
                p1.size = 0;
                inside = p2.vertices[0].y <= 1.F;
                if (inside) {
                    p1.vertices[p1.size] = p2.vertices[0];
                    p1.size += 1;
                }
                previous = p2.vertices[0];
                for (size_t vertexindex = 1; vertexindex < p2.size; vertexindex += 1) {
                    nextinside = p2.vertices[vertexindex].y <= 1.F;
                    if ((inside && !nextinside) || (!inside && nextinside)) {
                        /* Append intersection point to p1 */
                        float ratio = (1.F - previous.y) / (p2.vertices[vertexindex].y - previous.y);
                        p1.vertices[p1.size].x = previous.x + (p2.vertices[vertexindex].x - previous.x) * ratio;
                        p1.vertices[p1.size].y = 1.F;
                        p1.vertices[p1.size].z = previous.z + (p2.vertices[vertexindex].z - previous.z) * ratio;
                        p1.size += 1;
                    }
                    if (nextinside) {
                        p1.vertices[p1.size] = p2.vertices[vertexindex];
                        p1.size += 1;
                    }
                    inside = nextinside;
                    previous = p2.vertices[vertexindex];
                }
                nextinside = p2.vertices[0].y <= 1.F;
                if ((inside && !nextinside) || (!inside && nextinside)) {
                    /* Append intersection point to p1 */
                    float ratio = (1.F - previous.y) / (p2.vertices[0].y - previous.y);
                    p1.vertices[p1.size].x = previous.x + (p2.vertices[0].x - previous.x) * ratio;
                    p1.vertices[p1.size].y = 1.F;
                    p1.vertices[p1.size].z = previous.z + (p2.vertices[0].z - previous.z) * ratio;
                    p1.size += 1;
                }
                if (p1.size < 3) {
                    continue;
                }

                /* Check p1 against z >= 0 and save clipped polygon in p2 */
                p2.size = 0;
                inside = p1.vertices[0].z >= 0.F;
                if (inside) {
                    p2.vertices[p2.size] = p1.vertices[0];
                    p2.size += 1;
                }
                previous = p1.vertices[0];
                for (size_t vertexindex = 1; vertexindex < p1.size; vertexindex += 1) {
                    nextinside = p1.vertices[vertexindex].z >= 0.F;
                    if ((inside && !nextinside) || (!inside && nextinside)) {
                        /* Append intersection point to p2 */
                        float ratio = -previous.z / (p1.vertices[vertexindex].z - previous.z);
                        p2.vertices[p2.size].x = previous.x + (p1.vertices[vertexindex].x - previous.x) * ratio;
                        p2.vertices[p2.size].y = previous.y + (p1.vertices[vertexindex].y - previous.y) * ratio;
                        p2.vertices[p2.size].z = 0.F;
                        p2.size += 1;
                    }
                    if (nextinside) {
                        p2.vertices[p2.size] = p1.vertices[vertexindex];
                        p2.size += 1;
                    }
                    inside = nextinside;
                    previous = p1.vertices[vertexindex];
                }
                nextinside = p1.vertices[0].z >= 0.F;
                if ((inside && !nextinside) || (!inside && nextinside)) {
                    /* Append intersection point to p2 */
                    float ratio = -previous.z / (p1.vertices[0].z - previous.z);
                    p2.vertices[p2.size].x = previous.x + (p1.vertices[0].x - previous.x) * ratio;
                    p2.vertices[p2.size].y = previous.y + (p1.vertices[0].y - previous.y) * ratio;
                    p2.vertices[p2.size].z = 0.F;
                    p2.size += 1;
                }
                if (p2.size < 3) {
                    continue;
                }

                /* Check p2 against z <= 1 and save clipped polygon in p1 */
                p1.size = 0;
                inside = p2.vertices[0].z <= 1.F;
                if (inside) {
                    p1.vertices[p1.size] = p2.vertices[0];
                    p1.size += 1;
                }
                previous = p2.vertices[0];
                for (size_t vertexindex = 1; vertexindex < p2.size; vertexindex += 1) {
                    nextinside = p2.vertices[vertexindex].z <= 1.F;
                    if ((inside && !nextinside) || (!inside && nextinside)) {
                        /* Append intersection point to p1 */
                        float ratio = (1.F - previous.z) / (p2.vertices[vertexindex].z - previous.z);
                        p1.vertices[p1.size].x = previous.x + (p2.vertices[vertexindex].x - previous.x) * ratio;
                        p1.vertices[p1.size].y = previous.y + (p2.vertices[vertexindex].y - previous.y) * ratio;
                        p1.vertices[p1.size].z = 1.F;
                        p1.size += 1;
                    }
                    if (nextinside) {
                        p1.vertices[p1.size] = p2.vertices[vertexindex];
                        p1.size += 1;
                    }
                    inside = nextinside;
                    previous = p2.vertices[vertexindex];
                }
                nextinside = p2.vertices[0].z <= 1.F;
                if ((inside && !nextinside) || (!inside && nextinside)) {
                    /* Append intersection point to p1 */
                    float ratio = (1.F - previous.z) / (p2.vertices[0].z - previous.z);
                    p1.vertices[p1.size].x = previous.x + (p2.vertices[0].x - previous.x) * ratio;
                    p1.vertices[p1.size].y = previous.y + (p2.vertices[0].y - previous.y) * ratio;
                    p1.vertices[p1.size].z = 1.F;
                    p1.size += 1;
                }
                if (p1.size < 3) {
                    continue;
                }

                /* Add new triangles */
                if (newsize + p1.size - 2 > transformedtriangles.size) {
                    reallocpointer = realloc(newdata, (newsize + p1.size - 2) * sizeof(triangle));
                    if (reallocpointer == NULL) {
                        free(newlightingtable);
                        free(lightingtable);
                        free(newdata);
                        releasetriangles(&transformedtriangles);
                        errornumber = RENDERER_ERROR_INSUFFICIENTMEMORY;
                        return;
                    }
                    newdata = reallocpointer;
                    reallocpointer = realloc(newlightingtable, (newsize + p1.size - 2) * sizeof(light));
                    if (reallocpointer == NULL) {
                        free(newlightingtable);
                        free(lightingtable);
                        free(newdata);
                        releasetriangles(&transformedtriangles);
                        errornumber = RENDERER_ERROR_INSUFFICIENTMEMORY;
                        return;
                    }
                    newlightingtable = reallocpointer;
                }
                for (size_t newtriangleindex = 0; newtriangleindex < p1.size - 2; newtriangleindex += 1) {
                    newdata[newsize].v1 = p1.vertices[0];
                    newdata[newsize].w1 = 1.F;
                    newdata[newsize].v2 = p1.vertices[newtriangleindex + 1];
                    newdata[newsize].w2 = 1.F;
                    newdata[newsize].v3 = p1.vertices[newtriangleindex + 2];
                    newdata[newsize].w3 = 1.F;
                    newlightingtable[newsize].red = lightingtable[triangleindex].red;
                    newlightingtable[newsize].green = lightingtable[triangleindex].green;
                    newlightingtable[newsize].blue = lightingtable[triangleindex].blue;
                    newsize += 1;
                }
            }
        }
    }
    if (newsize == 0) {
        free(newlightingtable);
        free(lightingtable);
        free(newdata);
        releasetriangles(&transformedtriangles);
        return;
    }
    if (newsize < transformedtriangles.size) {
        reallocpointer = realloc(newdata, newsize * sizeof(triangle));
        if (reallocpointer == NULL) {
            free(newlightingtable);
            free(lightingtable);
            free(newdata);
            releasetriangles(&transformedtriangles);
            errornumber = RENDERER_ERROR_INSUFFICIENTMEMORY;
            return;
        }
        newdata = reallocpointer;
    }
    if (newsize != transformedtriangles.size) {
        reallocpointer = realloc(transformedtriangles.data, newsize * sizeof(triangle));
        if (reallocpointer == NULL) {
            free(newlightingtable);
            free(lightingtable);
            free(newdata);
            releasetriangles(&transformedtriangles);
            errornumber = RENDERER_ERROR_INSUFFICIENTMEMORY;
            return;
        }
        transformedtriangles.data = reallocpointer;
        transformedtriangles.size = newsize;
    }
    temp = transformedtriangles.data;
    transformedtriangles.data = newdata;
    newdata = temp;
    free(lightingtable);
    lightingtable = realloc(newlightingtable, newsize * sizeof(light));
    if (lightingtable == NULL) {
        free(newlightingtable);
        free(newdata);
        releasetriangles(&transformedtriangles);
        errornumber = RENDERER_ERROR_INSUFFICIENTMEMORY;
        return;
    }

    /* Viewport transformation */
    transformationmatrix[0] = (float)target->width;
    transformationmatrix[1] = 0.F;
    transformationmatrix[2] = 0.F;
    transformationmatrix[3] = 0.F;
    transformationmatrix[4] = 0.F;
    transformationmatrix[5] = -(float)target->height;
    transformationmatrix[6] = 0.F;
    transformationmatrix[7] = 0.F;
    transformationmatrix[8] = 0.F;
    transformationmatrix[9] = 0.F;
    transformationmatrix[10] = 1.F;
    transformationmatrix[11] = 0.F;
    transformationmatrix[12] = (float)target->width;
    transformationmatrix[13] = (float)target->height;
    transformationmatrix[14] = 0.F;
    transformationmatrix[15] = 1.F;
    cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans, (int)transformedtriangles.size * 3, 4, 4, 1.F, (float *)transformedtriangles.data, 4, transformationmatrix, 4, 0.F, (float *)newdata, 4);
    free(transformedtriangles.data);
    transformedtriangles.data = newdata;

    /* Rasterization */
    uint32_t * doublesizedsurface = calloc((size_t)target->width * 2 * (size_t)target->height * 2, sizeof(uint32_t));
    if (doublesizedsurface == NULL) {
        free(lightingtable);
        releasetriangles(&transformedtriangles);
        errornumber = RENDERER_ERROR_INSUFFICIENTMEMORY;
        return;
    }
    float * zbuffer = NULL;
    if (usezbuffer) {
        zbuffer = malloc((size_t)target->width * 2 * (size_t)target->height * 2 * sizeof(float));
        if (zbuffer == NULL) {
            free(doublesizedsurface);
            free(lightingtable);
            releasetriangles(&transformedtriangles);
            errornumber = RENDERER_ERROR_INSUFFICIENTMEMORY;
            return;
        }
        for (size_t index = 0; index < (size_t)target->width * 2 * (size_t)target->height * 2; index += 1) {
            zbuffer[index] = FLT_MAX;
        }
    } else {
        srand((unsigned int)time(NULL));
        zsortingsubroutine(transformedtriangles.data, lightingtable, 0, transformedtriangles.size - 1);
    }
    for (size_t triangleindex = 0; triangleindex < transformedtriangles.size; triangleindex += 1) {
        int minx = (int)roundf(fminf(transformedtriangles.data[triangleindex].v1.x, fminf(transformedtriangles.data[triangleindex].v2.x, transformedtriangles.data[triangleindex].v3.x)));
        int maxx = (int)roundf(fmaxf(transformedtriangles.data[triangleindex].v1.x, fmaxf(transformedtriangles.data[triangleindex].v2.x, transformedtriangles.data[triangleindex].v3.x)));
        int miny = (int)roundf(fminf(transformedtriangles.data[triangleindex].v1.y, fminf(transformedtriangles.data[triangleindex].v2.y, transformedtriangles.data[triangleindex].v3.y)));
        int maxy = (int)roundf(fmaxf(transformedtriangles.data[triangleindex].v1.y, fmaxf(transformedtriangles.data[triangleindex].v2.y, transformedtriangles.data[triangleindex].v3.y)));
        int x1 = (int)(roundf(transformedtriangles.data[triangleindex].v2.x) - roundf(transformedtriangles.data[triangleindex].v1.x));
        int y1 = (int)(roundf(transformedtriangles.data[triangleindex].v2.y) - roundf(transformedtriangles.data[triangleindex].v1.y));
        int x2 = (int)(roundf(transformedtriangles.data[triangleindex].v3.x) - roundf(transformedtriangles.data[triangleindex].v1.x));
        int y2 = (int)(roundf(transformedtriangles.data[triangleindex].v3.y) - roundf(transformedtriangles.data[triangleindex].v1.y));

        if (usezbuffer) {
            vector v1 = {
                transformedtriangles.data[triangleindex].v2.x - transformedtriangles.data[triangleindex].v1.x,
                transformedtriangles.data[triangleindex].v2.y - transformedtriangles.data[triangleindex].v1.y,
                transformedtriangles.data[triangleindex].v2.z - transformedtriangles.data[triangleindex].v1.z
            };
            vector v2 = {
                transformedtriangles.data[triangleindex].v3.x - transformedtriangles.data[triangleindex].v1.x,
                transformedtriangles.data[triangleindex].v3.y - transformedtriangles.data[triangleindex].v1.y,
                transformedtriangles.data[triangleindex].v3.z - transformedtriangles.data[triangleindex].v1.z
            };
            vector normal;
            crossproduct(&normal, &v1, &v2);
            float d = dotproduct(&normal, (const vector *)&transformedtriangles.data[triangleindex].v1);

            for (int x = minx; x <= maxx; x += 1) {
                for (int y = miny; y <= maxy; y += 1) {
                    int x3 = x - (int)roundf(transformedtriangles.data[triangleindex].v1.x);
                    int y3 = y - (int)roundf(transformedtriangles.data[triangleindex].v1.y);
                    int r = x1 * y2 - y1 * x2;
                    int s = x3 * y2 - y3 * x2;
                    int t = x1 * y3 - y1 * x3;
                    if ((r > 0 && s >= 0 && t >= 0 && s + t <= r) || (r < 0 && s <= 0 && t <= 0 && s + t >= r)) {
                        if (x != (int)target->width * 2 && y != (int)target->height * 2) {
                            assert(x >= 0 && x < (int)target->width * 2 && y >= 0 && y < (int)target->height * 2);
                            float z = -(normal.x * x + normal.y * y - d) / normal.z;
                            if (z < zbuffer[y * target->width * 2 + x]) {
                                doublesizedsurface[y * target->width * 2 + x] = 0xFF000000 | (uint32_t)roundf(lightingtable[triangleindex].blue * 255.F) << 16 | (uint32_t)roundf(lightingtable[triangleindex].green * 255.F) << 8 | (uint32_t)roundf(lightingtable[triangleindex].red * 255.F);
                                zbuffer[y * target->width * 2 + x] = z;
                            }
                        }
                    }
                }
            }
        } else {
            for (int x = minx; x <= maxx; x += 1) {
                for (int y = miny; y <= maxy; y += 1) {
                    int x3 = x - (int)roundf(transformedtriangles.data[triangleindex].v1.x);
                    int y3 = y - (int)roundf(transformedtriangles.data[triangleindex].v1.y);
                    int r = x1 * y2 - y1 * x2;
                    int s = x3 * y2 - y3 * x2;
                    int t = x1 * y3 - y1 * x3;
                    if ((r > 0 && s >= 0 && t >= 0 && s + t <= r) || (r < 0 && s <= 0 && t <= 0 && s + t >= r)) {
                        if (x != (int)target->width * 2 && y != (int)target->height * 2) {
                            assert(x >= 0 && x < (int)target->width * 2 && y >= 0 && y < (int)target->height * 2);
                            doublesizedsurface[y * target->width * 2 + x] = 0xFF000000 | (uint32_t)roundf(lightingtable[triangleindex].blue * 255.F) << 16 | (uint32_t)roundf(lightingtable[triangleindex].green * 255.F) << 8 | (uint32_t)roundf(lightingtable[triangleindex].red * 255.F);
                        }
                    }
                }
            }
        }
    }
    for (size_t y = 0; y < (size_t)target->height; y += 1) {
        for (size_t x = 0; x < (size_t)target->width; x += 1) {
            uint8_t alpha = ((doublesizedsurface[y * 2 * (size_t)target->width * 2 + x * 2] >> 24) + (doublesizedsurface[y * 2 * (size_t)target->width * 2 + x * 2 + 1] >> 24) + (doublesizedsurface[(y * 2 + 1) * (size_t)target->width * 2 + x * 2] >> 24) + (doublesizedsurface[(y * 2 + 1) * (size_t)target->width * 2 + x * 2 + 1] >> 24)) / 4;
            uint16_t tempred = 0U;
            uint16_t tempgreen = 0U;
            uint16_t tempblue = 0U;
            size_t opaquepixels = 0;
            if (doublesizedsurface[y * 2 * (size_t)target->width * 2 + x * 2] >> 24 == 0xFFU) {
                tempred += doublesizedsurface[y * 2 * (size_t)target->width * 2 + x * 2] & 0xFFU;
                tempgreen += doublesizedsurface[y * 2 * (size_t)target->width * 2 + x * 2] >> 8 & 0xFFU;
                tempblue += doublesizedsurface[y * 2 * (size_t)target->width * 2 + x * 2] >> 16 & 0xFFU;
                opaquepixels += 1;
            }
            if (doublesizedsurface[y * 2 * (size_t)target->width * 2 + x * 2 + 1] >> 24 == 0xFFU) {
                tempred += doublesizedsurface[y * 2 * (size_t)target->width * 2 + x * 2 + 1] & 0xFFU;
                tempgreen += doublesizedsurface[y * 2 * (size_t)target->width * 2 + x * 2 + 1] >> 8 & 0xFFU;
                tempblue += doublesizedsurface[y * 2 * (size_t)target->width * 2 + x * 2 + 1] >> 16 & 0xFFU;
                opaquepixels += 1;
            }
            if (doublesizedsurface[(y * 2 + 1) * (size_t)target->width * 2 + x * 2] >> 24 == 0xFFU) {
                tempred += doublesizedsurface[(y * 2 + 1) * (size_t)target->width * 2 + x * 2] & 0xFFU;
                tempgreen += doublesizedsurface[(y * 2 + 1) * (size_t)target->width * 2 + x * 2] >> 8 & 0xFFU;
                tempblue += doublesizedsurface[(y * 2 + 1) * (size_t)target->width * 2 + x * 2] >> 16 & 0xFFU;
                opaquepixels += 1;
            }
            if (doublesizedsurface[(y * 2 + 1) * (size_t)target->width * 2 + x * 2 + 1] >> 24 == 0xFFU) {
                tempred += doublesizedsurface[(y * 2 + 1) * (size_t)target->width * 2 + x * 2 + 1] & 0xFFU;
                tempgreen += doublesizedsurface[(y * 2 + 1) * (size_t)target->width * 2 + x * 2 + 1] >> 8 & 0xFFU;
                tempblue += doublesizedsurface[(y * 2 + 1) * (size_t)target->width * 2 + x * 2 + 1] >> 16 & 0xFFU;
                opaquepixels += 1;
            }
            if (opaquepixels != 0) {
                uint8_t red = (uint8_t)(tempred / opaquepixels);
                uint8_t green = (uint8_t)(tempgreen / opaquepixels);
                uint8_t blue = (uint8_t)(tempblue / opaquepixels);
                target->pixels[y * target->width + x] = (uint32_t)alpha << 24 | (uint32_t)blue << 16 | (uint32_t)green << 8 | (uint32_t)red;
            }
        }
    }
    if (usezbuffer) {
        free(zbuffer);
    }
    free(doublesizedsurface);

    free(lightingtable);
    releasetriangles(&transformedtriangles);
    errornumber = RENDERER_ERROR_NONE;
}

void savesurfacetopngfile(const surface * s, const char * filename)
{
    png_FILE_p filepointer = fopen(filename, "wb");
    if (filepointer == NULL) {
        errornumber = RENDERER_ERROR_FILEOPENFAILED;
        return;
    }

    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (png == NULL) {
        fclose(filepointer);
        errornumber = RENDERER_ERROR_INSUFFICIENTMEMORY;
        return;
    }

    png_infop info = png_create_info_struct(png);
    if (info == NULL) {
        png_destroy_write_struct(&png, &info);
        fclose(filepointer);
        errornumber = RENDERER_ERROR_INSUFFICIENTMEMORY;
        return;
    }

    png_set_IHDR(png, info, s->width, s->height, 8, PNG_COLOR_TYPE_RGB_ALPHA, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_bytepp rows = png_malloc(png, (png_alloc_size_t)s->height * sizeof(png_bytep));
    for (uint16_t y = 0U; y < s->height; y += 1U) {
        png_uint_32p row = png_malloc(png, (png_alloc_size_t)s->width * sizeof(png_uint_32));
        rows[y] = (png_bytep)row;
        for (uint16_t x = 0U; x < s->width; x += 1U) {
            *row = (png_uint_32)s->pixels[y * s->width + x];
            row += 1;
        }
    }

    png_init_io(png, filepointer);
    png_set_rows(png, info, rows);
    png_write_png(png, info, PNG_TRANSFORM_IDENTITY, NULL);
    for (uint16_t y = 0U; y < s->height; y += 1U) {
        png_free(png, rows[y]);
    }
    png_free(png, rows);
    png_destroy_write_struct(&png, &info);

    if (fclose(filepointer) == EOF) {
        errornumber = RENDERER_ERROR_FILECLOSEFAILED;
        return;
    }

    errornumber = RENDERER_ERROR_NONE;
}

static int inicallback(IniDispatch * dispatch, void * user_data)
{
    if (dispatch->type == INI_SECTION) {
        const char * source = dispatch->data;
        for (size_t i = 0; i < 64; i += 1) {
            inisection[i] = *source;
            if (*source == '\0') {
                break;
            } else if (i == 63) {
                inisection[i] = '\0';
            } else {
                source += 1;
            }
        }
    } else if (dispatch->type == INI_KEY) {
        if (strcmp(inisection, "Renderer") == 0) {
            if (strcmp(dispatch->data, "LightSourcePositionX") == 0) {
                if (sscanf(dispatch->value, "%f", &lightsourceposition.x) != 1) {
                    errornumber = RENDERER_ERROR_CONFIGWRONGFORMAT;
                }
            } else if (strcmp(dispatch->data, "LightSourcePositionY") == 0) {
                if (sscanf(dispatch->value, "%f", &lightsourceposition.y) != 1) {
                    errornumber = RENDERER_ERROR_CONFIGWRONGFORMAT;
                }
            } else if (strcmp(dispatch->data, "LightSourcePositionZ") == 0) {
                if (sscanf(dispatch->value, "%f", &lightsourceposition.z) != 1) {
                    errornumber = RENDERER_ERROR_CONFIGWRONGFORMAT;
                }
            } else if (strcmp(dispatch->data, "CameraPositionX") == 0) {
                if (sscanf(dispatch->value, "%f", &cameraposition.x) != 1) {
                    errornumber = RENDERER_ERROR_CONFIGWRONGFORMAT;
                }
            } else if (strcmp(dispatch->data, "CameraPositionY") == 0) {
                if (sscanf(dispatch->value, "%f", &cameraposition.y) != 1) {
                    errornumber = RENDERER_ERROR_CONFIGWRONGFORMAT;
                }
            } else if (strcmp(dispatch->data, "CameraPositionZ") == 0) {
                if (sscanf(dispatch->value, "%f", &cameraposition.z) != 1) {
                    errornumber = RENDERER_ERROR_CONFIGWRONGFORMAT;
                }
            } else if (strcmp(dispatch->data, "CameraLookAtPointX") == 0) {
                if (sscanf(dispatch->value, "%f", &cameralookatpoint.x) != 1) {
                    errornumber = RENDERER_ERROR_CONFIGWRONGFORMAT;
                }
            } else if (strcmp(dispatch->data, "CameraLookAtPointY") == 0) {
                if (sscanf(dispatch->value, "%f", &cameralookatpoint.y) != 1) {
                    errornumber = RENDERER_ERROR_CONFIGWRONGFORMAT;
                }
            } else if (strcmp(dispatch->data, "CameraLookAtPointZ") == 0) {
                if (sscanf(dispatch->value, "%f", &cameralookatpoint.z) != 1) {
                    errornumber = RENDERER_ERROR_CONFIGWRONGFORMAT;
                }
            } else if (strcmp(dispatch->data, "UpVectorX") == 0) {
                if (sscanf(dispatch->value, "%f", &up.x) != 1) {
                    errornumber = RENDERER_ERROR_CONFIGWRONGFORMAT;
                }
            } else if (strcmp(dispatch->data, "UpVectorY") == 0) {
                if (sscanf(dispatch->value, "%f", &up.y) != 1) {
                    errornumber = RENDERER_ERROR_CONFIGWRONGFORMAT;
                }
            } else if (strcmp(dispatch->data, "UpVectorZ") == 0) {
                if (sscanf(dispatch->value, "%f", &up.z) != 1) {
                    errornumber = RENDERER_ERROR_CONFIGWRONGFORMAT;
                }
            } else if (strcmp(dispatch->data, "ObjectPositionX") == 0) {
                if (sscanf(dispatch->value, "%f", &objectposition.x) != 1) {
                    errornumber = RENDERER_ERROR_CONFIGWRONGFORMAT;
                }
            } else if (strcmp(dispatch->data, "ObjectPositionY") == 0) {
                if (sscanf(dispatch->value, "%f", &objectposition.y) != 1) {
                    errornumber = RENDERER_ERROR_CONFIGWRONGFORMAT;
                }
            } else if (strcmp(dispatch->data, "ObjectPositionZ") == 0) {
                if (sscanf(dispatch->value, "%f", &objectposition.z) != 1) {
                    errornumber = RENDERER_ERROR_CONFIGWRONGFORMAT;
                }
            } else if (strcmp(dispatch->data, "ObjectRotationX") == 0) {
                if (sscanf(dispatch->value, "%f", &objectrotationxdegree) != 1) {
                    errornumber = RENDERER_ERROR_CONFIGWRONGFORMAT;
                } else {
                    objectrotationx = degreetoradian(objectrotationxdegree);
                }
            } else if (strcmp(dispatch->data, "ObjectRotationY") == 0) {
                if (sscanf(dispatch->value, "%f", &objectrotationydegree) != 1) {
                    errornumber = RENDERER_ERROR_CONFIGWRONGFORMAT;
                } else {
                    objectrotationy = degreetoradian(objectrotationydegree);
                }
            } else if (strcmp(dispatch->data, "ObjectRotationZ") == 0) {
                if (sscanf(dispatch->value, "%f", &objectrotationzdegree) != 1) {
                    errornumber = RENDERER_ERROR_CONFIGWRONGFORMAT;
                } else {
                    objectrotationz = degreetoradian(objectrotationzdegree);
                }
            } else if (strcmp(dispatch->data, "ObjectScalingX") == 0) {
                if (sscanf(dispatch->value, "%f", &objectscalingx) != 1) {
                    errornumber = RENDERER_ERROR_CONFIGWRONGFORMAT;
                }
            } else if (strcmp(dispatch->data, "ObjectScalingY") == 0) {
                if (sscanf(dispatch->value, "%f", &objectscalingy) != 1) {
                    errornumber = RENDERER_ERROR_CONFIGWRONGFORMAT;
                }
            } else if (strcmp(dispatch->data, "ObjectScalingZ") == 0) {
                if (sscanf(dispatch->value, "%f", &objectscalingz) != 1) {
                    errornumber = RENDERER_ERROR_CONFIGWRONGFORMAT;
                }
            } else if (strcmp(dispatch->data, "FieldOfView") == 0) {
                if (sscanf(dispatch->value, "%f", &fieldofviewdegree) != 1) {
                    errornumber = RENDERER_ERROR_CONFIGWRONGFORMAT;
                } else if (fieldofviewdegree < 0.F || fieldofviewdegree > 180.F) {
                    errornumber = RENDERER_ERROR_INVALIDVALUE;
                } else {
                    fieldofview = degreetoradian(fieldofviewdegree);
                }
            } else if (strcmp(dispatch->data, "zNear") == 0) {
                if (sscanf(dispatch->value, "%f", &znear) != 1) {
                    errornumber = RENDERER_ERROR_CONFIGWRONGFORMAT;
                } else if (znear < FLT_EPSILON) {
                    errornumber = RENDERER_ERROR_INVALIDVALUE;
                }
            } else if (strcmp(dispatch->data, "zFar") == 0) {
                if (sscanf(dispatch->value, "%f", &zfar) != 1) {
                    errornumber = RENDERER_ERROR_CONFIGWRONGFORMAT;
                } else if (zfar < FLT_EPSILON) {
                    errornumber = RENDERER_ERROR_INVALIDVALUE;
                }
            } else if (strcmp(dispatch->data, "OutputWidth") == 0) {
                if (sscanf(dispatch->value, "%u", &outputwidth) != 1) {
                    errornumber = RENDERER_ERROR_CONFIGWRONGFORMAT;
                } else if (outputwidth == 0U || outputwidth > 32767U) {
                    errornumber = RENDERER_ERROR_INVALIDVALUE;
                }
            } else if (strcmp(dispatch->data, "OutputHeight") == 0) {
                if (sscanf(dispatch->value, "%u", &outputheight) != 1) {
                    errornumber = RENDERER_ERROR_CONFIGWRONGFORMAT;
                } else if (outputheight == 0U || outputheight > 32767U) {
                    errornumber = RENDERER_ERROR_INVALIDVALUE;
                }
            } else if (strcmp(dispatch->data, "MaterialDiffuseReflectance") == 0) {
                if (strlen(dispatch->value) != 7 || dispatch->value[0] != '#' || !ishexadecimalcharacter(dispatch->value[1]) || !ishexadecimalcharacter(dispatch->value[2]) || !ishexadecimalcharacter(dispatch->value[3]) || !ishexadecimalcharacter(dispatch->value[4]) || !ishexadecimalcharacter(dispatch->value[5]) || !ishexadecimalcharacter(dispatch->value[6])) {
                    errornumber = RENDERER_ERROR_CONFIGWRONGFORMAT;
                } else {
                    materialdiffusereflectancered = hexadecimalcharactertovalue(dispatch->value[1]) * 16 + hexadecimalcharactertovalue(dispatch->value[2]);
                    materialdiffusereflectancegreen = hexadecimalcharactertovalue(dispatch->value[3]) * 16 + hexadecimalcharactertovalue(dispatch->value[4]);
                    materialdiffusereflectanceblue = hexadecimalcharactertovalue(dispatch->value[5]) * 16 + hexadecimalcharactertovalue(dispatch->value[6]);
                    materialdiffusereflectance.red = materialdiffusereflectancered / 255.0F;
                    materialdiffusereflectance.green = materialdiffusereflectancegreen / 255.0F;
                    materialdiffusereflectance.blue = materialdiffusereflectanceblue / 255.0F;
                }
            } else if (strcmp(dispatch->data, "BackfaceCulling") == 0) {
                if (strcmp(dispatch->value, "1") == 0) {
                    backfaceculling = true;
                } else if (strcmp(dispatch->value, "0") == 0) {
                    backfaceculling = false;
                } else {
                    errornumber = RENDERER_ERROR_CONFIGWRONGFORMAT;
                }
            } else if (strcmp(dispatch->data, "UseZBuffer") == 0) {
                if (strcmp(dispatch->value, "1") == 0) {
                    usezbuffer = true;
                } else if (strcmp(dispatch->value, "0") == 0) {
                    usezbuffer = false;
                } else {
                    errornumber = RENDERER_ERROR_CONFIGWRONGFORMAT;
                }
            }
        }
    }
    return 0;
}

static float degreetoradian(float degree)
{
    return degree * 3.14159265F / 180.F;
}

static bool ishexadecimalcharacter(char character)
{
    return character == '0' || character == '1' || character == '2' || character == '3' || character == '4' || character == '5' || character == '6' || character == '7' || character == '8' || character == '9'
        || character == 'A' || character == 'B' || character == 'C' || character == 'D' || character == 'E' || character == 'F'
        || character == 'a' || character == 'b' || character == 'c' || character == 'd' || character == 'e' || character == 'f';
}

static int hexadecimalcharactertovalue(char character)
{
    switch (character) {
    case '0':
        errornumber = RENDERER_ERROR_NONE;
        return 0;
    case '1':
        errornumber = RENDERER_ERROR_NONE;
        return 1;
    case '2':
        errornumber = RENDERER_ERROR_NONE;
        return 2;
    case '3':
        errornumber = RENDERER_ERROR_NONE;
        return 3;
    case '4':
        errornumber = RENDERER_ERROR_NONE;
        return 4;
    case '5':
        errornumber = RENDERER_ERROR_NONE;
        return 5;
    case '6':
        errornumber = RENDERER_ERROR_NONE;
        return 6;
    case '7':
        errornumber = RENDERER_ERROR_NONE;
        return 7;
    case '8':
        errornumber = RENDERER_ERROR_NONE;
        return 8;
    case '9':
        errornumber = RENDERER_ERROR_NONE;
        return 9;
    case 'A':
    case 'a':
        errornumber = RENDERER_ERROR_NONE;
        return 10;
    case 'B':
    case 'b':
        errornumber = RENDERER_ERROR_NONE;
        return 11;
    case 'C':
    case 'c':
        errornumber = RENDERER_ERROR_NONE;
        return 12;
    case 'D':
    case 'd':
        errornumber = RENDERER_ERROR_NONE;
        return 13;
    case 'E':
    case 'e':
        errornumber = RENDERER_ERROR_NONE;
        return 14;
    case 'F':
    case 'f':
        errornumber = RENDERER_ERROR_NONE;
        return 15;
    default:
        errornumber = RENDERER_ERROR_INVALIDVALUE;
        return -1;
    }
}

static float dotproduct(const vector * v1, const vector * v2)
{
    return v1->x * v2->x + v1->y * v2->y + v1->z * v2->z;
}

static void crossproduct(vector * product, const vector * v1, const vector * v2)
{
    product->x = v1->y * v2->z - v1->z * v2->y;
    product->y = v1->z * v2->x - v1->x * v2->z;
    product->z = v1->x * v2->y - v1->y * v2->x;
}

static void normalize(vector * v)
{
    float magnitude = sqrtf(v->x * v->x + v->y * v->y + v->z * v->z);
    if (magnitude != 0.F) {
        v->x /= magnitude;
        v->y /= magnitude;
        v->z /= magnitude;
    } else {
        v->x = 0.F;
        v->y = 1.F;
        v->z = 0.F;
    }
}

static void calculatenewtransformationmatrix(float * t, const float * op)
{
    memcpy(previousmatrix, t, 16 * sizeof(float));
    cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans, 4, 4, 4, 1.F, previousmatrix, 4, op, 4, 0.F, t, 4);
}

static void zsortingsubroutine(triangle * triangletable, light * lightingtable, intptr_t start, intptr_t end)
{
    if (start < end) {
        triangle temptriangle;
        light templight;
        int randomnumber = rand();
        intptr_t length = end + 1 - start;
        if (length <= RAND_MAX) {
            randomnumber %= length;
        }
        intptr_t pivot = start + randomnumber;
        temptriangle = triangletable[pivot];
        triangletable[pivot] = triangletable[end];
        triangletable[end] = temptriangle;
        templight = lightingtable[pivot];
        lightingtable[pivot] = lightingtable[end];
        lightingtable[end] = templight;
        intptr_t left = start;
        intptr_t right = end - 1;
        while (left <= right) {
            float zpivot = (triangletable[end].v1.z + triangletable[end].v2.z + triangletable[end].v3.z) / 3.F;
            if ((triangletable[left].v1.z + triangletable[left].v2.z + triangletable[left].v3.z) / 3.F > zpivot) {
                left += 1;
            } else if ((triangletable[right].v1.z + triangletable[right].v2.z + triangletable[right].v3.z) / 3.F <= zpivot) {
                right -= 1;
            } else {
                temptriangle = triangletable[left];
                triangletable[left] = triangletable[right];
                triangletable[right] = temptriangle;
                templight = lightingtable[left];
                lightingtable[left] = lightingtable[right];
                lightingtable[right] = templight;
                left += 1;
                right -= 1;
            }
        }
        temptriangle = triangletable[left];
        triangletable[left] = triangletable[end];
        triangletable[end] = temptriangle;
        templight = lightingtable[left];
        lightingtable[left] = lightingtable[end];
        lightingtable[end] = templight;
        zsortingsubroutine(triangletable, lightingtable, start, left - 1);
        zsortingsubroutine(triangletable, lightingtable, left + 1, end);
    }
}
