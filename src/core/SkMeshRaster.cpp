/*
 * Copyright 2026 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/core/SkMeshRaster.h"

#include "include/core/SkAlphaType.h"
#include "include/core/SkBlendMode.h"
#include "include/core/SkBlender.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkData.h"
#include "include/core/SkImage.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkMaskFilter.h"
#include "include/core/SkMesh.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPathEffect.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkSamplingOptions.h"
#include "include/core/SkShader.h"
#include "include/core/SkString.h"
#include "include/core/SkSurface.h"
#include "include/core/SkTileMode.h"
#include "include/core/SkVertices.h"
#include "include/effects/SkRuntimeEffect.h"
#include "src/core/SkDevice.h"
#include "src/core/SkMeshPriv.h"
#include "src/core/SkRuntimeEffectPriv.h"
#include "src/sksl/ir/SkSLProgram.h"

#include <cstring>
#include <string>
#include <vector>

namespace {

using Attribute = SkMeshSpecification::Attribute;
using Varying = SkMeshSpecification::Varying;

// Triangles drawn by one drawVertices call: the triangle index is encoded in the texture
// coordinates as 256 x 256 cells (see make_fragment_source), so it stays small enough for float
// interpolation to keep the barycentric weights exact.
constexpr int kTrianglesPerBatch = 256 * 256;

int attribute_floats(Attribute::Type type) {
    switch (type) {
        case Attribute::Type::kFloat:  return 1;
        case Attribute::Type::kFloat2: return 2;
        case Attribute::Type::kFloat3: return 3;
        case Attribute::Type::kFloat4: return 4;
        case Attribute::Type::kUByte4_unorm: return 4;
    }
    SkUNREACHABLE;
}

const char* attribute_type(Attribute::Type type) {
    switch (type) {
        case Attribute::Type::kFloat:  return "float";
        case Attribute::Type::kFloat2: return "float2";
        case Attribute::Type::kFloat3: return "float3";
        case Attribute::Type::kFloat4: return "float4";
        case Attribute::Type::kUByte4_unorm: return "half4";
    }
    SkUNREACHABLE;
}

int varying_floats(Varying::Type type) {
    switch (type) {
        case Varying::Type::kFloat:  case Varying::Type::kHalf:  return 1;
        case Varying::Type::kFloat2: case Varying::Type::kHalf2: return 2;
        case Varying::Type::kFloat3: case Varying::Type::kHalf3: return 3;
        case Varying::Type::kFloat4: case Varying::Type::kHalf4: return 4;
    }
    SkUNREACHABLE;
}

const char* varying_type(Varying::Type type) {
    switch (type) {
        case Varying::Type::kFloat:  return "float";
        case Varying::Type::kFloat2: return "float2";
        case Varying::Type::kFloat3: return "float3";
        case Varying::Type::kFloat4: return "float4";
        case Varying::Type::kHalf:   return "half";
        case Varying::Type::kHalf2:  return "half2";
        case Varying::Type::kHalf3:  return "half3";
        case Varying::Type::kHalf4:  return "half4";
    }
    SkUNREACHABLE;
}

bool is_identifier_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

// The program's `main` function renamed to `name`, so a wrapper can define its own main and call it.
std::string rename_main(const std::string& source, const char* name) {
    std::string out;
    out.reserve(source.size() + 16);
    size_t i = 0;
    while (i < source.size()) {
        bool token = source.compare(i, 4, "main") == 0 &&
                     (i == 0 || !is_identifier_char(source[i - 1])) &&
                     (i + 4 >= source.size() || !is_identifier_char(source[i + 4]));
        if (token) {
            size_t j = i + 4;
            while (j < source.size() && (source[j] == ' ' || source[j] == '\t' ||
                                         source[j] == '\n' || source[j] == '\r')) {
                ++j;
            }
            if (j < source.size() && source[j] == '(') {
                out += name;
                i += 4;
                continue;
            }
        }
        out += source[i++];
    }
    return out;
}

// Float `k` of a packed row set named `prefix`: four floats per row (an F32 RGBA pixel).
std::string packed(const char* prefix, int k) {
    return std::string(prefix) + std::to_string(k / 4) + "." + "xyzw"[k % 4];
}

// The value of a field of `count` floats starting at packed float `first`, as `type`.
std::string packed_value(const char* type, const char* prefix, int first, int count) {
    if (count == 1) {
        return std::string(type) + "(" + packed(prefix, first) + ")";
    }
    std::string value = std::string(type) + "(";
    for (int c = 0; c < count; ++c) {
        value += (c ? ", " : "") + packed(prefix, first + c);
    }
    return value + ")";
}

int rows_for(int floats) { return std::max(1, (floats + 3) / 4); }

// The vertex wrapper: drawn over a surface one pixel wide per vertex and one row per four output
// floats, it reads the vertex's attributes from an F32 image, runs the mesh's vertex program and
// returns the four floats of its row (the position first, then the varyings in their order).
std::string make_vertex_source(const SkMeshSpecification& spec, int attributeRows) {
    std::string sksl = rename_main(*SkMeshSpecificationPriv::VS(spec)->fSource, "sk_mesh_vs");
    sksl += "\nuniform shader sk_mesh_attributes;\n"
            "half4 main(float2 p) {\n"
            "    float x = floor(p.x) + 0.5;\n"
            "    int row = int(p.y);\n";
    for (int r = 0; r < attributeRows; ++r) {
        sksl += "    float4 a" + std::to_string(r) + " = sk_mesh_attributes.eval(float2(x, " +
                std::to_string(r) + ".5));\n";
    }
    sksl += "    Attributes attributes;\n";
    int k = 0;
    for (const Attribute& a : spec.attributes()) {
        int n = attribute_floats(a.type);
        sksl += std::string("    attributes.") + a.name.c_str() + " = " +
                packed_value(attribute_type(a.type), "a", k, n) + ";\n";
        k += n;
    }
    sksl += "    Varyings v = sk_mesh_vs(attributes);\n";
    std::vector<std::string> outputs;
    auto varyings = SkMeshSpecificationPriv::Varyings(spec);
    for (const Varying& v : varyings) {
        if (v.name.equals("position")) {
            outputs.insert(outputs.begin(), {"v.position.x", "v.position.y"});
        }
    }
    for (const Varying& v : varyings) {
        if (v.name.equals("position")) {
            continue;
        }
        int n = varying_floats(v.type);
        for (int c = 0; c < n; ++c) {
            outputs.push_back(std::string("float(v.") + v.name.c_str() +
                              (n == 1 ? std::string(")") : std::string(".") + "xyzw"[c] + ")"));
        }
    }
    for (int r = 0; r < rows_for((int)outputs.size()); ++r) {
        sksl += "    if (row == " + std::to_string(r) + ") { return half4(";
        for (int c = 0; c < 4; ++c) {
            size_t index = r * 4 + c;
            sksl += (c ? ", " : "") + (index < outputs.size() ? outputs[index] : std::string("0"));
        }
        sksl += "); }\n";
    }
    sksl += "    return half4(0);\n}\n";
    return sksl;
}

// The fragment wrapper, the shader of the drawVertices call. Each triangle's three corners have the
// texture coordinates (1, 1), (2, 1) and (1, 2) inside the cell of its index (cells of 4 x 4, 256
// per row), so the interpolated coordinate gives the triangle and its barycentric weights. The
// varyings of the three corners are fetched from the vertex pass, interpolated, and the mesh's
// fragment program, the paint's shader and the blender run as on the GPU.
std::string make_fragment_source(const SkMeshSpecification& spec, int varyingRows) {
    std::string sksl = rename_main(*SkMeshSpecificationPriv::FS(spec)->fSource, "sk_mesh_fs");
    bool hasColor = SkMeshSpecificationPriv::HasColors(spec);
    sksl += "\nuniform shader sk_mesh_varyings;\n"
            "uniform shader sk_mesh_triangles;\n"
            "uniform shader sk_mesh_paint;\n";
    if (hasColor) {
        sksl += "uniform blender sk_mesh_blender;\n";
    }
    sksl += "half4 main(float2 tc) {\n"
            "    float2 cell = floor(tc * 0.25);\n"
            "    float2 b = tc - cell * 4.0 - 1.0;\n"
            "    float w = 1.0 - b.x - b.y;\n"
            "    float4 i = sk_mesh_triangles.eval(float2(cell.y * 256.0 + cell.x + 0.5, 0.5)) + 0.5;\n";
    for (int r = 0; r < varyingRows; ++r) {
        std::string y = std::to_string(r) + ".5";
        sksl += "    float4 r" + std::to_string(r) + " = w * sk_mesh_varyings.eval(float2(i.x, " + y +
                ")) + b.x * sk_mesh_varyings.eval(float2(i.y, " + y +
                ")) + b.y * sk_mesh_varyings.eval(float2(i.z, " + y + "));\n";
    }
    sksl += "    Varyings v;\n"
            "    v.position = float2(r0.x, r0.y);\n";
    int k = 2;
    for (const Varying& v : SkMeshSpecificationPriv::Varyings(spec)) {
        if (v.name.equals("position")) {
            continue;
        }
        int n = varying_floats(v.type);
        sksl += std::string("    v.") + v.name.c_str() + " = " +
                packed_value(varying_type(v.type), "r", k, n) + ";\n";
        k += n;
    }
    if (hasColor) {
        bool full = SkMeshSpecificationPriv::GetColorType(spec) ==
                    SkMeshSpecificationPriv::ColorType::kFloat4;
        sksl += full ? "    float4 color;\n" : "    half4 color;\n";
        sksl += "    float2 local = sk_mesh_fs(v, color);\n";
        if (SkMeshSpecificationPriv::AlphaType(spec) == kUnpremul_SkAlphaType) {
            sksl += "    color.rgb *= color.a;\n";
        }
        sksl += "    return sk_mesh_blender.eval(sk_mesh_paint.eval(local), half4(color));\n";
    } else {
        sksl += "    float2 local = sk_mesh_fs(v);\n"
                "    return sk_mesh_paint.eval(local);\n";
    }
    sksl += "}\n";
    return sksl;
}

sk_sp<SkRuntimeEffect> make_effect(const std::string& sksl) {
    return SkMakeCachedRuntimeEffect(SkRuntimeEffect::MakeForShader, SkString(sksl.c_str()));
}

// The effect's uniforms filled from the mesh's (by name: a wrapper sees only one stage's).
sk_sp<SkData> uniforms_for(const SkRuntimeEffect& effect, const SkMesh& mesh) {
    sk_sp<SkData> data = SkData::MakeZeroInitialized(effect.uniformSize());
    const SkData* source = mesh.uniforms();
    if (!source) {
        return data;
    }
    for (const SkRuntimeEffect::Uniform& u : effect.uniforms()) {
        if (const auto* from = mesh.spec()->findUniform(u.name)) {
            if (from->offset + u.sizeInBytes() <= source->size()) {
                memcpy(static_cast<uint8_t*>(data->writable_data()) + u.offset,
                       source->bytes() + from->offset,
                       u.sizeInBytes());
            }
        }
    }
    return data;
}

// The effect's children: the wrapper's own by name, the mesh's by their name in the specification.
std::vector<SkRuntimeEffect::ChildPtr> children_for(
        const SkRuntimeEffect& effect,
        const SkMesh& mesh,
        std::initializer_list<std::pair<const char*, SkRuntimeEffect::ChildPtr>> own) {
    std::vector<SkRuntimeEffect::ChildPtr> children;
    for (const SkRuntimeEffect::Child& c : effect.children()) {
        SkRuntimeEffect::ChildPtr child;
        bool found = false;
        for (const auto& [name, ptr] : own) {
            if (c.name == name) {
                child = ptr;
                found = true;
            }
        }
        if (!found) {
            if (const auto* meshChild = mesh.spec()->findChild(c.name)) {
                if (meshChild->index < (int)mesh.children().size()) {
                    child = mesh.children()[meshChild->index];
                }
            }
        }
        children.push_back(child);
    }
    return children;
}

sk_sp<SkShader> raw_shader(sk_sp<SkImage> image) {
    return image->makeRawShader(SkTileMode::kClamp, SkTileMode::kClamp,
                                SkSamplingOptions(SkFilterMode::kNearest));
}

// The mesh's triangles as triples of vertex indices (relative to the mesh's first vertex).
bool triangles(const SkMesh& mesh, std::vector<int>* out) {
    std::vector<int> order;
    if (auto* ib = static_cast<const SkMeshPriv::IB*>(mesh.indexBuffer())) {
        const void* data = ib->peek();
        if (!data) {
            return false;
        }
        const uint16_t* indices = reinterpret_cast<const uint16_t*>(
                static_cast<const uint8_t*>(data) + mesh.indexOffset());
        order.assign(indices, indices + mesh.indexCount());
    } else {
        order.resize(mesh.vertexCount());
        for (size_t i = 0; i < order.size(); ++i) {
            order[i] = (int)i;
        }
    }
    int vertexCount = (int)mesh.vertexCount();
    auto add = [&](int a, int b, int c) {
        if (a < vertexCount && b < vertexCount && c < vertexCount) {
            out->insert(out->end(), {a, b, c});
        }
    };
    if (mesh.mode() == SkMesh::Mode::kTriangles) {
        for (size_t i = 0; i + 2 < order.size(); i += 3) {
            add(order[i], order[i + 1], order[i + 2]);
        }
    } else {
        for (size_t i = 0; i + 2 < order.size(); ++i) {
            add(order[i], order[i + 1], order[i + 2]);
        }
    }
    return true;
}

}  // namespace

void SkMeshRaster::Draw(SkDevice* device,
                        const SkMesh& mesh,
                        sk_sp<SkBlender> blender,
                        const SkPaint& paint) {
    const SkMeshSpecification& spec = *mesh.spec();
    auto* vb = static_cast<const SkMeshPriv::VB*>(mesh.vertexBuffer());
    const uint8_t* vertexData = vb ? static_cast<const uint8_t*>(vb->peek()) : nullptr;
    int vertexCount = (int)mesh.vertexCount();
    if (!vertexData || vertexCount == 0) {
        return;  // GPU buffers: nothing to read here.
    }
    vertexData += mesh.vertexOffset();

    std::vector<int> tris;
    if (!triangles(mesh, &tris) || tris.empty()) {
        return;
    }

    // 1. The vertices' attributes as floats, four per pixel of an F32 image.
    int attributeFloats = 0;
    for (const Attribute& a : spec.attributes()) {
        attributeFloats += attribute_floats(a.type);
    }
    int attributeRows = rows_for(attributeFloats);
    std::vector<float> attributes(4 * vertexCount * attributeRows, 0.f);
    for (int v = 0; v < vertexCount; ++v) {
        const uint8_t* vertex = vertexData + v * spec.stride();
        int k = 0;
        for (const Attribute& a : spec.attributes()) {
            int n = attribute_floats(a.type);
            for (int c = 0; c < n; ++c, ++k) {
                float value;
                if (a.type == Attribute::Type::kUByte4_unorm) {
                    value = vertex[a.offset + c] / 255.f;
                } else {
                    memcpy(&value, vertex + a.offset + 4 * c, 4);
                }
                attributes[4 * (k / 4 * vertexCount + v) + k % 4] = value;
            }
        }
    }
    auto attributeInfo = SkImageInfo::Make(vertexCount, attributeRows, kRGBA_F32_SkColorType,
                                           kPremul_SkAlphaType);
    sk_sp<SkImage> attributeImage = SkImages::RasterFromData(
            attributeInfo, SkData::MakeWithCopy(attributes.data(), attributes.size() * 4),
            attributeInfo.minRowBytes());

    // 2. The vertex program once per vertex: position and varyings into an F32 surface.
    int varyingFloats = 2;
    for (const Varying& v : SkMeshSpecificationPriv::Varyings(spec)) {
        if (!v.name.equals("position")) {
            varyingFloats += varying_floats(v.type);
        }
    }
    int varyingRows = rows_for(varyingFloats);
    sk_sp<SkRuntimeEffect> vsEffect = make_effect(make_vertex_source(spec, attributeRows));
    sk_sp<SkRuntimeEffect> fsEffect = make_effect(make_fragment_source(spec, varyingRows));
    if (!attributeImage || !vsEffect || !fsEffect) {
        return;
    }
    auto children = children_for(*vsEffect, mesh, {{"sk_mesh_attributes", raw_shader(attributeImage)}});
    sk_sp<SkShader> vsShader = vsEffect->makeShader(uniforms_for(*vsEffect, mesh), children);
    sk_sp<SkSurface> varyings = SkSurfaces::Raster(SkImageInfo::Make(
            vertexCount, varyingRows, kRGBA_F32_SkColorType, kPremul_SkAlphaType));
    if (!vsShader || !varyings) {
        return;
    }
    SkPaint vsPaint;
    vsPaint.setShader(vsShader);
    vsPaint.setBlendMode(SkBlendMode::kSrc);
    varyings->getCanvas()->drawPaint(vsPaint);
    SkPixmap outputs;
    if (!varyings->peekPixels(&outputs)) {
        return;
    }
    sk_sp<SkShader> varyingShader = raw_shader(varyings->makeImageSnapshot());

    // 3. The paint's shader (or color) as the fragment wrapper's source color, with the paint's
    //    alpha, and the paint drawn with the wrapper. Mask filters, path effects and antialiasing
    //    do not apply to meshes.
    SkColor4f color = paint.getColor4f();
    sk_sp<SkShader> source = paint.refShader()
            ? SkShaders::Blend(SkBlendMode::kDstIn, paint.refShader(),
                               SkShaders::Color({0, 0, 0, color.fA}, nullptr))
            : SkShaders::Color(color, nullptr);
    if (!blender) {
        blender = SkBlender::Mode(SkBlendMode::kModulate);
    }
    SkPaint drawPaint(paint);
    drawPaint.setMaskFilter(nullptr);
    drawPaint.setPathEffect(nullptr);
    drawPaint.setAntiAlias(false);
    drawPaint.setColor4f({1, 1, 1, 1}, nullptr);
    sk_sp<SkData> fsUniforms = uniforms_for(*fsEffect, mesh);

    // 4. The triangles, in batches, each corner at its position from the vertex pass.
    auto position = [&](int v) {
        const float* p = static_cast<const float*>(outputs.addr(v, 0));
        return SkPoint{p[0], p[1]};
    };
    int triangleCount = (int)tris.size() / 3;
    for (int first = 0; first < triangleCount; first += kTrianglesPerBatch) {
        int count = std::min(kTrianglesPerBatch, triangleCount - first);
        std::vector<float> indices(4 * count, 0.f);
        std::vector<SkPoint> points(3 * count), coords(3 * count);
        for (int t = 0; t < count; ++t) {
            const int* tri = &tris[3 * (first + t)];
            float cx = 4.f * (t % 256) + 1.f, cy = 4.f * (t / 256) + 1.f;
            for (int c = 0; c < 3; ++c) {
                indices[4 * t + c] = (float)tri[c];
                points[3 * t + c] = position(tri[c]);
            }
            coords[3 * t + 0] = {cx, cy};
            coords[3 * t + 1] = {cx + 1, cy};
            coords[3 * t + 2] = {cx, cy + 1};
        }
        auto indexInfo = SkImageInfo::Make(count, 1, kRGBA_F32_SkColorType, kPremul_SkAlphaType);
        sk_sp<SkImage> indexImage = SkImages::RasterFromData(
                indexInfo, SkData::MakeWithCopy(indices.data(), indices.size() * 4),
                indexInfo.minRowBytes());
        if (!indexImage) {
            return;
        }
        auto fsChildren = children_for(*fsEffect, mesh,
                                       {{"sk_mesh_varyings", varyingShader},
                                        {"sk_mesh_triangles", raw_shader(indexImage)},
                                        {"sk_mesh_paint", source},
                                        {"sk_mesh_blender", blender}});
        drawPaint.setShader(fsEffect->makeShader(fsUniforms, fsChildren));
        sk_sp<SkVertices> vertices = SkVertices::MakeCopy(SkVertices::kTriangles_VertexMode,
                                                          3 * count, points.data(), coords.data(),
                                                          nullptr);
        device->drawVertices(vertices.get(), SkBlender::Mode(SkBlendMode::kModulate), drawPaint,
                             /*skipColorXform=*/false);
    }
}
