# Skeletal Animation Blending

raylib supports weighted skeletal animation layers with cross-fades, additive
animation, per-bone masks and normalized replace weights. The final blended pose
is written to `Model.currentPose` and `Model.boneMatrices`, so the existing CPU
and GPU skinning paths consume the same result.

## API

```c
typedef enum {
    MODEL_ANIMATION_BLEND_REPLACE = 0,
    MODEL_ANIMATION_BLEND_ADDITIVE = 1
} ModelAnimationBlendMode;

typedef struct ModelAnimationLayer {
    const ModelAnimation *animation;
    float frame;
    float weight;
    const unsigned char *boneMask;
    ModelAnimationBlendMode mode;
} ModelAnimationLayer;

void UpdateModelAnimationBlend(
    Model model,
    const ModelAnimationLayer *layers,
    int layerCount,
    bool normalizeWeights);

Vector3 GetModelAnimationSkinnedVertex(Model model, int meshIndex, int vertexIndex);
```

`UpdateModelAnimation()` and `UpdateModelAnimationEx()` remain available and now
use the same blending implementation internally. `UpdateModelAnimationEx()` is
the two-clip cross-fade shorthand:

```c
UpdateModelAnimationEx(model, walk, walkFrame, run, runFrame, blend);
```

is equivalent to two replace layers with weights `1.0f - blend` and `blend`.

## Parameters

- `animation`: Animation to sample. Every active layer must have valid keyframes,
  a valid pose array and at least `model.skeleton.boneCount` bones.
- `frame`: Fractional animation frame. Values wrap around the clip duration.
- `weight`: Non-negative layer weight. Negative or non-finite values are rejected.
- `boneMask`: Optional array with at least `model.skeleton.boneCount` entries.
  A non-zero entry enables that bone for the layer. `NULL` enables every bone.
- `mode`: `MODEL_ANIMATION_BLEND_REPLACE` blends the sampled pose into the base
  pose. `MODEL_ANIMATION_BLEND_ADDITIVE` applies a bind-relative delta after all
  replace layers.
- `normalizeWeights`: When true, all enabled replace layers are normalized
  independently per bone. Additive layers are not normalized because their
  weights represent delta intensity.

When `normalizeWeights` is false, replace weights below one leave the remaining
influence on the bind pose. Replace weights above one are normalized. If no
replace layer affects a bone, the bind pose is used, producing an identity bone
matrix. A layer with zero weight has no effect and may be disabled without
reinitializing the model.

Additive layers compute their delta relative to the bind pose and apply it on top
of the normalized base pose. This makes a weight-zero additive layer an exact
no-op and prevents an additive clip from replacing the base animation.

## Example

```c
unsigned char upperBodyMask[boneCount] = { 0 };
for (unsigned int bone = 0; bone < boneCount; bone++)
    upperBodyMask[bone] = IsUpperBodyBone(model.skeleton.bones[bone].name);

ModelAnimationLayer layers[3] = {
    { &walk, walkFrame, 1.0f - crossFade, NULL, MODEL_ANIMATION_BLEND_REPLACE },
    { &run, runFrame, crossFade, NULL, MODEL_ANIMATION_BLEND_REPLACE },
    { &wave, waveFrame, 0.45f, upperBodyMask, MODEL_ANIMATION_BLEND_ADDITIVE }
};

UpdateModelAnimationBlend(model, layers, 3, true);
DrawModel(model, position, 1.0f, WHITE);
```

The complete example is
[`examples/models/models_animation_layers.c`](../examples/models/models_animation_layers.c).

## GPU and CPU Skinning

`UpdateModelAnimationBlend()` writes the same final matrix array used by:

- GPU skinning in `DrawModelEx()`, through the `boneMatrices` shader uniform.
- CPU skinning fallback in the animated vertex buffers.

`GetModelAnimationSkinnedVertex()` evaluates one vertex on the CPU with those
matrices. It is useful for animation tests, bounds checks and comparisons against
a GPU skinning shader.

When GPU skinning is enabled and a material exposes bone vertex attributes, the
CPU vertex upload is intentionally skipped to avoid two skinning paths modifying
the same mesh.

## Tests

Configure and run the unit tests without building the example collection:

```sh
cmake -S . -B build -DBUILD_EXAMPLES=OFF -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

The tests cover:

- Fractional frame interpolation and frame wrapping.
- Replace-layer weight normalization, partial weights and zero weights.
- Per-bone masks on replace and additive layers.
- Cross-fade translation and rotation continuity.
- GPU-equivalent and software-skinned vertex consistency.
- Invalid frame, weight and layer input handling.
- A 100-bone, four-layer performance check against a 60 FPS frame budget.

The performance test prints the measured cost per update and fails if it exceeds
2 ms. A typical debug build is below 0.1 ms per update on a desktop CPU.

To also run the render check, which opens a hidden window and verifies that the
blended robot produces visible pixels:

```sh
cmake -S . -B build -DBUILD_EXAMPLES=OFF -DBUILD_TESTING=ON -DRAYLIB_RENDER_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

The render check exits with code 77 (CTest skip) when no graphics context is
available.

To run the visual example:

```sh
cd examples
make models/models_animation_layers PLATFORM=PLATFORM_DESKTOP
cd models
./models_animation_layers
```

Press `SPACE` to toggle the masked additive layer. The example reports cross-fade
weight and the most recent blend update time on screen.

## Error Handling

Invalid runtime data, layer lists, animation data, frames, weights or blend modes
produce a warning that includes the function, trigger condition and input values.
The call returns without modifying the current pose, so a bad frame cannot leave
a partially blended skeleton or index outside the keyframe arrays.

## Known Limitations

- Animation layers operate on raylib's stored keyframe poses. Additive deltas are
  bind-relative and are not a replacement for a full local-space animation graph.
- Additive weights are intensity values and are not divided by the sum of
  additive layers.
- Masks are raw byte arrays; raylib cannot validate their allocated length.
- One model uses one skeleton. Layers must target animation clips compatible with
  that skeleton.
- GPU skinning still requires a shader following raylib's bone attribute and
  `boneMatrices` uniform naming conventions.
