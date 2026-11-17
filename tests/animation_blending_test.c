/*******************************************************************************************
*
*   raylib unit tests - skeletal animation blending
*
*   Tests interpolation, weight normalization, bone masks, additive layers,
*   zero weights, GPU/software skinning consistency and blending performance
*
********************************************************************************************/

#include "raylib.h"
#include "raymath.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define TEST_MAX_BONES 100

typedef struct TestModel {
    Model model;
    BoneInfo bones[TEST_MAX_BONES];
    Transform bindPose[TEST_MAX_BONES];
    Transform currentPose[TEST_MAX_BONES];
    Matrix boneMatrices[TEST_MAX_BONES];
} TestModel;

typedef struct TestAnimation {
    ModelAnimation animation;
    Transform poses[2][TEST_MAX_BONES];
    ModelAnimPose posePointers[2];
} TestAnimation;

static int testFailures = 0;

#define CHECK(condition, message) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL: %s (%s:%i)\n", message, __FILE__, __LINE__); \
            testFailures++; \
        } \
    } while (0)

#define CHECK_NEAR(value, expected, tolerance, message) \
    do { \
        float checkValue = (float)(value); \
        float checkExpected = (float)(expected); \
        if (fabsf(checkValue - checkExpected) > (float)(tolerance)) { \
            fprintf(stderr, "FAIL: %s (actual=%f, expected=%f, %s:%i)\n", \
                message, checkValue, checkExpected, __FILE__, __LINE__); \
            testFailures++; \
        } \
    } while (0)

static void InitTestModel(TestModel *testModel, unsigned int boneCount)
{
    memset(testModel, 0, sizeof(TestModel));

    testModel->model.skeleton.boneCount = boneCount;
    testModel->model.skeleton.bones = testModel->bones;
    testModel->model.skeleton.bindPose = testModel->bindPose;
    testModel->model.currentPose = testModel->currentPose;
    testModel->model.boneMatrices = testModel->boneMatrices;

    for (unsigned int i = 0; i < boneCount; i++)
    {
        testModel->bones[i].parent = -1;
        snprintf(testModel->bones[i].name, sizeof(testModel->bones[i].name), "Bone%u", i);
        testModel->bindPose[i].translation = Vector3Zero();
        testModel->bindPose[i].rotation = QuaternionIdentity();
        testModel->bindPose[i].scale = Vector3One();
        testModel->currentPose[i] = testModel->bindPose[i];
        testModel->boneMatrices[i] = MatrixIdentity();
    }
}

static void InitTestAnimation(TestAnimation *testAnimation, unsigned int boneCount)
{
    memset(testAnimation, 0, sizeof(TestAnimation));

    testAnimation->animation.boneCount = boneCount;
    testAnimation->animation.keyframeCount = 2;
    testAnimation->posePointers[0] = testAnimation->poses[0];
    testAnimation->posePointers[1] = testAnimation->poses[1];
    testAnimation->animation.keyframePoses = testAnimation->posePointers;
    snprintf(testAnimation->animation.name, sizeof(testAnimation->animation.name), "TestAnimation");

    for (int frame = 0; frame < 2; frame++)
    {
        for (unsigned int bone = 0; bone < boneCount; bone++)
        {
            testAnimation->poses[frame][bone].translation = Vector3Zero();
            testAnimation->poses[frame][bone].rotation = QuaternionIdentity();
            testAnimation->poses[frame][bone].scale = Vector3One();
        }
    }
}

static void SetAnimationPose(TestAnimation *testAnimation, int frame, unsigned int bone, Transform transform)
{
    testAnimation->poses[frame][bone] = transform;
}

static float QuaternionDot(Quaternion q1, Quaternion q2)
{
    return q1.x*q2.x + q1.y*q2.y + q1.z*q2.z + q1.w*q2.w;
}

static void TestInterpolation(void)
{
    TestModel testModel = { 0 };
    TestAnimation animation = { 0 };
    InitTestModel(&testModel, 1);
    InitTestAnimation(&animation, 1);

    Transform frame0 = { { 0.0f, 0.0f, 0.0f }, QuaternionIdentity(), Vector3One() };
    Transform frame1 = { { 10.0f, 0.0f, 0.0f }, QuaternionIdentity(), Vector3One() };
    SetAnimationPose(&animation, 0, 0, frame0);
    SetAnimationPose(&animation, 1, 0, frame1);

    ModelAnimationLayer layer = { &animation.animation, 0.25f, 1.0f, NULL, MODEL_ANIMATION_BLEND_REPLACE };
    UpdateModelAnimationBlend(testModel.model, &layer, 1, true);

    CHECK_NEAR(testModel.currentPose[0].translation.x, 2.5f, 0.0001f, "fractional frame interpolation");
    CHECK_NEAR(testModel.boneMatrices[0].m12, 2.5f, 0.0001f, "interpolated bone matrix");

    // Frames outside the sequence wrap instead of indexing outside the keyframe array
    layer.frame = 2.5f;
    UpdateModelAnimationBlend(testModel.model, &layer, 1, true);
    CHECK_NEAR(testModel.currentPose[0].translation.x, 5.0f, 0.0001f, "positive wrapped frame");

    layer.frame = -1.5f;
    UpdateModelAnimationBlend(testModel.model, &layer, 1, true);
    CHECK_NEAR(testModel.currentPose[0].translation.x, 5.0f, 0.0001f, "negative wrapped frame");
}

static void TestWeightNormalization(void)
{
    TestModel testModel = { 0 };
    TestAnimation animationA = { 0 };
    TestAnimation animationB = { 0 };
    InitTestModel(&testModel, 1);
    InitTestAnimation(&animationA, 1);
    InitTestAnimation(&animationB, 1);

    Transform poseA = { { 0.0f, 0.0f, 0.0f }, QuaternionIdentity(), Vector3One() };
    Transform poseB = { { 10.0f, 0.0f, 0.0f }, QuaternionIdentity(), Vector3One() };

    for (int frame = 0; frame < 2; frame++)
    {
        SetAnimationPose(&animationA, frame, 0, poseA);
        SetAnimationPose(&animationB, frame, 0, poseB);
    }

    ModelAnimationLayer layers[2] = {
        { &animationA.animation, 0.0f, 1.0f, NULL, MODEL_ANIMATION_BLEND_REPLACE },
        { &animationB.animation, 0.0f, 3.0f, NULL, MODEL_ANIMATION_BLEND_REPLACE }
    };

    UpdateModelAnimationBlend(testModel.model, layers, 2, true);
    CHECK_NEAR(testModel.currentPose[0].translation.x, 7.5f, 0.0001f, "normalized layer weights");

    // Unnormalized layers leave the missing influence on the bind pose
    layers[0].weight = 0.25f;
    layers[1].weight = 0.25f;
    UpdateModelAnimationBlend(testModel.model, layers, 2, false);
    CHECK_NEAR(testModel.currentPose[0].translation.x, 2.5f, 0.0001f, "partial unnormalized weights");

    layers[0].weight = 0.0f;
    layers[1].weight = 0.0f;
    UpdateModelAnimationBlend(testModel.model, layers, 2, true);
    CHECK_NEAR(testModel.currentPose[0].translation.x, 0.0f, 0.0001f, "zero weights use bind pose");
    CHECK((testModel.boneMatrices[0].m12 == 0.0f) && (testModel.boneMatrices[0].m13 == 0.0f) &&
        (testModel.boneMatrices[0].m14 == 0.0f), "zero weights use identity bone matrix");
}

static void TestBoneMaskAndAdditive(void)
{
    TestModel testModel = { 0 };
    TestAnimation base = { 0 };
    TestAnimation additive = { 0 };
    InitTestModel(&testModel, 2);
    InitTestAnimation(&base, 2);
    InitTestAnimation(&additive, 2);

    Transform baseBone0 = { { 1.0f, 0.0f, 0.0f }, QuaternionIdentity(), Vector3One() };
    Transform baseBone1 = { { 2.0f, 0.0f, 0.0f }, QuaternionIdentity(), Vector3One() };
    Transform additiveBone0 = { { 5.0f, 0.0f, 0.0f }, QuaternionIdentity(), Vector3One() };
    Transform additiveBone1 = { { 7.0f, 0.0f, 0.0f }, QuaternionIdentity(), Vector3One() };

    for (int frame = 0; frame < 2; frame++)
    {
        SetAnimationPose(&base, frame, 0, baseBone0);
        SetAnimationPose(&base, frame, 1, baseBone1);
        SetAnimationPose(&additive, frame, 0, additiveBone0);
        SetAnimationPose(&additive, frame, 1, additiveBone1);
    }

    unsigned char mask[2] = { 1, 0 };
    ModelAnimationLayer layers[2] = {
        { &base.animation, 0.0f, 1.0f, NULL, MODEL_ANIMATION_BLEND_REPLACE },
        { &additive.animation, 0.0f, 0.5f, mask, MODEL_ANIMATION_BLEND_ADDITIVE }
    };

    UpdateModelAnimationBlend(testModel.model, layers, 2, true);
    CHECK_NEAR(testModel.currentPose[0].translation.x, 3.5f, 0.0001f, "masked additive bone");
    CHECK_NEAR(testModel.currentPose[1].translation.x, 2.0f, 0.0001f, "additive mask excludes other bones");
    CHECK_NEAR(testModel.boneMatrices[0].m12, 3.5f, 0.0001f, "masked additive bone matrix");
    CHECK_NEAR(testModel.boneMatrices[1].m12, 2.0f, 0.0001f, "unmasked bone matrix unchanged");

    layers[1].weight = 0.0f;
    UpdateModelAnimationBlend(testModel.model, layers, 2, true);
    CHECK_NEAR(testModel.currentPose[0].translation.x, 1.0f, 0.0001f, "zero-weight additive is a no-op");

    // Masks affect replace-layer normalization independently per bone
    unsigned char replaceMaskA[2] = { 1, 0 };
    unsigned char replaceMaskB[2] = { 0, 1 };
    ModelAnimationLayer layersMasked[2] = {
        { &base.animation, 0.0f, 1.0f, replaceMaskA, MODEL_ANIMATION_BLEND_REPLACE },
        { &base.animation, 0.0f, 1.0f, replaceMaskB, MODEL_ANIMATION_BLEND_REPLACE }
    };

    UpdateModelAnimationBlend(testModel.model, layersMasked, 2, true);
    CHECK_NEAR(testModel.currentPose[0].translation.x, 1.0f, 0.0001f, "masked replace layer bone 0");
    CHECK_NEAR(testModel.currentPose[1].translation.x, 2.0f, 0.0001f, "masked replace layer bone 1");
}

static void TestCrossFadeContinuity(void)
{
    TestModel testModel = { 0 };
    TestAnimation animationA = { 0 };
    TestAnimation animationB = { 0 };
    InitTestModel(&testModel, 1);
    InitTestAnimation(&animationA, 1);
    InitTestAnimation(&animationB, 1);

    Transform poseA = { { 1.0f, 0.0f, 0.0f }, QuaternionIdentity(), Vector3One() };
    Transform poseB = {
        { 9.0f, 0.0f, 0.0f },
        QuaternionFromAxisAngle((Vector3){ 0.0f, 1.0f, 0.0f }, 90.0f*DEG2RAD),
        Vector3One()
    };

    for (int frame = 0; frame < 2; frame++)
    {
        SetAnimationPose(&animationA, frame, 0, poseA);
        SetAnimationPose(&animationB, frame, 0, poseB);
    }

    float previousX = 0.0f;
    Quaternion previousRotation = QuaternionIdentity();
    for (int step = 0; step <= 100; step++)
    {
        float blend = (float)step/100.0f;
        ModelAnimationLayer layers[2] = {
            { &animationA.animation, 0.0f, 1.0f - blend, NULL, MODEL_ANIMATION_BLEND_REPLACE },
            { &animationB.animation, 0.0f, blend, NULL, MODEL_ANIMATION_BLEND_REPLACE }
        };

        UpdateModelAnimationBlend(testModel.model, layers, 2, true);
        float currentX = testModel.currentPose[0].translation.x;
        Quaternion currentRotation = testModel.currentPose[0].rotation;

        if (step == 0)
        {
            CHECK_NEAR(currentX, poseA.translation.x, 0.0001f, "crossfade start pose");
            CHECK_NEAR(currentRotation.w, poseA.rotation.w, 0.0001f, "crossfade start rotation");
        }
        else
        {
            CHECK_NEAR(currentX - previousX, 0.08f, 0.002f, "crossfade translation continuity");
            CHECK_NEAR(fabsf(QuaternionDot(previousRotation, currentRotation)), 1.0f, 0.002f,
                "crossfade rotation continuity");
        }

        if (step == 100)
        {
            CHECK_NEAR(currentX, poseB.translation.x, 0.0001f, "crossfade end pose");
            CHECK_NEAR(fabsf(currentRotation.w), fabsf(poseB.rotation.w), 0.0001f, "crossfade end rotation");
        }

        previousX = currentX;
        previousRotation = currentRotation;
    }
}

static void TestLegacyBlendCompatibility(void)
{
    TestModel genericModel = { 0 };
    TestModel legacyModel = { 0 };
    TestAnimation animationA = { 0 };
    TestAnimation animationB = { 0 };
    InitTestModel(&genericModel, 1);
    InitTestModel(&legacyModel, 1);
    InitTestAnimation(&animationA, 1);
    InitTestAnimation(&animationB, 1);

    Transform poseA = { { 1.0f, 2.0f, 3.0f }, QuaternionFromAxisAngle((Vector3){ 0.0f, 0.0f, 1.0f }, 15.0f*DEG2RAD),
        (Vector3){ 1.0f, 1.0f, 1.0f } };
    Transform poseB = { { 8.0f, 6.0f, 4.0f }, QuaternionFromAxisAngle((Vector3){ 1.0f, 0.0f, 0.0f }, 70.0f*DEG2RAD),
        (Vector3){ 1.2f, 0.8f, 1.1f } };

    for (int frame = 0; frame < 2; frame++)
    {
        SetAnimationPose(&animationA, frame, 0, poseA);
        SetAnimationPose(&animationB, frame, 0, poseB);
    }

    float blend = 0.37f;
    ModelAnimationLayer layers[2] = {
        { &animationA.animation, 0.25f, 1.0f - blend, NULL, MODEL_ANIMATION_BLEND_REPLACE },
        { &animationB.animation, 0.75f, blend, NULL, MODEL_ANIMATION_BLEND_REPLACE }
    };
    UpdateModelAnimationBlend(genericModel.model, layers, 2, true);
    UpdateModelAnimationEx(legacyModel.model, animationA.animation, 0.25f, animationB.animation, 0.75f, blend);

    CHECK_NEAR(genericModel.currentPose[0].translation.x, legacyModel.currentPose[0].translation.x,
        0.0001f, "legacy cross-fade translation compatibility");
    CHECK_NEAR(genericModel.boneMatrices[0].m14, legacyModel.boneMatrices[0].m14,
        0.0001f, "legacy cross-fade matrix compatibility");
    CHECK_NEAR(fabsf(QuaternionDot(genericModel.currentPose[0].rotation, legacyModel.currentPose[0].rotation)),
        1.0f, 0.0001f, "legacy cross-fade rotation compatibility");
}

static void TestGpuSoftwareConsistency(void)
{
    TestModel testModel = { 0 };
    TestAnimation animation = { 0 };
    InitTestModel(&testModel, 2);
    InitTestAnimation(&animation, 2);

    Transform bone0Pose = { { 4.0f, 0.0f, 0.0f }, QuaternionIdentity(), Vector3One() };
    Transform bone1Pose = { { 0.0f, 2.0f, 0.0f }, QuaternionIdentity(), Vector3One() };
    for (int frame = 0; frame < 2; frame++)
    {
        SetAnimationPose(&animation, frame, 0, bone0Pose);
        SetAnimationPose(&animation, frame, 1, bone1Pose);
    }

    float vertices[] = { 1.0f, 0.0f, 0.0f };
    unsigned char boneIndices[] = { 0, 1, 0, 0 };
    float boneWeights[] = { 0.25f, 0.75f, 0.0f, 0.0f };
    Mesh mesh = {
        .vertexCount = 1,
        .vertices = vertices,
        .boneIndices = boneIndices,
        .boneWeights = boneWeights
    };

    testModel.model.meshCount = 1;
    testModel.model.meshes = &mesh;

    ModelAnimationLayer layer = { &animation.animation, 0.0f, 1.0f, NULL, MODEL_ANIMATION_BLEND_REPLACE };
    UpdateModelAnimationBlend(testModel.model, &layer, 1, true);

    Vector3 softwareVertex = GetModelAnimationSkinnedVertex(testModel.model, 0, 0);

    // GPU shader equivalent: sum(weight * boneMatrix[index] * position)
    Vector3 gpuVertex = Vector3Zero();
    for (int slot = 0; slot < 4; slot++)
    {
        if (boneWeights[slot] == 0.0f) continue;
        Vector3 transformed = Vector3Transform((Vector3){ vertices[0], vertices[1], vertices[2] },
            testModel.boneMatrices[boneIndices[slot]]);
        gpuVertex = Vector3Add(gpuVertex, Vector3Scale(transformed, boneWeights[slot]));
    }

    CHECK_NEAR(softwareVertex.x, 2.0f, 0.0001f, "software skinned vertex X");
    CHECK_NEAR(softwareVertex.y, 1.5f, 0.0001f, "software skinned vertex Y");
    CHECK_NEAR(softwareVertex.x, gpuVertex.x, 0.0001f, "GPU/software skinning X consistency");
    CHECK_NEAR(softwareVertex.y, gpuVertex.y, 0.0001f, "GPU/software skinning Y consistency");
    CHECK_NEAR(softwareVertex.z, gpuVertex.z, 0.0001f, "GPU/software skinning Z consistency");

    CHECK(mesh.animVertices != NULL, "CPU skinning allocates offline vertex buffer");
    if (mesh.animVertices != NULL)
    {
        CHECK_NEAR(mesh.animVertices[0], softwareVertex.x, 0.0001f, "CPU skinned buffer X consistency");
        CHECK_NEAR(mesh.animVertices[1], softwareVertex.y, 0.0001f, "CPU skinned buffer Y consistency");
        CHECK_NEAR(mesh.animVertices[2], softwareVertex.z, 0.0001f, "CPU skinned buffer Z consistency");
        MemFree(mesh.animVertices);
    }
}

static void TestInvalidInput(void)
{
    TestModel testModel = { 0 };
    TestAnimation animation = { 0 };
    InitTestModel(&testModel, 1);
    InitTestAnimation(&animation, 1);

    Transform pose = { { 3.0f, 0.0f, 0.0f }, QuaternionIdentity(), Vector3One() };
    SetAnimationPose(&animation, 0, 0, pose);
    SetAnimationPose(&animation, 1, 0, pose);

    ModelAnimationLayer layer = { &animation.animation, 0.0f, 1.0f, NULL, MODEL_ANIMATION_BLEND_REPLACE };
    UpdateModelAnimationBlend(testModel.model, &layer, 1, true);
    CHECK_NEAR(testModel.currentPose[0].translation.x, 3.0f, 0.0001f, "valid pose before invalid input");

    layer.frame = NAN;
    UpdateModelAnimationBlend(testModel.model, &layer, 1, true);
    CHECK_NEAR(testModel.currentPose[0].translation.x, 3.0f, 0.0001f, "NaN frame leaves pose unchanged");

    layer.frame = 0.0f;
    layer.weight = -1.0f;
    UpdateModelAnimationBlend(testModel.model, &layer, 1, true);
    CHECK_NEAR(testModel.currentPose[0].translation.x, 3.0f, 0.0001f, "negative weight leaves pose unchanged");

    UpdateModelAnimationBlend(testModel.model, NULL, 1, true);
    CHECK_NEAR(testModel.currentPose[0].translation.x, 3.0f, 0.0001f, "NULL layer list leaves pose unchanged");

    UpdateModelAnimationEx(testModel.model, animation.animation, 0.0f, animation.animation, 0.0f, 2.0f);
    CHECK_NEAR(testModel.currentPose[0].translation.x, 3.0f, 0.0001f, "out-of-range blend leaves pose unchanged");

    testModel.bindPose[0].scale = Vector3Zero();
    layer.frame = 0.0f;
    layer.weight = 1.0f;
    layer.mode = MODEL_ANIMATION_BLEND_ADDITIVE;
    UpdateModelAnimationBlend(testModel.model, &layer, 1, true);
    CHECK(isfinite(testModel.currentPose[0].translation.x), "zero bind scale does not produce non-finite pose");
}

static void TestPerformance(void)
{
    TestModel testModel = { 0 };
    TestAnimation animationA = { 0 };
    TestAnimation animationB = { 0 };
    InitTestModel(&testModel, TEST_MAX_BONES);
    InitTestAnimation(&animationA, TEST_MAX_BONES);
    InitTestAnimation(&animationB, TEST_MAX_BONES);

    unsigned char mask[TEST_MAX_BONES];
    for (int bone = 0; bone < TEST_MAX_BONES; bone++)
    {
        Transform poseA = {
            { (float)bone*0.01f, 0.0f, 0.0f },
            QuaternionFromAxisAngle((Vector3){ 0.0f, 1.0f, 0.0f }, (float)bone*DEG2RAD),
            Vector3One()
        };
        Transform poseB = {
            { (float)bone*0.02f, 0.0f, 0.0f },
            QuaternionFromAxisAngle((Vector3){ 1.0f, 0.0f, 0.0f }, (float)bone*DEG2RAD),
            (Vector3){ 1.0f, 1.01f, 1.0f }
        };

        for (int frame = 0; frame < 2; frame++)
        {
            SetAnimationPose(&animationA, frame, (unsigned int)bone, poseA);
            SetAnimationPose(&animationB, frame, (unsigned int)bone, poseB);
        }
        mask[bone] = (unsigned char)(bone%2);
    }

    ModelAnimationLayer layers[4] = {
        { &animationA.animation, 0.0f, 1.0f, NULL, MODEL_ANIMATION_BLEND_REPLACE },
        { &animationB.animation, 0.0f, 1.0f, NULL, MODEL_ANIMATION_BLEND_REPLACE },
        { &animationA.animation, 0.0f, 0.5f, mask, MODEL_ANIMATION_BLEND_ADDITIVE },
        { &animationB.animation, 0.0f, 0.5f, mask, MODEL_ANIMATION_BLEND_ADDITIVE }
    };

    const int iterations = 500;
    clock_t start = clock();
    for (int iteration = 0; iteration < iterations; iteration++)
    {
        layers[0].frame = (float)iteration*0.01f;
        layers[1].frame = (float)iteration*0.02f;
        layers[2].frame = (float)iteration*0.03f;
        layers[3].frame = (float)iteration*0.04f;
        UpdateModelAnimationBlend(testModel.model, layers, 4, true);
    }
    double elapsedMs = ((double)(clock() - start)*1000.0)/(double)CLOCKS_PER_SEC;
    double averageMs = elapsedMs/iterations;

    printf("Animation blend performance: %.3f ms/update (%i bones, 4 layers)\n", averageMs, TEST_MAX_BONES);
    CHECK(averageMs < 2.0, "100-bone blend performance below 2 ms");
}

int main(void)
{
    TestInterpolation();
    TestWeightNormalization();
    TestBoneMaskAndAdditive();
    TestCrossFadeContinuity();
    TestLegacyBlendCompatibility();
    TestGpuSoftwareConsistency();
    TestInvalidInput();
    TestPerformance();

    if (testFailures == 0)
    {
        printf("All animation blending tests passed\n");
        return 0;
    }

    fprintf(stderr, "%i animation blending test(s) failed\n", testFailures);
    return 1;
}
