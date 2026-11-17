/*******************************************************************************************
*
*   raylib [models] example - animation layers
*
*   Example complexity rating: [★★★★] 4/4
*
*   Example demonstrates:
*       - Cross-fading between walking and running animations
*       - Adding a masked additive layer to the upper body only
*       - Normalizing replace-layer weights per bone
*
*   WARNING: GPU skinning is used when enabled at raylib compile time, otherwise CPU skinning is
*   used automatically. Both paths consume the same blended bone matrices.
*
*   Example licensed under an unmodified zlib/libpng license, which is an OSI-certified,
*   BSD-like license that allows static linking with closed source software
*
*   Copyright (c) 2026 raylib contributors
*
********************************************************************************************/

#include "raylib.h"
#include "raymath.h"

#include <math.h>
#include <string.h>

#if defined(PLATFORM_DESKTOP)
    #define GLSL_VERSION            330
#else
    #define GLSL_VERSION            100
#endif

static int FindAnimationIndex(const ModelAnimation *animations, int animationCount, const char *name);
static bool IsUpperBodyBone(const char *boneName);

int main(void)
{
    const int screenWidth = 900;
    const int screenHeight = 500;

    InitWindow(screenWidth, screenHeight, "raylib [models] example - animation layers");

    Camera camera = { 0 };
    camera.position = (Vector3){ 5.0f, 4.0f, 5.0f };
    camera.target = (Vector3){ 0.0f, 1.8f, 0.0f };
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    Model model = LoadModel("resources/models/gltf/robot.glb");

#if SUPPORT_GPU_SKINNING
    Shader skinningShader = { 0 };
    skinningShader = LoadShader(TextFormat("resources/shaders/glsl%i/skinning.vs", GLSL_VERSION),
        TextFormat("resources/shaders/glsl%i/skinning.fs", GLSL_VERSION));

    for (int material = 0; material < model.materialCount; material++) model.materials[material].shader = skinningShader;
#endif

    int animationCount = 0;
    ModelAnimation *animations = LoadModelAnimations("resources/models/gltf/robot.glb", &animationCount);

    if ((animations == NULL) || (animationCount == 0))
    {
        TraceLog(LOG_ERROR, "ANIMATION: Failed to load animations from resources/models/gltf/robot.glb");
        UnloadModel(model);
#if SUPPORT_GPU_SKINNING
        UnloadShader(skinningShader);
#endif
        CloseWindow();
        return 1;
    }

    int walkIndex = FindAnimationIndex(animations, animationCount, "Robot_Walking");
    int runIndex = FindAnimationIndex(animations, animationCount, "Robot_Running");
    int additiveIndex = FindAnimationIndex(animations, animationCount, "Robot_Wave");

    if (walkIndex < 0) walkIndex = 0;
    if (runIndex < 0) runIndex = (animationCount > 1)? 1 : walkIndex;
    if (additiveIndex < 0) additiveIndex = (animationCount > 2)? 2 : walkIndex;

    unsigned char *upperBodyMask = (unsigned char *)MemAlloc(model.skeleton.boneCount*sizeof(unsigned char));
    memset(upperBodyMask, 0, model.skeleton.boneCount*sizeof(unsigned char));
    for (unsigned int bone = 0; bone < model.skeleton.boneCount; bone++)
    {
        upperBodyMask[bone] = IsUpperBodyBone(model.skeleton.bones[bone].name)? 1 : 0;
    }

    float walkFrame = 0.0f;
    float runFrame = 0.0f;
    float additiveFrame = 0.0f;
    bool additiveEnabled = true;
    double blendTimeMs = 0.0;

    SetTargetFPS(60);

    while (!WindowShouldClose())
    {
        UpdateCamera(&camera, CAMERA_ORBITAL);

        if (IsKeyPressed(KEY_SPACE)) additiveEnabled = !additiveEnabled;

        float frameSpeed = GetFrameTime()*45.0f;
        walkFrame = fmodf(walkFrame + frameSpeed, (float)animations[walkIndex].keyframeCount);
        runFrame = fmodf(runFrame + frameSpeed, (float)animations[runIndex].keyframeCount);
        additiveFrame = fmodf(additiveFrame + frameSpeed, (float)animations[additiveIndex].keyframeCount);

        // Smoothly alternate between walking and running while continuously advancing both clips
        float crossFade = 0.5f - 0.5f*cosf((float)GetTime()*0.8f);

        ModelAnimationLayer layers[3] = {
            { &animations[walkIndex], walkFrame, 1.0f - crossFade, NULL, MODEL_ANIMATION_BLEND_REPLACE },
            { &animations[runIndex], runFrame, crossFade, NULL, MODEL_ANIMATION_BLEND_REPLACE },
            {
                &animations[additiveIndex], additiveFrame,
                additiveEnabled? 0.45f : 0.0f, upperBodyMask, MODEL_ANIMATION_BLEND_ADDITIVE
            }
        };

        double blendStart = GetTime();
        UpdateModelAnimationBlend(model, layers, 3, true);
        blendTimeMs = (GetTime() - blendStart)*1000.0;

        BeginDrawing();

            ClearBackground(RAYWHITE);

            BeginMode3D(camera);

                DrawModel(model, Vector3Zero(), 1.0f, WHITE);
                DrawGrid(10, 1.0f);

            EndMode3D();

            DrawText("Animation layers: Walk <-> Run + masked Wave additive", 10, 10, 20, DARKGRAY);
            DrawText(TextFormat("Cross-fade: %.2f", crossFade), 10, 38, 20, MAROON);
            DrawText(TextFormat("Upper-body additive: %s (SPACE)", additiveEnabled? "ON" : "OFF"), 10, 66, 20, BLUE);
            DrawText(TextFormat("Blend update: %.3f ms", blendTimeMs), 10, 94, 20, DARKGREEN);

        EndDrawing();
    }

    MemFree(upperBodyMask);
    UnloadModelAnimations(animations, animationCount);
    UnloadModel(model);
#if SUPPORT_GPU_SKINNING
    UnloadShader(skinningShader);
#endif

    CloseWindow();

    return 0;
}

static int FindAnimationIndex(const ModelAnimation *animations, int animationCount, const char *name)
{
    if (animations == NULL) return -1;

    for (int animation = 0; animation < animationCount; animation++)
    {
        if (TextIsEqual(animations[animation].name, name)) return animation;
    }

    return -1;
}

static bool IsUpperBodyBone(const char *boneName)
{
    const char *upperBodyParts[] = {
        "Head", "Neck", "Torso", "Abdomen", "Shoulder", "UpperArm", "LowerArm",
        "Palm", "Index", "Middle", "Ring", "Thumb"
    };

    for (int part = 0; part < (int)(sizeof(upperBodyParts)/sizeof(upperBodyParts[0])); part++)
    {
        if (strstr(boneName, upperBodyParts[part]) != NULL) return true;
    }

    return false;
}
