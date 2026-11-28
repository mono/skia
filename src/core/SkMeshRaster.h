/*
 * Copyright 2026 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkMeshRaster_DEFINED
#define SkMeshRaster_DEFINED

#include "include/core/SkRefCnt.h"

class SkBlender;
class SkDevice;
class SkMesh;
class SkPaint;

namespace SkMeshRaster {

/**
 * Draws an SkMesh with the CPU backend. The mesh's vertex program runs once per vertex (wrapped as
 * a runtime shader that writes the position and varyings into an F32 surface); the triangles are
 * then drawn with drawVertices and a runtime shader that interpolates the varyings of the
 * triangle's corners at full precision and runs the mesh's fragment program, the paint's shader
 * (or color) and the blender. Meshes whose buffers live on a GPU are not drawn.
 */
void Draw(SkDevice*, const SkMesh&, sk_sp<SkBlender>, const SkPaint&);

}  // namespace SkMeshRaster

#endif
