/*******************************************************************************************
*
*   raylib render test - skeletal animation blending
*
*   Renders a cross-faded and masked additive animation to a texture and verifies
*   that visible pixels were produced. This test requires a graphics context and
*   returns 77 when no window can be created, allowing CTest to skip it.
*
********************************************************************************************/

#include "raylib.h"
#include "raymath.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(320, 240, "raylib animation render check");

    if (!IsWindowReady()) return 77;

    const char *modelPath = TextFormat("%s/models/gltf/robot.glb", ANIMATION_TEST_RESOURCE_DIR);
    Model model = LoadModel(modelPath);
    int animationCount = 0;
    ModelAnimation *animations = LoadModelAnimations(modelPath, &animationCount);

    if ((model.meshCount == 0) || (animations == NULL) || (animationCount < 2))
    {
        TraceLog(LOG_WARNING, "ANIMATION: Render check could not load model animations from %s", modelPath);
        UnloadModelAnimations(animations, animationCount);
        UnloadModel(model);
        CloseWindow();
        return 77;
    }

    Camera camera = { 0 };
    camera.position = (Vector3){ 5.0f, 4.0f, 5.0f };
    camera.target = (Vector3){ 0.0f, 1.8f, 0.0f };
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    unsigned char *mask = (unsigned char *)calloc(model.skeleton.boneCount, sizeof(unsigned char));
    for (unsigned int bone = 0; bone < model.skeleton.boneCount; bone++)
    {
        mask[bone] = (unsigned char)((strstr(model.skeleton.bones[bone].name, "Head") != NULL) ||
            (strstr(model.skeleton.bones[bone].name, "Arm") != NULL) ||
            (strstr(model.skeleton.bones[bone].name, "Torso") != NULL));
    }

    ModelAnimationLayer layers[3] = {
        { &animations[0], 20.0f, 0.5f, NULL, MODEL_ANIMATION_BLEND_REPLACE },
        { &animations[1], 30.0f, 0.5f, NULL, MODEL_ANIMATION_BLEND_REPLACE },
        { &animations[2%animationCount], 40.0f, 0.5f, mask, MODEL_ANIMATION_BLEND_ADDITIVE }
    };
    UpdateModelAnimationBlend(model, layers, 3, true);

    RenderTexture2D target = LoadRenderTexture(800, 450);
    const char *outputPath = getenv("RAYLIB_ANIMATION_RENDER_OUTPUT");
    Color background = (outputPath != NULL)? RAYWHITE : (Color){ 13, 17, 23, 255 };

    BeginTextureMode(target);
        ClearBackground(background);
        BeginMode3D(camera);
            DrawModel(model, Vector3Zero(), 1.0f, WHITE);
            if (outputPath != NULL) DrawGrid(10, 1.0f);
        EndMode3D();
    EndTextureMode();

    Image renderedImage = LoadImageFromTexture(target.texture);
    int visiblePixels = 0;
    for (int y = 0; y < renderedImage.height; y++)
    {
        for (int x = 0; x < renderedImage.width; x++)
        {
            Color pixel = GetImageColor(renderedImage, x, y);
            if ((pixel.r != background.r) || (pixel.g != background.g) || (pixel.b != background.b)) visiblePixels++;
        }
    }

    printf("Animation render check: %i visible pixels\n", visiblePixels);

    if (outputPath != NULL)
    {
        ImageFlipVertical(&renderedImage);
        ExportImage(renderedImage, outputPath);
    }

    UnloadImage(renderedImage);
    UnloadRenderTexture(target);
    free(mask);
    UnloadModelAnimations(animations, animationCount);
    UnloadModel(model);
    CloseWindow();

    return (visiblePixels > 100)? 0 : 1;
}
