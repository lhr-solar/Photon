#include "gltf.hpp"
#include "glm/detail/func_matrix.hpp"
#include <tiny_gltf.h>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>

void Gltf::init(GPU& gpu, const unsigned char* newModel, size_t size){
    if (newModel == nullptr || size == 0)
        return;
    std::string err = {}, warn = {};
    model = tinygltf::Model{};
    loader = tinygltf::TinyGLTF{};
    source.assign(newModel, newModel + size);
    bool ok = loader.LoadBinaryFromMemory(&model, &err, &warn, source.data(), 
              static_cast<unsigned int>(source.size()), "assets/models");
    int sceneIndex = model.defaultScene >= 0 ? model.defaultScene : 0;
    vertices.clear();
    drawItems.clear();
    materials.clear();
    const tinygltf::Scene& scene = model.scenes[sceneIndex];
    std::vector<PrimitiveRange> ranges;
    for (int nodeIndex : scene.nodes)
        appendNodeMesh(model, nodeIndex, glm::mat4(1.0f), vertices, ranges);

    glm::vec3 minP(vertices[0].pos[0], vertices[0].pos[1], vertices[0].pos[2]);
    glm::vec3 maxP = minP;
    for (const auto& v : vertices) {
        glm::vec3 p(v.pos[0], v.pos[1], v.pos[2]);
        minP = glm::min(minP, p);
        maxP = glm::max(maxP, p);
    }
    glm::vec3 center = (minP + maxP) * 0.5f;
    glm::vec3 extents = maxP - minP;
    float maxExtent = std::max(extents.x, std::max(extents.y, extents.z)) * 0.5f;
    float scale = (maxExtent > 0.0f) ? (1.0f / maxExtent) : 1.0f;
    for (auto& v : vertices) {
        glm::vec3 p(v.pos[0], v.pos[1], v.pos[2]);
        p = (p - center) * scale;
        v.pos[0] = p.x;
        v.pos[1] = p.y;
        v.pos[2] = p.z;
    }

    camera.position = {2.5f, 0.0f, 0.0f};
    camera.yaw = 180.0f;
    camera.pitch = 0.0f;
    camera.front = {-1.0f, 0.0f, 0.0f};
    camera.up = {0.0f, 0.0f, 1.0f};

    materials.reserve(model.materials.size() + 1);
    for (const auto& mat : model.materials) {
        MaterialRuntime runtimeMat{};
        if (mat.values.find("baseColorFactor") != mat.values.end()) {
            const auto& cf = mat.values.at("baseColorFactor").ColorFactor();
            runtimeMat.params.baseColorFactor = glm::vec4(
                static_cast<float>(cf[0]),
                static_cast<float>(cf[1]),
                static_cast<float>(cf[2]),
                static_cast<float>(cf[3]));
        }
        if (mat.values.find("baseColorTexture") != mat.values.end()) {
            runtimeMat.baseColorTextureIndex = mat.values.at("baseColorTexture").TextureIndex();
            runtimeMat.params.hasBaseColorTexture = runtimeMat.baseColorTextureIndex >= 0 ? 1 : 0;
        }
        if (mat.values.find("metallicFactor") != mat.values.end()) {
            runtimeMat.params.metallicFactor = static_cast<float>(mat.values.at("metallicFactor").Factor());
        }
        if (mat.values.find("roughnessFactor") != mat.values.end()) {
            runtimeMat.params.roughnessFactor = static_cast<float>(mat.values.at("roughnessFactor").Factor());
        }
        if (mat.values.find("metallicRoughnessTexture") != mat.values.end()) {
            runtimeMat.metallicRoughnessTextureIndex = mat.values.at("metallicRoughnessTexture").TextureIndex();
            runtimeMat.params.hasMetallicRoughnessTexture = runtimeMat.metallicRoughnessTextureIndex >= 0 ? 1 : 0;
        }

        if (mat.additionalValues.find("normalTexture") != mat.additionalValues.end()) {
            runtimeMat.normalTextureIndex = mat.additionalValues.at("normalTexture").TextureIndex();
            runtimeMat.params.hasNormalTexture = runtimeMat.normalTextureIndex >= 0 ? 1 : 0;
        }
        if (mat.additionalValues.find("normalScale") != mat.additionalValues.end()) {
            runtimeMat.params.normalScale = static_cast<float>(mat.additionalValues.at("normalScale").Factor());
        }
        if (mat.additionalValues.find("occlusionTexture") != mat.additionalValues.end()) {
            runtimeMat.occlusionTextureIndex = mat.additionalValues.at("occlusionTexture").TextureIndex();
            runtimeMat.params.hasOcclusionTexture = runtimeMat.occlusionTextureIndex >= 0 ? 1 : 0;
        }
        if (mat.additionalValues.find("occlusionStrength") != mat.additionalValues.end()) {
            runtimeMat.params.occlusionStrength = static_cast<float>(mat.additionalValues.at("occlusionStrength").Factor());
        }
        if (mat.additionalValues.find("emissiveTexture") != mat.additionalValues.end()) {
            runtimeMat.emissiveTextureIndex = mat.additionalValues.at("emissiveTexture").TextureIndex();
            runtimeMat.params.hasEmissiveTexture = runtimeMat.emissiveTextureIndex >= 0 ? 1 : 0;
        }
        if (mat.additionalValues.find("emissiveFactor") != mat.additionalValues.end()) {
            const auto& ef = mat.additionalValues.at("emissiveFactor").ColorFactor();
            runtimeMat.params.emissiveFactor = glm::vec4(
                static_cast<float>(ef[0]),
                static_cast<float>(ef[1]),
                static_cast<float>(ef[2]),
                1.0f);
        }
        if (mat.additionalValues.find("alphaCutoff") != mat.additionalValues.end()) {
            runtimeMat.params.alphaCutoff = static_cast<float>(mat.additionalValues.at("alphaCutoff").Factor());
        }
        if (mat.additionalValues.find("alphaMode") != mat.additionalValues.end()) {
            const std::string mode = mat.additionalValues.at("alphaMode").string_value;
            if (mode == "MASK") runtimeMat.params.alphaMode = 1;
            else if (mode == "BLEND") runtimeMat.params.alphaMode = 2;
            else runtimeMat.params.alphaMode = 0;
        }
        materials.push_back(runtimeMat);
    }
    if (materials.empty())  materials.push_back(MaterialRuntime{});
    for (const auto& r : ranges) {
        DrawItem d{};
        d.firstVertex = r.firstVertex;
        d.vertexCount = r.vertexCount;
        d.materialIndex = (r.materialIndex >= 0 && r.materialIndex < static_cast<int>(materials.size()))
            ? static_cast<uint32_t>(r.materialIndex)
            : 0;
        drawItems.push_back(d);
    }
    if (drawItems.empty()) drawItems.push_back({0u, static_cast<uint32_t>(vertices.size()), 0u});

    // at this point, the gltf model stuff is mostly done, now, we should initialize the rendering stuff, 
    // this should closely resemble the Shader.hpp/cpp file

};

void Gltf::render(GPU& gpu, VkCommandBuffer& commandBuffer){
    if(dirty)rebuild(gpu);

};

void Gltf::rebuild(GPU& gpu){
    dirty = false;
};

void Gltf::destroy(){

};

void Gltf::appendNodeMesh(const tinygltf::Model& model,
                           int nodeIndex,
                           const glm::mat4& parentMatrix,
                           std::vector<GltfVertex>& outVertices,
                           std::vector<PrimitiveRange>& outRanges) {
    const tinygltf::Node& node = model.nodes[nodeIndex];
    glm::mat4 world = parentMatrix * nodeLocalMatrix(node);
    if (node.mesh >= 0) {
        const tinygltf::Mesh& mesh = model.meshes[node.mesh];
        for (const tinygltf::Primitive& primitive : mesh.primitives) {
            appendPrimitiveVertices(model, primitive, world, outVertices, outRanges);
        }
    }
    for (int child : node.children) {
        appendNodeMesh(model, child, world, outVertices, outRanges);
    }
}

void Gltf::appendPrimitiveVertices(const tinygltf::Model& model,
                                    const tinygltf::Primitive& primitive,
                                    const glm::mat4& worldMatrix,
                                    std::vector<GltfVertex>& outVertices,
                                    std::vector<PrimitiveRange>& outRanges) {
    auto posIt = primitive.attributes.find("POSITION");
    if (posIt == primitive.attributes.end()) {
        return;
    }

    const tinygltf::Accessor& posAccessor = model.accessors[posIt->second];
    if (posAccessor.bufferView < 0 || static_cast<size_t>(posAccessor.bufferView) >= model.bufferViews.size()) {
        return;
    }
    const tinygltf::BufferView& posView = model.bufferViews[posAccessor.bufferView];
    if (posView.buffer < 0 || static_cast<size_t>(posView.buffer) >= model.buffers.size()) {
        return;
    }
    const tinygltf::Buffer& posBuffer = model.buffers[posView.buffer];
    if (posView.byteOffset + posAccessor.byteOffset >= posBuffer.data.size()) {
        return;
    }
    const uint8_t* posData = posBuffer.data.data() + posView.byteOffset + posAccessor.byteOffset;
    const size_t posStride = posAccessor.ByteStride(posView) ? posAccessor.ByteStride(posView) : sizeof(float) * 3;

    bool hasNormals = false;
    const uint8_t* normalData = nullptr;
    size_t normalStride = 0;
    auto normalIt = primitive.attributes.find("NORMAL");
    if (normalIt != primitive.attributes.end()) {
        const tinygltf::Accessor& normalAccessor = model.accessors[normalIt->second];
        if (normalAccessor.bufferView >= 0 && static_cast<size_t>(normalAccessor.bufferView) < model.bufferViews.size()) {
            const tinygltf::BufferView& normalView = model.bufferViews[normalAccessor.bufferView];
            if (normalView.buffer >= 0 && static_cast<size_t>(normalView.buffer) < model.buffers.size()) {
                const tinygltf::Buffer& normalBuffer = model.buffers[normalView.buffer];
                if (normalView.byteOffset + normalAccessor.byteOffset < normalBuffer.data.size()) {
                    normalData = normalBuffer.data.data() + normalView.byteOffset + normalAccessor.byteOffset;
                    normalStride = normalAccessor.ByteStride(normalView) ? normalAccessor.ByteStride(normalView) : sizeof(float) * 3;
                    hasNormals = true;
                }
            }
        }
    }

    bool hasUV0 = false;
    const uint8_t* uvData = nullptr;
    size_t uvStride = 0;
    auto uvIt = primitive.attributes.find("TEXCOORD_0");
    if (uvIt != primitive.attributes.end()) {
        const tinygltf::Accessor& uvAccessor = model.accessors[uvIt->second];
        if (uvAccessor.bufferView >= 0 && static_cast<size_t>(uvAccessor.bufferView) < model.bufferViews.size()) {
            const tinygltf::BufferView& uvView = model.bufferViews[uvAccessor.bufferView];
            if (uvView.buffer >= 0 && static_cast<size_t>(uvView.buffer) < model.buffers.size()) {
                const tinygltf::Buffer& uvBuffer = model.buffers[uvView.buffer];
                if (uvView.byteOffset + uvAccessor.byteOffset < uvBuffer.data.size()) {
                    uvData = uvBuffer.data.data() + uvView.byteOffset + uvAccessor.byteOffset;
                    uvStride = uvAccessor.ByteStride(uvView) ? uvAccessor.ByteStride(uvView) : sizeof(float) * 2;
                    hasUV0 = true;
                }
            }
        }
    }

    bool hasColor = false;
    const uint8_t* colorData = nullptr;
    size_t colorStride = 0;
    int colorType = TINYGLTF_TYPE_VEC3;
    int colorComponentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
    bool colorNormalized = false;
    auto colorIt = primitive.attributes.find("COLOR_0");
    if (colorIt != primitive.attributes.end()) {
        const tinygltf::Accessor& colorAccessor = model.accessors[colorIt->second];
        if (colorAccessor.bufferView >= 0 && static_cast<size_t>(colorAccessor.bufferView) < model.bufferViews.size()) {
            const tinygltf::BufferView& colorView = model.bufferViews[colorAccessor.bufferView];
            if (colorView.buffer >= 0 && static_cast<size_t>(colorView.buffer) < model.buffers.size()) {
                const tinygltf::Buffer& colorBuffer = model.buffers[colorView.buffer];
                if (colorView.byteOffset + colorAccessor.byteOffset < colorBuffer.data.size()) {
                    colorData = colorBuffer.data.data() + colorView.byteOffset + colorAccessor.byteOffset;
                    colorStride = colorAccessor.ByteStride(colorView) ?
                        colorAccessor.ByteStride(colorView) :
                        (tinygltf::GetNumComponentsInType(colorAccessor.type) *
                         tinygltf::GetComponentSizeInBytes(colorAccessor.componentType));
                    colorType = colorAccessor.type;
                    colorComponentType = colorAccessor.componentType;
                    colorNormalized = colorAccessor.normalized;
                    hasColor = true;
                }
            }
        }
    }

    auto readColor = [&](uint32_t vertexIndex) -> glm::vec3 {
        if (!hasColor) {
            return glm::vec3(1.0f);
        }
        const uint8_t* p = colorData + vertexIndex * colorStride;
        const int componentCount = tinygltf::GetNumComponentsInType(colorType);
        glm::vec3 c(1.0f);
        switch (colorComponentType) {
            case TINYGLTF_COMPONENT_TYPE_FLOAT: {
                const float* v = reinterpret_cast<const float*>(p);
                c.r = v[0];
                c.g = v[1];
                c.b = v[2];
                break;
            }
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: {
                const uint8_t* v = reinterpret_cast<const uint8_t*>(p);
                if (colorNormalized) {
                    c.r = v[0] / 255.0f;
                    c.g = v[1] / 255.0f;
                    c.b = v[2] / 255.0f;
                } else {
                    c.r = static_cast<float>(v[0]);
                    c.g = static_cast<float>(v[1]);
                    c.b = static_cast<float>(v[2]);
                }
                break;
            }
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
                const uint16_t* v = reinterpret_cast<const uint16_t*>(p);
                if (colorNormalized) {
                    c.r = v[0] / 65535.0f;
                    c.g = v[1] / 65535.0f;
                    c.b = v[2] / 65535.0f;
                } else {
                    c.r = static_cast<float>(v[0]);
                    c.g = static_cast<float>(v[1]);
                    c.b = static_cast<float>(v[2]);
                }
                break;
            }
            default:
                break;
        }
        if (componentCount < 3) {
            return glm::vec3(1.0f);
        }
        return c;
    };

    const uint32_t firstVertex = static_cast<uint32_t>(outVertices.size());

    auto emitVertex = [&](uint32_t vertexIndex) {
        const float* p = reinterpret_cast<const float*>(posData + vertexIndex * posStride);
        glm::vec4 worldPos = worldMatrix * glm::vec4(p[0], p[1], p[2], 1.0f);
        glm::vec3 color = readColor(vertexIndex);
        glm::vec3 normal(0.0f, 0.0f, 1.0f);
        if (hasNormals) {
            const float* n = reinterpret_cast<const float*>(normalData + vertexIndex * normalStride);
            normal = glm::normalize(glm::mat3(worldMatrix) * glm::vec3(n[0], n[1], n[2]));
        }
        glm::vec2 uv0(0.0f);
        if (hasUV0) {
            const float* uv = reinterpret_cast<const float*>(uvData + vertexIndex * uvStride);
            uv0 = glm::vec2(uv[0], uv[1]);
        }

        GltfVertex out{};
        out.pos[0] = worldPos.x;
        out.pos[1] = worldPos.y;
        out.pos[2] = worldPos.z;
        out.color[0] = color.r;
        out.color[1] = color.g;
        out.color[2] = color.b;
        out.uv[0] = uv0.x;
        out.uv[1] = uv0.y;
        out.normal[0] = normal.x;
        out.normal[1] = normal.y;
        out.normal[2] = normal.z;
        outVertices.push_back(out);
    };

    if (primitive.indices < 0) {
        for (uint32_t i = 0; i < posAccessor.count; i++) {
            emitVertex(i);
        }
        const uint32_t vertexCount = static_cast<uint32_t>(outVertices.size()) - firstVertex;
        if (vertexCount > 0) {
            outRanges.push_back({firstVertex, vertexCount, primitive.material});
        }
        return;
    }

    const tinygltf::Accessor& indexAccessor = model.accessors[primitive.indices];
    if (indexAccessor.bufferView < 0 || static_cast<size_t>(indexAccessor.bufferView) >= model.bufferViews.size()) {
        return;
    }
    const tinygltf::BufferView& indexView = model.bufferViews[indexAccessor.bufferView];
    if (indexView.buffer < 0 || static_cast<size_t>(indexView.buffer) >= model.buffers.size()) {
        return;
    }
    const tinygltf::Buffer& indexBuffer = model.buffers[indexView.buffer];
    if (indexView.byteOffset + indexAccessor.byteOffset >= indexBuffer.data.size()) {
        return;
    }
    const uint8_t* indexData = indexBuffer.data.data() + indexView.byteOffset + indexAccessor.byteOffset;
    const size_t indexStride = indexAccessor.ByteStride(indexView) ?
        indexAccessor.ByteStride(indexView) :
        tinygltf::GetComponentSizeInBytes(indexAccessor.componentType);

    for (uint32_t i = 0; i < indexAccessor.count; i++) {
        const uint8_t* p = indexData + i * indexStride;
        uint32_t index = 0;
        switch (indexAccessor.componentType) {
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                index = *reinterpret_cast<const uint8_t*>(p);
                break;
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                index = *reinterpret_cast<const uint16_t*>(p);
                break;
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
                index = *reinterpret_cast<const uint32_t*>(p);
                break;
            default:
                continue;
        }
        emitVertex(index);
    }

    const uint32_t vertexCount = static_cast<uint32_t>(outVertices.size()) - firstVertex;
    if (vertexCount > 0) {
        outRanges.push_back({firstVertex, vertexCount, primitive.material});
    }
}

glm::mat4 Gltf::nodeLocalMatrix(const tinygltf::Node& node) {
    glm::mat4 matrix = glm::mat4(1.0f);
    if (node.matrix.size() == 16) {
        matrix = glm::make_mat4(node.matrix.data());
    } else {
        glm::vec3 translation(0.0f);
        if (node.translation.size() == 3) {
            translation = glm::vec3(
                static_cast<float>(node.translation[0]),
                static_cast<float>(node.translation[1]),
                static_cast<float>(node.translation[2]));
        }

        glm::quat rotation(1.0f, 0.0f, 0.0f, 0.0f);
        if (node.rotation.size() == 4) {
            rotation = glm::quat(
                static_cast<float>(node.rotation[3]),
                static_cast<float>(node.rotation[0]),
                static_cast<float>(node.rotation[1]),
                static_cast<float>(node.rotation[2]));
        }

        glm::vec3 scale(1.0f);
        if (node.scale.size() == 3) {
            scale = glm::vec3(
                static_cast<float>(node.scale[0]),
                static_cast<float>(node.scale[1]),
                static_cast<float>(node.scale[2]));
        }

        matrix = glm::translate(glm::mat4(1.0f), translation) *
                 glm::mat4_cast(rotation) *
                 glm::scale(glm::mat4(1.0f), scale);
    }
    return matrix;
}
