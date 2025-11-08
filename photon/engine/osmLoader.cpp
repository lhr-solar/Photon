#ifdef WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include "osmLoader.hpp"
#include "include.hpp"
#include "../gpu/vulkanDevice.hpp"
#include "../gpu/vulkanBuffer.hpp"

#include <httplib.h>
#include <earcut.hpp>
#include <sstream>
#include <fstream>
#include <cmath>
#include <algorithm>

#include "../../.external/tinygltf/json.hpp"

OSMLoader::OSMLoader()
{
    statusMessage = "Ready";
}

OSMLoader::~OSMLoader()
{
    clear();
}

void OSMLoader::clear()
{
    geometries.clear();
    statusMessage = "Cleared";
}

glm::vec2 OSMLoader::latLonToMeters(double lat, double lon, double centerLat, double centerLon)
{
    const double R = 6371000.0; // Earth radius in meters
    double latRad = glm::radians(lat);
    double centerLatRad = glm::radians(centerLat);

    double x = glm::radians(lon - centerLon) * R * cos(centerLatRad);
    double y = glm::radians(lat - centerLat) * R;

    return glm::vec2(static_cast<float>(x), static_cast<float>(y));
}

bool OSMLoader::downloadOSM(double lat, double lon, double radius, std::string &outXML)
{
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
    httplib::SSLClient cli("https://overpass-api.de");
    logs("[OSM] Using HTTPS (OpenSSL available)");
#else
    httplib::Client cli("http://overpass-api.de");
    logs("[OSM] Using HTTP (OpenSSL not available)");
#endif
    cli.set_connection_timeout(30);
    cli.set_read_timeout(60);

    double latDelta = radius / 111000.0;
    double lonDelta = radius / (111000.0 * cos(glm::radians(lat)));

    double minLat = lat - latDelta;
    double maxLat = lat + latDelta;
    double minLon = lon - lonDelta;
    double maxLon = lon + lonDelta;

    std::ostringstream query;
    query << "[out:json][timeout:60];\n"
          << "(\n"
          << "  node(" << minLat << "," << minLon << "," << maxLat << "," << maxLon << ");\n"
          << "  way(" << minLat << "," << minLon << "," << maxLat << "," << maxLon << ");\n"
          << ");\n"
          << "out body;\n"
          << ">;\n"
          << "out skel qt;";

    std::string queryStr = query.str();

    httplib::Headers headers = {
        {"Accept", "application/json"}};
    httplib::Params form;
    form.emplace("data", queryStr);

    auto res = cli.Post("/api/interpreter", headers, form);

    if (!res)
    {
        statusMessage = "HTTP error: connection failed";
        logs("[!] OSM download failed: " << statusMessage);
        return false;
    }

    if (res->status != 200)
    {
        statusMessage = "HTTP error: " + std::to_string(res->status);
        std::string body_preview = res->body.size() > 300 ? res->body.substr(0, 300) + "..." : res->body;
        logs("[!] OSM download failed: " << statusMessage << "\n[OSM] Response: " << body_preview);
        return false;
    }

    outXML = res->body;
    logs("[+] OSM data downloaded: " << outXML.size() << " bytes");
    return true;
}

bool OSMLoader::parseAndBuild(const std::string &osmXML, double centerLat, double centerLon)
{
    try
    {
        using nlohmann::json;
        json j = json::parse(osmXML);

        std::map<int64_t, glm::vec2> nodes;
        std::vector<std::pair<int64_t, std::vector<int64_t>>> buildingWays; // map wayid, nodelist
        std::vector<std::vector<int64_t>> roadWays;
        std::vector<std::vector<int64_t>> parkWays;
        std::vector<std::vector<int64_t>> waterWays;
        std::map<int64_t, float> buildingHeights;

        if (j.find("elements") == j.end() || !j["elements"].is_array())
        {
            statusMessage = "Invalid Overpass JSON: no elements";
            logs("[!] Invalid Overpass JSON: no elements");
            return false;
        }

        // find nodes
        for (const auto &el : j["elements"])
        {
            if (el.find("type") == el.end() || !el["type"].is_string())
                continue;
            if (el["type"] == "node")
            {
                if (el.find("id") == el.end() || el.find("lat") == el.end() || el.find("lon") == el.end())
                    continue;
                int64_t id = el["id"].get<int64_t>();
                double lat = el["lat"].get<double>();
                double lon = el["lon"].get<double>();
                nodes[id] = latLonToMeters(lat, lon, centerLat, centerLon);
            }
        }

        // get ways and types
        for (const auto &el : j["elements"])
        {
            if (el.find("type") == el.end() || !el["type"].is_string())
                continue;
            if (el["type"] != "way")
                continue;

            if (el.find("id") == el.end() || el.find("nodes") == el.end() || !el["nodes"].is_array())
                continue;
            int64_t wayId = el["id"].get<int64_t>();

            std::vector<int64_t> nodeRefs;
            nodeRefs.reserve(el["nodes"].size());
            for (const auto &nid : el["nodes"])
            {
                nodeRefs.push_back(nid.get<int64_t>());
            }

            bool isBuilding = false;
            bool isRoad = false;
            bool isPark = false;
            bool isWater = false;
            bool isParking = false;
            bool isFootway = false;
            float height = 12.0f;

            if (el.find("tags") != el.end() && el["tags"].is_object())
            {
                const auto &tags = el["tags"];
                auto has = [&](const char *k)
                {
                    auto it = tags.find(k);
                    return it != tags.end() && it->is_string();
                };
                auto val = [&](const char *k) -> std::string
                {
                    if (!has(k))
                        return std::string();
                    return tags[k].get<std::string>();
                };

                if (has("building"))
                {
                    isBuilding = true;
                }
                if (has("height"))
                {
                    try
                    {
                        height = std::stof(val("height"));
                    }
                    catch (...)
                    {
                    }
                }
                if (has("building:levels"))
                {
                    try
                    {
                        height = std::stof(val("building:levels")) * 3.2f;
                    }
                    catch (...)
                    {
                    }
                }
                if (has("highway"))
                {
                    std::string v = val("highway");
                    if (v == "footway" || v == "path" || v == "steps")
                    {
                        isFootway = true;
                    }
                    else
                    {
                        isRoad = true;
                    }
                }
                if (has("amenity") && val("amenity") == "parking")
                {
                    isParking = true;
                }
                if (has("leisure") && val("leisure") == "park")
                {
                    isPark = true;
                }
                if (has("landuse"))
                {
                    std::string v = val("landuse");
                    if (v == "grass" || v == "recreation_ground")
                        isPark = true;
                }
                if (has("natural"))
                {
                    std::string v = val("natural");
                    if (v == "water")
                        isWater = true;
                    else if (v == "wood")
                        isPark = true;
                }
            }

            if (isBuilding)
            {
                buildingWays.push_back({wayId, nodeRefs});
                buildingHeights[wayId] = std::min(height, 80.0f);
            }
            else if (isRoad)
            {
                roadWays.push_back(nodeRefs);
            }
            else if (isParking)
            {
                roadWays.push_back(nodeRefs);
            }
            else if (isFootway)
            {
                roadWays.push_back(nodeRefs);
            }
            else if (isPark)
            {
                parkWays.push_back(nodeRefs);
            }
            else if (isWater)
            {
                waterWays.push_back(nodeRefs);
            }
        }

        logs("[+] OSM parsed (JSON): " << nodes.size() << " nodes, "
                                       << buildingWays.size() << " buildings, "
                                       << roadWays.size() << " roads, "
                                       << parkWays.size() << " parks, "
                                       << waterWays.size() << " water");

        buildBuildings(nodes, buildingWays, buildingHeights);
        buildRoads(nodes, roadWays);
        buildParks(nodes, parkWays);
        buildWater(nodes, waterWays);
        buildGroundPlane();

        statusMessage = "OSM data loaded: " + std::to_string(geometries.size()) + " geometries";
        return true;
    }
    catch (const std::exception &e)
    {
        statusMessage = std::string("Parse error: ") + e.what();
        logs("[!] OSM parse error: " << e.what());
        return false;
    }
}

void OSMLoader::buildBuildings(const std::map<int64_t, glm::vec2> &nodes,
                               const std::vector<std::pair<int64_t, std::vector<int64_t>>> &ways,
                               const std::map<int64_t, float> &buildingHeights)
{
    for (size_t i = 0; i < ways.size(); ++i)
    {
        int64_t wayId = ways[i].first;
        const auto &way = ways[i].second;
        if (way.size() < 3)
            continue;

        std::vector<glm::vec2> outline;
        for (auto nodeId : way)
        {
            auto it = nodes.find(nodeId);
            if (it != nodes.end())
            {
                outline.push_back(it->second);
            }
        }

        if (outline.size() < 3)
            continue;

        if (glm::distance(outline.front(), outline.back()) > 0.1f)
        {
            outline.push_back(outline.front());
        }

        float height = 12.0f; // Default height

        // get actual height
        auto heightIt = buildingHeights.find(wayId);
        if (heightIt != buildingHeights.end())
        {
            height = heightIt->second;
        }

        // color based on height
        glm::vec4 color;
        if (height < 12.0f)
            color = glm::vec4(0.785f, 0.541f, 0.118f, 1.0f); // low
        else if (height < 24.0f)
            color = glm::vec4(0.718f, 0.447f, 0.055f, 1.0f); // mid
        else
            color = glm::vec4(0.596f, 0.349f, 0.039f, 1.0f); // high

        extrudePolygon(outline, height, color, "building");
    }
}

void OSMLoader::buildRoads(const std::map<int64_t, glm::vec2> &nodes,
                           const std::vector<std::vector<int64_t>> &ways)
{
    for (const auto &way : ways)
    {
        if (way.size() < 2)
            continue;

        std::vector<glm::vec2> line;
        for (auto nodeId : way)
        {
            auto it = nodes.find(nodeId);
            if (it != nodes.end())
            {
                line.push_back(it->second);
            }
        }

        if (line.size() < 2)
            continue;

        glm::vec4 roadColor(0.494f, 0.529f, 0.573f, 1.0f); // gray
        bufferLine(line, 6.0f, roadColor, "road");

        glm::vec4 crownColor(0.902f, 0.922f, 0.945f, 1.0f); // light gray
        bufferLine(line, 2.6f, crownColor, "road_crown");
    }
}

void OSMLoader::buildParks(const std::map<int64_t, glm::vec2> &nodes,
                           const std::vector<std::vector<int64_t>> &ways)
{
    for (const auto &way : ways)
    {
        if (way.size() < 3)
            continue;

        std::vector<glm::vec2> outline;
        for (auto nodeId : way)
        {
            auto it = nodes.find(nodeId);
            if (it != nodes.end())
            {
                outline.push_back(it->second);
            }
        }

        if (outline.size() < 3)
            continue;

        if (glm::distance(outline.front(), outline.back()) > 0.1f)
        {
            outline.push_back(outline.front());
        }

        glm::vec4 parkColor(0.4f, 0.651f, 0.118f, 1.0f); // green
        extrudePolygon(outline, 0.5f, parkColor, "park");
    }
}

void OSMLoader::buildWater(const std::map<int64_t, glm::vec2> &nodes,
                           const std::vector<std::vector<int64_t>> &ways)
{
    for (const auto &way : ways)
    {
        if (way.size() < 3)
            continue;

        std::vector<glm::vec2> outline;
        for (auto nodeId : way)
        {
            auto it = nodes.find(nodeId);
            if (it != nodes.end())
            {
                outline.push_back(it->second);
            }
        }

        if (outline.size() < 3)
            continue;

        if (glm::distance(outline.front(), outline.back()) > 0.1f)
        {
            outline.push_back(outline.front());
        }

        glm::vec4 waterColor(0.243f, 0.561f, 0.878f, 1.0f); // blue
        extrudePolygon(outline, 0.5f, waterColor, "water");
    }
}

void OSMLoader::buildGroundPlane()
{
    OSMGeometry geom;
    geom.name = "ground";
    geom.color = glm::vec4(0.7f, 0.7f, 0.7f, 1.0f);

    float size = 1000.0f;
    float groundLevel = -0.5f;
    vertex v0, v1, v2, v3;
    v0.pos = glm::vec3(-size, groundLevel, -size);
    v0.normal = glm::vec3(0.0f, -1.0f, 0.0f);
    v0.uv = glm::vec2(0.0f, 0.0f);
    v0.color = geom.color;

    v1.pos = glm::vec3(size, groundLevel, -size);
    v1.normal = glm::vec3(0.0f, -1.0f, 0.0f);
    v1.uv = glm::vec2(1.0f, 0.0f);
    v1.color = geom.color;

    v2.pos = glm::vec3(size, groundLevel, size);
    v2.normal = glm::vec3(0.0f, -1.0f, 0.0f);
    v2.uv = glm::vec2(1.0f, 1.0f);
    v2.color = geom.color;

    v3.pos = glm::vec3(-size, groundLevel, size);
    v3.normal = glm::vec3(0.0f, -1.0f, 0.0f);
    v3.uv = glm::vec2(0.0f, 1.0f);
    v3.color = geom.color;

    geom.vertices.push_back(v0);
    geom.vertices.push_back(v1);
    geom.vertices.push_back(v2);
    geom.vertices.push_back(v3);

    geom.indices.push_back(0);
    geom.indices.push_back(2);
    geom.indices.push_back(1);

    geom.indices.push_back(0);
    geom.indices.push_back(3);
    geom.indices.push_back(2);

    geometries.push_back(geom);
}

void OSMLoader::extrudePolygon(const std::vector<glm::vec2> &outline, float height,
                               glm::vec4 color, const std::string &name)
{
    if (outline.size() < 3)
        return;

    OSMGeometry geom;
    geom.color = color;
    geom.name = name;

    // Triangulate base
    std::vector<uint32_t> baseIndices = triangulatePolygon(outline);

    uint32_t baseOffset = 0;

    // Bottom vertices
    for (const auto &p : outline)
    {
        vertex v;
        v.pos = glm::vec3(p.x, 0.0f, p.y);
        v.normal = glm::vec3(0.0f, -1.0f, 0.0f);
        v.uv = glm::vec2(0.0f);
        v.color = color;
        geom.vertices.push_back(v);
    }

    // Bottom triangles
    for (auto idx : baseIndices)
    {
        geom.indices.push_back(baseOffset + idx);
    }

    uint32_t topOffset = static_cast<uint32_t>(geom.vertices.size());

    // Top vertices 
    for (const auto &p : outline)
    {
        vertex v;
        v.pos = glm::vec3(p.x, height, p.y);
        v.normal = glm::vec3(0.0f, 1.0f, 0.0f);
        v.uv = glm::vec2(0.0f);
        v.color = color;
        geom.vertices.push_back(v);
    }

    // Top triangles 
    for (int i = static_cast<int>(baseIndices.size()) - 1; i >= 0; --i)
    {
        geom.indices.push_back(topOffset + baseIndices[i]);
    }

    // Side walls
    size_t n = outline.size();
    for (size_t i = 0; i < n - 1; ++i)
    {
        uint32_t b0 = baseOffset + static_cast<uint32_t>(i);
        uint32_t b1 = baseOffset + static_cast<uint32_t>(i + 1);
        uint32_t t0 = topOffset + static_cast<uint32_t>(i);
        uint32_t t1 = topOffset + static_cast<uint32_t>(i + 1);

        // Two triangles per wall segment
        geom.indices.push_back(b0);
        geom.indices.push_back(t0);
        geom.indices.push_back(b1);

        geom.indices.push_back(b1);
        geom.indices.push_back(t0);
        geom.indices.push_back(t1);
    }

    geometries.push_back(geom);
}

void OSMLoader::bufferLine(const std::vector<glm::vec2> &line, float width,
                           glm::vec4 color, const std::string &name)
{
    if (line.size() < 2)
        return;

    OSMGeometry geom;
    geom.color = color;
    geom.name = name;

    float halfWidth = width * 0.5f;

    float height = (name == "road_crown") ? 1.02f : 1.0f; // offset road pavements

    for (size_t i = 0; i < line.size(); ++i)
    {
        glm::vec2 p = line[i];
        glm::vec2 dir(0.0f);

        if (i == 0)
        {
            dir = glm::normalize(line[i + 1] - line[i]);
        }
        else if (i == line.size() - 1)
        {
            dir = glm::normalize(line[i] - line[i - 1]);
        }
        else
        {
            dir = glm::normalize(line[i + 1] - line[i - 1]);
        }

        glm::vec2 perp(-dir.y, dir.x);
        glm::vec2 left = p + perp * halfWidth;
        glm::vec2 right = p - perp * halfWidth;

        vertex vl, vr;
        vl.pos = glm::vec3(left.x, height, left.y);
        vl.normal = glm::vec3(0.0f, 1.0f, 0.0f);
        vl.uv = glm::vec2(0.0f);
        vl.color = color;

        vr.pos = glm::vec3(right.x, height, right.y);
        vr.normal = glm::vec3(0.0f, 1.0f, 0.0f);
        vr.uv = glm::vec2(0.0f);
        vr.color = color;

        geom.vertices.push_back(vl);
        geom.vertices.push_back(vr);
    }

    // quads
    for (size_t i = 0; i < line.size() - 1; ++i)
    {
        uint32_t base = static_cast<uint32_t>(i * 2);
        uint32_t l0 = base;
        uint32_t r0 = base + 1;
        uint32_t l1 = base + 2;
        uint32_t r1 = base + 3;

        geom.indices.push_back(l0);
        geom.indices.push_back(l1);
        geom.indices.push_back(r0);

        geom.indices.push_back(r0);
        geom.indices.push_back(l1);
        geom.indices.push_back(r1);
    }

    geometries.push_back(geom);
}

std::vector<uint32_t> OSMLoader::triangulatePolygon(const std::vector<glm::vec2> &outline)
{
    std::vector<uint32_t> indices;
    if (outline.size() < 3)
        return indices;
    using Point = std::array<double, 2>;
    std::vector<std::vector<Point>> polygon;
    std::vector<Point> ring;

    for (const auto &p : outline)
    {
        ring.push_back({static_cast<double>(p.x), static_cast<double>(p.y)});
    }

    polygon.push_back(ring);

    indices = mapbox::earcut<uint32_t>(polygon);

    return indices;
}

bool OSMLoader::fetchAndBuild(const OSMConfig &config)
{
    loading = true;
    statusMessage = "Fetching OSM data...";
    clear();

    std::string osmXML;
    if (!downloadOSM(config.centerLat, config.centerLon, config.radiusMeters, osmXML))
    {
        loading = false;
        return false;
    }

    statusMessage = "Parsing and building geometry...";
    if (!parseAndBuild(osmXML, config.centerLat, config.centerLon))
    {
        loading = false;
        return false;
    }

    loading = false;
    logs("[+] OSM load complete: " << geometries.size() << " geometries");
    return true;
}

bool OSMLoader::uploadToGPU(VulkanDevice *device, std::vector<Model> &outModels)
{
    if (geometries.empty())
    {
        statusMessage = "No geometries to upload";
        return false;
    }

    logs("[+] Merging " << geometries.size() << " OSM geometries by category");

    // merge verticies for performance
    std::map<std::string, std::vector<vertex>> mergedVertices;
    std::map<std::string, std::vector<uint32_t>> mergedIndices;

    for (const auto &geom : geometries)
    {
        if (geom.vertices.empty() || geom.indices.empty())
            continue;

        auto &verts = mergedVertices[geom.name];
        auto &inds = mergedIndices[geom.name];

        uint32_t baseVertex = static_cast<uint32_t>(verts.size());

        verts.insert(verts.end(), geom.vertices.begin(), geom.vertices.end());

        for (uint32_t idx : geom.indices)
        {
            inds.push_back(baseVertex + idx);
        }
    }

    logs("[+] Merged into " << mergedVertices.size() << " categories, uploading to GPU");

    // Upload merged geometries
    for (auto &[name, verts] : mergedVertices)
    {
        auto &inds = mergedIndices[name];
        if (verts.empty() || inds.empty())
            continue;

        Model model;
        model.name = name;
        model.vertices = verts;
        model.indices = inds;

        Model::Mesh mesh;

        VkDeviceSize vertexBufferSize = sizeof(vertex) * verts.size();
        VkResult result = device->createBuffer(
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &mesh.vertexBuffer, vertexBufferSize, (void *)verts.data());

        if (result != VK_SUCCESS)
        {
            logs("[!] Failed to create vertex buffer for " << name);
            continue;
        }

        mesh.vertexCount = static_cast<uint32_t>(verts.size());

        VkDeviceSize indexBufferSize = sizeof(uint32_t) * inds.size();
        result = device->createBuffer(
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &mesh.indexBuffer, indexBufferSize, (void *)inds.data());

        if (result != VK_SUCCESS)
        {
            logs("[!] Failed to create index buffer for " << name);
            mesh.vertexBuffer.destroy();
            continue;
        }

        mesh.indexCount = static_cast<uint32_t>(inds.size());

        Primitive prim;
        prim.firstIndex = 0;
        prim.indexCount = mesh.indexCount;
        prim.materialIndex = -1;
        mesh.primitives.push_back(prim);

        model.meshes.push_back(mesh);
        outModels.push_back(model);
    }

    statusMessage = "Uploaded " + std::to_string(outModels.size()) + " merged OSM models to GPU";
    return true;
}
