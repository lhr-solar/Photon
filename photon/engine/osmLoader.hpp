#pragma once

#include <string>
#include <vector>
#include <map>
#include <glm/glm.hpp>
#include "../gpu/vulkanGLTF.hpp"

class VulkanDevice;

struct OSMConfig
{
    double centerLat = 30.2672; // Austin
    double centerLon = -97.7431;
    double radiusMeters = 1000.0;
    bool autoFetch = false;
};

struct OSMGeometry
{
    std::vector<vertex> vertices;
    std::vector<uint32_t> indices;
    glm::vec4 color;
    std::string name;
};

class OSMLoader
{
public:
    OSMLoader();
    ~OSMLoader();

    bool fetchAndBuild(const OSMConfig &config);

    const std::vector<OSMGeometry> &getGeometries() const { return geometries; }

    bool uploadToGPU(VulkanDevice *device, std::vector<Model> &outModels);

    void clear();

    std::string getStatus() const { return statusMessage; }
    bool isLoading() const { return loading; }

private:
    bool downloadOSM(double lat, double lon, double radius, std::string &outXML);
    bool parseAndBuild(const std::string &osmXML, double centerLat, double centerLon);
    glm::vec2 latLonToMeters(double lat, double lon, double centerLat, double centerLon);
    void buildBuildings(const std::map<int64_t, glm::vec2> &nodes,
                        const std::vector<std::pair<int64_t, std::vector<int64_t>>> &ways,
                        const std::map<int64_t, float> &buildingHeights);
    void buildRoads(const std::map<int64_t, glm::vec2> &nodes,
                    const std::vector<std::vector<int64_t>> &ways);
    void buildParks(const std::map<int64_t, glm::vec2> &nodes,
                    const std::vector<std::vector<int64_t>> &ways);
    void buildWater(const std::map<int64_t, glm::vec2> &nodes,
                    const std::vector<std::vector<int64_t>> &ways);
    void buildGroundPlane();

    void extrudePolygon(const std::vector<glm::vec2> &outline, float height,
                        glm::vec4 color, const std::string &name);
    void bufferLine(const std::vector<glm::vec2> &line, float width,
                    glm::vec4 color, const std::string &name);

    std::vector<uint32_t> triangulatePolygon(const std::vector<glm::vec2> &outline);
    std::vector<OSMGeometry> geometries;
    std::string statusMessage;
    bool loading = false;
};
