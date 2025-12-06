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
#include <thread>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <locale>
#include <cstdlib>
#include "secrets.h"

#include "../../.external/tinygltf/json.hpp"

namespace fs = std::filesystem;

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
    if (cancelRequest) return false;

    // Check cache
    if (!fs::exists("cache")) {
        fs::create_directory("cache");
    }

    std::ostringstream cacheName;
    cacheName << "cache/osm_" << std::fixed << std::setprecision(4) << lat << "_" << lon << "_" << static_cast<int>(radius) << ".json";
    std::string cachePath = cacheName.str();

    if (fs::exists(cachePath)) {
        logs("[OSM] Loading from cache: " << cachePath);
        std::ifstream t(cachePath);
        std::stringstream buffer;
        buffer << t.rdbuf();
        outXML = buffer.str();
        if (!outXML.empty()) {
            return true;
        }
        logs("[!] Cache file empty or invalid, re-downloading...");
    }

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
    httplib::SSLClient cli("https://overpass-api.de");
    logs("[OSM] Using HTTPS (OpenSSL available)");
#else
    httplib::Client cli("http://overpass-api.de");
    logs("[OSM] Using HTTP (OpenSSL not available)");
#endif
    cli.set_connection_timeout(60);
    cli.set_read_timeout(300);

    double latDelta = radius / 111000.0;
    double lonDelta = radius / (111000.0 * cos(glm::radians(lat)));

    double minLat = lat - latDelta;
    double maxLat = lat + latDelta;
    double minLon = lon - lonDelta;
    double maxLon = lon + lonDelta;

    std::ostringstream query;
    query << "[out:json][timeout:300];\n"
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

    outXML.clear();
    auto res = cli.Get("/api/interpreter", form, headers, 
        [&](const char *data, size_t data_length) {
            if (cancelRequest) return false;
            outXML.append(data, data_length);
            return true;
        }
    );

    if (!res)
    {
        if (cancelRequest) {
             statusMessage = "Cancelled";
             logs("[OSM] Download cancelled");
             return false;
        }
        statusMessage = "HTTP error: connection failed";
        logs("[!] OSM download failed: " << statusMessage);
        return false;
    }

    if (res->status != 200)
    {
        statusMessage = "HTTP error: " + std::to_string(res->status);
        std::string body_preview = outXML.size() > 300 ? outXML.substr(0, 300) + "..." : outXML;
        logs("[!] OSM download failed: " << statusMessage << "\n[OSM] Response: " << body_preview);
        return false;
    }

    // Cache the result
    if (!outXML.empty()) {
        std::ofstream out(cachePath);
        out << outXML;
        out.close();
        logs("[OSM] Cached to: " << cachePath);
    }

    logs("[+] OSM data downloaded: " << outXML.size() << " bytes");
    return true;
}

bool OSMLoader::fetchElevations(const std::vector<std::pair<double, double>> &locations, std::vector<float> &outElevations)
{
    if (locations.empty()) return true;
    outElevations.resize(locations.size(), 0.0f);

    // 1. Calculate Bounding Box
    double minLat = 1000.0, maxLat = -1000.0;
    double minLon = 1000.0, maxLon = -1000.0;
    for(const auto& loc : locations) {
        if(loc.first < minLat) minLat = loc.first;
        if(loc.first > maxLat) maxLat = loc.first;
        if(loc.second < minLon) minLon = loc.second;
        if(loc.second > maxLon) maxLon = loc.second;
    }
    
    // Expand slightly to ensure coverage and valid query
    minLat -= 0.005; maxLat += 0.005;
    minLon -= 0.005; maxLon += 0.005;

    if (!fs::exists("cache")) {
        fs::create_directory("cache");
    }

    // 2. Check Cache (AAIGrid file)
    std::ostringstream cacheName;
    // Include API type in cache name to avoid conflicts if switching back
    cacheName << "cache/elev_ot_" << std::fixed << std::setprecision(5) 
              << minLat << "_" << minLon << "_" << maxLat << "_" << maxLon << ".asc";
    std::string cachePath = cacheName.str();
    
    std::string gridData;
    bool loadedFromCache = false;
    
    if (fs::exists(cachePath)) {
        logs("[OSM] Loading elevation raster from cache: " << cachePath);
        std::ifstream t(cachePath);
        std::stringstream buffer;
        buffer << t.rdbuf();
        gridData = buffer.str();
        if (!gridData.empty()) {
            loadedFromCache = true;
        }
    }

    // 3. Fetch from OpenTopography if not cached
    if (!loadedFromCache) {
        logs("[OSM] Fetching elevation raster from OpenTopography...");
        statusMessage = "Fetching elevation raster...";

        // API Key provided by user
        std::string apiKey = OPENTOPOLOGY_API_KEY;
        
        #ifdef CPPHTTPLIB_OPENSSL_SUPPORT
            // httplib::SSLClient constructor expects host, not URL scheme
            httplib::SSLClient cli("portal.opentopography.org");
            
            cli.set_connection_timeout(30);
            cli.set_read_timeout(120); // Raster generation can take time

            // Construct Query
            std::stringstream pathSs;
            pathSs.imbue(std::locale::classic());
            pathSs << "/API/globaldem?demtype=SRTMGL3" 
                   << "&south=" << minLat 
                   << "&north=" << maxLat 
                   << "&west=" << minLon 
                   << "&east=" << maxLon 
                   << "&outputFormat=AAIGrid" 
                   << "&API_Key=" << apiKey;
            
            std::string path = pathSs.str();
            
            logs("[OSM] GET " << path); // Debug log
            
            auto res = cli.Get(path.c_str());
            
            if (res && res->status == 200) {
                gridData = res->body;
                
                if (gridData.find("ncols") != std::string::npos) {
                    std::ofstream out(cachePath);
                    out << gridData;
                    out.close();
                    logs("[OSM] Cached raster to " << cachePath);
                    loadedFromCache = true;
                } else {
                    logs("[!] OpenTopography returned unexpected data: " << gridData.substr(0, 200));
                }
            } else {
                 logs("[!] OpenTopography request failed: " << (res ? std::to_string(res->status) : "Connection Failed"));
                 if (res) {
                     logs("[!] Response: " << res->body);
                 }
                 return true; 
            }
        #else
            // Fallback to system curl for HTTPS support since OpenSSL is missing
            logs("[OSM] OpenSSL not linked. Falling back to system curl for HTTPS request.");
            
            std::ostringstream urlSs;
            urlSs.imbue(std::locale::classic());
            urlSs << "https://portal.opentopography.org/API/globaldem?demtype=SRTMGL3";
            urlSs << "&south=" << minLat;
            urlSs << "&north=" << maxLat;
            urlSs << "&west=" << minLon;
            urlSs << "&east=" << maxLon;
            urlSs << "&outputFormat=AAIGrid";
            urlSs << "&API_Key=" << apiKey;
            
            std::string url = urlSs.str();
            std::string cmd = "curl -s -o \"" + cachePath + "\" \"" + url + "\"";
            
            logs("[OSM] Executing: " << cmd);
            int ret = system(cmd.c_str());
            
            if (ret == 0 && fs::exists(cachePath) && fs::file_size(cachePath) > 0) {
                 std::ifstream t(cachePath);
                 std::stringstream buffer;
                 buffer << t.rdbuf();
                 gridData = buffer.str();
                 
                 if (gridData.find("ncols") != std::string::npos) {
                     loadedFromCache = true;
                     logs("[OSM] Downloaded and cached raster via curl.");
                 } else {
                     logs("[!] Downloaded data appears invalid (not AAIGrid).");
                     logs("[!] Content preview: " << gridData.substr(0, 200));
                     // Remove invalid file
                     t.close();
                     fs::remove(cachePath);
                 }
            } else {
                logs("[!] Curl failed or file empty. Return code: " << ret);
            }
            
            if (!loadedFromCache) return true; // Continue without elevation
        #endif
    }

    if (!loadedFromCache) {
        return false; // Failed to load or download
    }

    // 4. Parse AAIGrid
    // Format:
    // ncols        123
    // nrows        123
    // xllcorner    -123.456
    // yllcorner    12.345
    // cellsize     0.000833
    // NODATA_value -9999
    // ... data ...
    
    std::stringstream ss(gridData);
    std::string key;
    int ncols = 0, nrows = 0;
    double xllcorner = 0.0, yllcorner = 0.0, cellsize = 0.0;
    float nodata = -9999.0f;
    
    // Parse header
    for(int i=0; i<6; ++i) {
        ss >> key;
        std::transform(key.begin(), key.end(), key.begin(), ::tolower);
        if (key == "ncols") ss >> ncols;
        else if (key == "nrows") ss >> nrows;
        else if (key == "xllcorner") ss >> xllcorner;
        else if (key == "yllcorner") ss >> yllcorner;
        else if (key == "cellsize") ss >> cellsize;
        else if (key == "nodata_value") ss >> nodata;
    }

    if (ncols <= 0 || nrows <= 0 || cellsize <= 0.0) {
        logs("[!] Invalid AAIGrid header.");
        return false;
    }

    std::vector<float> rasterData;
    rasterData.reserve(ncols * nrows);
    float val;
    while(ss >> val) {
        rasterData.push_back(val);
    }
    
    if (rasterData.size() != static_cast<size_t>(ncols * nrows)) {
        logs("[!] AAIGrid data size mismatch. Expected " << (ncols*nrows) << ", got " << rasterData.size());
    }

    // 5. Interpolate for each location
    for (size_t i = 0; i < locations.size(); ++i) {
        double lat = locations[i].first;
        double lon = locations[i].second;

        // AAIGrid starts from Top-Left usually? NO.
        // AAIGrid standard:
        // Data starts at top-left (North-West).
        // yllcorner is the Bottom-Left latitude (South).
        // xllcorner is the Left longitude (West).
        
        // Map lat/lon to grid coordinates (col, row)
        // col = (lon - xllcorner) / cellsize
        // row = (lat - yllcorner) / cellsize -- WAIT.
        // Standard AAIGrid stores data row by row from Top to Bottom.
        // So row 0 is the Northernmost row (yllcorner + nrows*cellsize).
        
        double colF = (lon - xllcorner) / cellsize;
        double rowF = (lat - yllcorner) / cellsize; 
        
        // Invert row because data is stored Top-to-Bottom
        // Top latitude = yllcorner + nrows * cellsize
        // Row index = (TopLat - lat) / cellsize
        //           = (yllcorner + nrows*cellsize - lat) / cellsize
        //           = nrows - (lat - yllcorner)/cellsize
        
        // Let's verify.
        // If lat = yllcorner (bottom), (lat - yllcorner)/cellsize = 0.
        // nrows - 0 = nrows. This is out of bounds.
        // Correct: Row 0 corresponds to Y = yllcorner + (nrows-1)*cellsize? No.
        // Usually row 0 is Y = yllcorner + nrows*cellsize (top edge).
        // Let's stick to standard:
        // row_index = nrows - 1 - floor((lat - yllcorner) / cellsize)
        
        double gridX = colF;
        double gridY = (double)nrows - 1.0 - rowF; // Inverted Y for array indexing
        
        // But for interpolation we need fractional parts.
        // Let's define u, v in array space (0..ncols-1, 0..nrows-1)
        // where (0,0) is Top-Left of the array.
        
        double u = colF;
        double v = (double)nrows - rowF; // Top is 0, Bottom is nrows
        
        // Adjust v range to 0..nrows-1
        // If lat is max (top), rowF is approx nrows. v is 0.
        // If lat is min (bottom), rowF is 0. v is nrows.
        
        // Clamp
        if (u < 0) u = 0;
        if (u > ncols - 1.001) u = ncols - 1.001;
        if (v < 0) v = 0;
        if (v > nrows - 1.001) v = nrows - 1.001;
        
        int c0 = static_cast<int>(floor(u));
        int r0 = static_cast<int>(floor(v));
        int c1 = c0 + 1;
        int r1 = r0 + 1;
        
        if (c1 >= ncols) c1 = ncols - 1;
        if (r1 >= nrows) r1 = nrows - 1;
        
        double tx = u - c0;
        double ty = v - r0;
        
        // Indices in rasterData
        int i00 = r0 * ncols + c0;
        int i10 = r0 * ncols + c1;
        int i01 = r1 * ncols + c0;
        int i11 = r1 * ncols + c1;
        
        // Handle nodata
        auto getVal = [&](int idx) {
            float val = rasterData[idx];
            return (val == nodata) ? 0.0f : val;
        };
        
        float h00 = getVal(i00);
        float h10 = getVal(i10);
        float h01 = getVal(i01);
        float h11 = getVal(i11);
        
        // Bilinear
        float hTop = h00 * (1.0f - (float)tx) + h10 * (float)tx;
        float hBot = h01 * (1.0f - (float)tx) + h11 * (float)tx;
        
        float h = hTop * (1.0f - (float)ty) + hBot * (float)ty;
        
        outElevations[i] = h;
    }

    float minEl = 10000.0f, maxEl = -10000.0f;
    for(float e : outElevations) {
        if(e < minEl) minEl = e;
        if(e > maxEl) maxEl = e;
    }
    logs("[+] Elevation complete via OpenTopography (Range: " << minEl << "m to " << maxEl << "m)");
    return true;
}

bool OSMLoader::parseAndBuild(const std::string &osmXML, double centerLat, double centerLon, bool useElevation)
{
    try
    {
        using nlohmann::json;
        json j = json::parse(osmXML);

        std::map<int64_t, glm::vec3> nodes;
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
        std::vector<std::pair<double, double>> nodeCoords;
        std::vector<int64_t> nodeIds;

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
                glm::vec2 m = latLonToMeters(lat, lon, centerLat, centerLon);
                nodes[id] = glm::vec3(m.x, 0.0f, m.y);

                if (useElevation)
                {
                    nodeCoords.push_back({lat, lon});
                    nodeIds.push_back(id);
                }
            }
        }

        if (useElevation && !nodeCoords.empty())
        {
            std::vector<float> elevations;
            fetchElevations(nodeCoords, elevations);

            if (cancelRequest)
            {
                logs("[OSM] Cancelled during elevation fetch");
                return false;
            }

            for (size_t i = 0; i < nodeIds.size(); ++i)
            {
                nodes[nodeIds[i]].y = elevations[i];
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

void OSMLoader::buildBuildings(const std::map<int64_t, glm::vec3> &nodes,
                               const std::vector<std::pair<int64_t, std::vector<int64_t>>> &ways,
                               const std::map<int64_t, float> &buildingHeights)
{
    for (size_t i = 0; i < ways.size(); ++i)
    {
        int64_t wayId = ways[i].first;
        const auto &way = ways[i].second;
        if (way.size() < 3)
            continue;

        std::vector<glm::vec3> outline;
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

void OSMLoader::buildRoads(const std::map<int64_t, glm::vec3> &nodes,
                           const std::vector<std::vector<int64_t>> &ways)
{
    for (const auto &way : ways)
    {
        if (way.size() < 2)
            continue;

        std::vector<glm::vec3> line;
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

void OSMLoader::buildParks(const std::map<int64_t, glm::vec3> &nodes,
                           const std::vector<std::vector<int64_t>> &ways)
{
    for (const auto &way : ways)
    {
        if (way.size() < 3)
            continue;

        std::vector<glm::vec3> outline;
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

void OSMLoader::buildWater(const std::map<int64_t, glm::vec3> &nodes,
                           const std::vector<std::vector<int64_t>> &ways)
{
    for (const auto &way : ways)
    {
        if (way.size() < 3)
            continue;

        std::vector<glm::vec3> outline;
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

void OSMLoader::extrudePolygon(const std::vector<glm::vec3> &outline, float height,
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
        // p.y is elevation, p.z is z-coordinate
        v.pos = glm::vec3(p.x, p.y, p.z);
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
        // p.y is elevation, p.z is z-coordinate
        v.pos = glm::vec3(p.x, p.y + height, p.z);
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

void OSMLoader::bufferLine(const std::vector<glm::vec3> &line, float width,
                           glm::vec4 color, const std::string &name)
{
    if (line.size() < 2)
        return;

    OSMGeometry geom;
    geom.color = color;
    geom.name = name;

    float halfWidth = width * 0.5f;

    float heightOffset = (name == "road_crown") ? 0.02f : 0.0f; // offset road pavements

    for (size_t i = 0; i < line.size(); ++i)
    {
        glm::vec3 p = line[i];
        glm::vec3 dir(0.0f);

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

        // Project direction to XZ plane for width calculation
        glm::vec2 dir2D = glm::normalize(glm::vec2(dir.x, dir.z));
        glm::vec2 perp(-dir2D.y, dir2D.x);
        
        glm::vec3 left = p;
        left.x += perp.x * halfWidth;
        left.z += perp.y * halfWidth;
        
        glm::vec3 right = p;
        right.x -= perp.x * halfWidth;
        right.z -= perp.y * halfWidth;

        vertex vl, vr;
        vl.pos = glm::vec3(left.x, left.y + heightOffset, left.z);
        vl.normal = glm::vec3(0.0f, 1.0f, 0.0f);
        vl.uv = glm::vec2(0.0f);
        vl.color = color;

        vr.pos = glm::vec3(right.x, right.y + heightOffset, right.z);
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

std::vector<uint32_t> OSMLoader::triangulatePolygon(const std::vector<glm::vec3> &outline)
{
    std::vector<uint32_t> indices;
    if (outline.size() < 3)
        return indices;
    using Point = std::array<double, 2>;
    std::vector<std::vector<Point>> polygon;
    std::vector<Point> ring;

    for (const auto &p : outline)
    {
        // Project to XZ plane for triangulation
        ring.push_back({static_cast<double>(p.x), static_cast<double>(p.z)});
    }

    polygon.push_back(ring);

    indices = mapbox::earcut<uint32_t>(polygon);

    return indices;
}

bool OSMLoader::fetchAndBuild(const OSMConfig &config)
{
    loading = true;
    // cancelRequest = false; // Handled by caller
    statusMessage = "Fetching OSM data...";
    clear();

    std::string osmXML;
    if (!downloadOSM(config.centerLat, config.centerLon, config.radiusMeters, osmXML))
    {
        loading = false;
        return false;
    }

    if (cancelRequest)
    {
        statusMessage = "Cancelled";
        loading = false;
        return false;
    }

    statusMessage = "Parsing and building geometry...";
    if (!parseAndBuild(osmXML, config.centerLat, config.centerLon, config.useElevation))
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
