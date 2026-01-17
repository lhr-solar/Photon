#pragma once

#include <string>
#include <vector>
#include <map>
#include <atomic>
#include <glm/glm.hpp>
#include "../gpu/vulkanGLTF.hpp"
#include "chunking.hpp"

class VulkanDevice;

struct OSMConfig
{
    double centerLat = 30.2672; // Austin
    double centerLon = -97.7431;
    double radiusMeters = 1000.0;
    bool autoFetch = false;
    bool useElevation = true;
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

    // Write per-chunk OSM mesh data to disk for streaming.
    // - outDir: directory for chunk files
    // - originMercatorMeters: global EPSG:3857 meters for the current OSM center
    // - chunkSizeMeters: chunk edge length in meters
    bool writeChunksToDisk(const std::string& outDir, glm::dvec2 originMercatorMeters, double chunkSizeMeters);

    void clear();
    void cancel() { cancelRequest = true; }
    void resetCancel() { cancelRequest = false; }

    std::string getStatus() const { return statusMessage; }
    bool isLoading() const { return loading; }

    // Global projection (EPSG:3857, meters). x=east, y=north.
    static glm::dvec2 latLonToMercatorMeters(double lat, double lon);

    // Inverse Web Mercator (EPSG:3857 meters -> lat/lon degrees).
    static glm::dvec2 mercatorMetersToLatLon(double x, double y);

    // Fetch a single chunk's OSM data by its global chunk id and write its mesh file.
    // - originLat/Lon define the local world origin used by this client (car/camera local coords)
    // - chunkId is global (EPSG:3857 meters / chunkSize)
    bool fetchChunkToDisk(const std::string& outDir, double originLat, double originLon, ChunkId chunkId, double chunkSizeMeters);

    // Sample elevation (meters) at a single lat/lon using the existing OpenTopography cache.
    // Returns true even if elevation isn't available; outMeters will be 0 in that case.
    bool getElevationAt(double lat, double lon, float& outMeters);
private:
    bool downloadOSM(double lat, double lon, double radius, std::string &outXML);
    bool downloadOSMBBox(double minLat, double minLon, double maxLat, double maxLon, const std::string& cacheKey, std::string &outJSON);
    bool fetchElevations(const std::vector<std::pair<double, double>> &locations, std::vector<float> &outElevations);
    bool parseAndBuild(const std::string &osmXML, double centerLat, double centerLon, bool useElevation);
    glm::vec2 latLonToMeters(double lat, double lon, double centerLat, double centerLon);
    void buildBuildings(const std::map<int64_t, glm::vec3> &nodes,
                        const std::vector<std::pair<int64_t, std::vector<int64_t>>> &ways,
                        const std::map<int64_t, float> &buildingHeights);
    void buildRoads(const std::map<int64_t, glm::vec3> &nodes,
                    const std::vector<std::vector<int64_t>> &ways);
    void buildParks(const std::map<int64_t, glm::vec3> &nodes,
                    const std::vector<std::vector<int64_t>> &ways);
    void buildWater(const std::map<int64_t, glm::vec3> &nodes,
                    const std::vector<std::vector<int64_t>> &ways);
    void buildGroundPlane();

    void extrudePolygon(const std::vector<glm::vec3> &outline, float height,
                        glm::vec4 color, const std::string &name);
    void bufferLine(const std::vector<glm::vec3> &line, float width,
                    glm::vec4 color, const std::string &name);

    std::vector<uint32_t> triangulatePolygon(const std::vector<glm::vec3> &outline);
    std::vector<OSMGeometry> geometries;
    std::string statusMessage;
    std::string OSM_KEY;
    bool loading = false;
    std::atomic<bool> cancelRequest{false};
};
