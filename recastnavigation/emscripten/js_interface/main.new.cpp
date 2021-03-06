#include <stdio.h>
#include <math.h>
#include <memory.h>

#include <string>
#include <random>

#include <Recast.h>
#include <InputGeom.h>
#include <DetourNavMesh.h>
#include <DetourCommon.h>
#include <DetourNavMeshQuery.h>
#include <DetourNavMeshBuilder.h>
#include <DetourCrowd.h>

#include <RecastDebugDraw.h>
#include <DetourDebugDraw.h>

#include <emscripten.h>

#include <SampleInterfaces.h>

static const int MAX_POLYS = 256;

extern "C" {
    extern void agentPool_clear();
    extern void agentPool_add(const int idx);
    extern void agentPool_get(const int idx,
                              const float pos_x,  const float pos_y, const float pos_z,
                              const float vel_x,  const float vel_y, const float vel_z,
                              const float radius, const int active,  const int state, const int neighbors);
    extern void flush_active_agents_callback();
}

////////////////////////////
// CUSTOM
//////////////
rcMeshLoaderObj* meshLoader;

////////////////////////////
// FROM SAMPLE
//////////////
InputGeom* m_geom;
dtNavMesh* m_navMesh;
dtNavMeshQuery* m_navQuery;
dtCrowd* m_crowd;

unsigned char m_navMeshDrawFlags;

bool m_keepInterResults = true;
float m_totalBuildTimeMs;

//// DEFAULTS
float m_agentHeight = 2.0f;  // , 5.0f, 0.1f);
float m_agentRadius = 0.5f;  // , 5.0f, 0.1f);

float m_cellSize = m_agentRadius / 2;
float m_cellHeight = m_cellSize / 2;

float m_agentMaxClimb = 4.0f;  // , 5.0f, 0.1f);
float m_agentMaxSlope = 30.0f;  // , 90.0f, 1.0f);

float m_regionMinSize = 1.0f;  // , 150.0f, 1.0f);
float m_regionMergeSize = 1.0f;  // , 150.0f, 1.0f);
bool m_monotonePartitioning = 0;

float m_edgeMaxLen = 50.0f;  // , 50.0f, 1.0f);
float m_edgeMaxError = 1.0f;  // , 3.0f, 0.1f);
float m_vertsPerPoly = 3.0f;  // , 12.0f, 1.0f);        

float m_detailSampleDist = 0.0f;  // , 16.0f, 1.0f);
float m_detailSampleMaxError = 8.0f;  // , 16.0f, 1.0f);

unsigned char* m_triareas;
rcHeightfield* m_solid;
rcCompactHeightfield* m_chf;
rcContourSet* m_cset;
rcPolyMesh* m_pmesh;
rcConfig m_cfg; 
rcPolyMeshDetail* m_dmesh;

/// Tool types.
enum SampleToolType
{
    TOOL_NONE = 0,
    TOOL_TILE_EDIT,
    TOOL_TILE_HIGHLIGHT,
    TOOL_TEMP_OBSTACLE,
    TOOL_NAVMESH_TESTER,
    TOOL_NAVMESH_PRUNE,
    TOOL_OFFMESH_CONNECTION,
    TOOL_CONVEX_VOLUME,
    TOOL_CROWD,
    MAX_TOOLS
};

/// These are just sample areas to use consistent values across the samples.
/// The use should specify these base on his needs.
enum SamplePolyAreas
{
    SAMPLE_POLYAREA_GROUND,
    SAMPLE_POLYAREA_WATER,
    SAMPLE_POLYAREA_ROAD,
    SAMPLE_POLYAREA_DOOR,
    SAMPLE_POLYAREA_GRASS,
    SAMPLE_POLYAREA_JUMP,
};
enum SamplePolyFlags
{
    SAMPLE_POLYFLAGS_WALK       = 0x01,     // Ability to walk (ground, grass, road)
    SAMPLE_POLYFLAGS_SWIM       = 0x02,     // Ability to swim (water).
    SAMPLE_POLYFLAGS_DOOR       = 0x04,     // Ability to move through doors.
    SAMPLE_POLYFLAGS_JUMP       = 0x08,     // Ability to jump.
    SAMPLE_POLYFLAGS_DISABLED   = 0x10,     // Disabled polygon
    SAMPLE_POLYFLAGS_ALL        = 0xffff    // All abilities.
};

enum DrawMode
{
    DRAWMODE_NAVMESH,
    DRAWMODE_NAVMESH_TRANS,
    DRAWMODE_NAVMESH_BVTREE,
    DRAWMODE_NAVMESH_NODES,
    DRAWMODE_NAVMESH_INVIS,
    DRAWMODE_MESH,
    DRAWMODE_VOXELS,
    DRAWMODE_VOXELS_WALKABLE,
    DRAWMODE_COMPACT,
    DRAWMODE_COMPACT_DISTANCE,
    DRAWMODE_COMPACT_REGIONS,
    DRAWMODE_REGION_CONNECTIONS,
    DRAWMODE_RAW_CONTOURS,
    DRAWMODE_BOTH_CONTOURS,
    DRAWMODE_CONTOURS,
    DRAWMODE_POLYMESH,
    DRAWMODE_POLYMESH_DETAIL,
    MAX_DRAWMODE
};

DrawMode m_drawMode;

BuildContext* m_ctx;

DebugDrawGL* dd;


////////////////////////////

extern "C" {
  extern char* getThreeJSMeshes();
}

/////////////////////////////////

void debugConfig()
{
    printf("config \n");
    printf(" m_cellSize=%f \n", m_cellSize);
    printf(" m_cellHeight=%f \n", m_cellHeight);

    //// DEFAULTS
    printf(" m_agentHeight=%f \n", m_agentHeight);
    printf(" m_agentRadius=%f \n", m_agentRadius);
    printf(" m_agentMaxClimb=%f \n", m_agentMaxClimb);
    printf(" m_agentMaxSlope=%f \n", m_agentMaxSlope);

    printf(" m_monotonePartitioning=%u\n", m_monotonePartitioning);

    printf(" m_regionMinSize=%f \n", m_regionMinSize);
    printf(" m_regionMergeSize=%f \n", m_regionMergeSize);

    printf(" m_edgeMaxLen=%f \n", m_edgeMaxLen);
    printf(" m_edgeMaxError=%f \n", m_edgeMaxError);
    printf(" m_vertsPerPoly=%f \n", m_vertsPerPoly);        

    printf(" m_detailSampleDist=%f \n", m_detailSampleDist);
    printf(" m_detailSampleMaxError=%f \n", m_detailSampleMaxError);
}

void debugDrawNavMesh(unsigned char flags) {
    duDebugDrawNavMesh(dd, *m_navMesh, flags);
}
void debugDrawNavMeshPortals() {
    duDebugDrawNavMeshPortals(dd, *m_navMesh);
}
void debugDrawRegionConnections() {
    duDebugDrawRegionConnections(dd, *m_cset, 0.5f);
}
void debugDrawRawContours() {
    duDebugDrawRawContours(dd, *m_cset, 0.5f);
}
void debugDrawContours() {
    duDebugDrawContours(dd, *m_cset, 0.5f);
}
void debugDrawHeightfieldSolid() {
    duDebugDrawHeightfieldSolid(dd, *m_solid);
}
void debugDrawHeightfieldWalkable() {
    duDebugDrawHeightfieldWalkable(dd, *m_solid);
}

////////////////////////////

void emscripten_log(char* string, bool escape = true)
{
    char buff[1024];
    sprintf(buff, (escape ? "console.log('%s');" : "console.log(%s);"), string);
    emscripten_run_script(buff);
    // free(buff);
}
void emscripten_debugger()
{
    emscripten_run_script("debugger");
}

////////////////////////////

void cleanup()
{
    printf("cleanup \n");
    
    delete [] m_triareas;
    m_triareas = 0;
    rcFreeHeightField(m_solid);
    m_solid = 0;
    rcFreeCompactHeightfield(m_chf);
    m_chf = 0;
    rcFreeContourSet(m_cset);
    m_cset = 0;
    rcFreePolyMesh(m_pmesh);
    m_pmesh = 0;
    rcFreePolyMeshDetail(m_dmesh);
    m_dmesh = 0;
    dtFreeNavMesh(m_navMesh);
    m_navMesh = 0;
    //dtNavMeshQuery(m_navQuery);
    m_navQuery = 0;
}

void bareGeomInit(){
    m_geom = new InputGeom;
    meshLoader = new rcMeshLoaderObj;
}
void bareGeomAddVertex(float x, float y, float z, int cap){
    meshLoader->addVertex(x, y, z, cap);
}
void bareGeomAddTriangle(int a, int b, int c, int cap){
    meshLoader->addTriangle(a, b, c, cap);
}
void bareGeomValidate(){
    m_geom->loadFromMeshLoader(m_ctx, meshLoader);
}
void getNavMeshVertices(std::string callback){
    const int nvp = m_pmesh->nvp;
    const float cs = m_pmesh->cs;
    const float ch = m_pmesh->ch;
    const float* orig = m_pmesh->bmin;

    char buff[512];

    sprintf(buff, "nvp=%u, cs=%f, ch=%f, orig={%f, %f, %f}", nvp, cs, ch, orig[0], orig[1], orig[2]);
    emscripten_log(buff);
    emscripten_run_script("__tmp_recastjs_data = [];");

    for (int i = 0; i < m_pmesh->npolys; ++i)
    {
        if (m_pmesh->areas[i] == SAMPLE_POLYAREA_GROUND)
        {
            const unsigned short* p = &m_pmesh->polys[i*nvp*2];

            unsigned short vi[3];
            for (int j = 2; j < nvp; ++j)
            {
                if (p[j] == RC_MESH_NULL_IDX) break;
                vi[0] = p[0];
                vi[1] = p[j-1];
                vi[2] = p[j];

                for (int k = 0; k < 3; ++k)
                {
                    const unsigned short* v = &m_pmesh->verts[vi[k]*3];
                    const float x = orig[0] + v[0]*cs;
                    const float y = orig[1] + (v[1]+1)*ch;
                    const float z = orig[2] + v[2]*cs;

                    sprintf(buff, "__tmp_recastjs_data.push({x:%f, y:%f, z:%f});", x, y, z);
                    emscripten_run_script(buff);
                }
            }
        }
    }

    sprintf(buff, "%s(__tmp_recastjs_data);", callback.c_str());
    emscripten_run_script(buff);

    // free(buff);
}

void getNavMeshTiles(std::string callback){
}

float randomf()
{
    return (static_cast <float> (rand()) / static_cast <float> (RAND_MAX));
}

void getRandomPoint(std::string callback)
{
    char buff[512];

    dtQueryFilter filter;
    filter.setIncludeFlags(3);
    filter.setExcludeFlags(0);

    dtPolyRef ref = 0;

    float randomPt[3];

    dtStatus findStatus = m_navQuery->findRandomPoint(&filter, randomf, &ref, randomPt);

    if (dtStatusFailed(findStatus)) {
        printf("Cannot find a random point: %u\n", findStatus);

        sprintf(buff, "%s(null);", callback.c_str());
        emscripten_run_script(buff);

    } else {

        sprintf(buff, "%s({x:%f, y:%f, z:%f});", callback.c_str(), randomPt[0], randomPt[1], randomPt[2]);
        emscripten_run_script(buff);
    }

    // free(buff);
}

void getNavHeightfieldRegions(std::string callback)
{
    if (! m_chf) {
        printf("CompactHeightfield (m_chf) is not available \n");
        return;
    }

    const float cs = m_chf->cs;
    const float ch = m_chf->ch;

    int height = m_chf->height;
    int width = m_chf->width;

    char buff[512];

    sprintf(buff, "cs=%f, ch=%f, height=%u, width=%u", cs, ch, height, width);
    emscripten_log(buff);

    emscripten_run_script("__tmp_recastjs_data = [];");

    for (int y = 0; y < m_chf->height; ++y)
    {
        printf("on y=%u \n", y);
        for (int x = 0; x < m_chf->width; ++x)
        {
            const float fx = m_chf->bmin[0] + x*cs;
            const float fz = m_chf->bmin[2] + y*cs;
            const rcCompactCell& c = m_chf->cells[x+y*m_chf->width];
            
            for (unsigned i = c.index, ni = c.index+c.count; i < ni; ++i)
            {
                const rcCompactSpan& s = m_chf->spans[i];
                const float fy = m_chf->bmin[1] + (s.y)*ch;
                unsigned int color;
                if (s.reg)
                    color = duIntToCol(s.reg, 192);
                else
                    color = duRGBA(0,0,0,64);

                sprintf(buff, "__tmp_recastjs_data.push({x:%f, y:%f, z:%f});", fx, fy, fz);      emscripten_run_script(buff);
                sprintf(buff, "__tmp_recastjs_data.push({x:%f, y:%f, z:%f});", fx, fy, fz+cs);   emscripten_run_script(buff);
                sprintf(buff, "__tmp_recastjs_data.push({x:%f, y:%f, z:%f});", fx+cs, fy, fz+cs);emscripten_run_script(buff);
                sprintf(buff, "__tmp_recastjs_data.push({x:%f, y:%f, z:%f});", fx+cs, fy, fz);   emscripten_run_script(buff);
            }
        }
    }
    
    sprintf(buff, "%s(__tmp_recastjs_data);", callback.c_str());
    emscripten_run_script(buff);

    // free(buff);
}

void findNearestPoly(float cx, float cy, float cz,
                    float ex, float ey, float ez,
                     /*const dtQueryFilter* filter,
                     dtPolyRef* nearestRef, float* nearestPt*/
                    std::string callback)
{
    emscripten_run_script("__tmp_recastjs_data = [];");
    char buff[512];

    const float p[3] = {cx,cy,cz};
    const float ext[3] = {ex,ey,ez};
    float nearestPt[3];

    dtQueryFilter filter;
    filter.setIncludeFlags(3);
    filter.setExcludeFlags(0);

    dtPolyRef ref = 0;

    dtStatus findStatus = m_navQuery->findNearestPoly(p, ext, &filter, &ref, 0);

    if (dtStatusFailed(findStatus)) {
        printf("Cannot find nearestPoly: %u\n", findStatus);

    } else {

        const dtMeshTile* tile;
        const dtPoly* poly;
        findStatus = m_navMesh->getTileAndPolyByRef(ref, &tile, &poly);

        if (dtStatusFailed(findStatus)) {
            printf("Cannot get tile and poly by ref #%u : %u \n", ref, findStatus);

        } else {
            // TODO: put poly and tile in __tmp_recastjs_data

            for (int i = 0; i < tile->header->vertCount; i++) {
                float* v = &tile->verts[i];

                sprintf(buff, "__tmp_recastjs_data.push({x:%f, y:%f, z:%f});", v[0], v[1], v[2]);
                emscripten_run_script(buff);
            }   
        }
    }

    sprintf(buff, "%s(__tmp_recastjs_data);", callback.c_str());
    emscripten_run_script(buff);

    // free(buff);
}

void findNearestPoint(float cx, float cy, float cz,
                    float ex, float ey, float ez,
                     /*const dtQueryFilter* filter,
                     dtPolyRef* nearestRef, float* nearestPt*/
                    std::string callback)
{
    emscripten_run_script("__tmp_recastjs_data = [];");
    char buff[512];

    const float p[3] = {cx,cy,cz};
    const float ext[3] = {ex,ey,ez};
    float nearestPt[3];

    dtQueryFilter filter;
    filter.setIncludeFlags(3);
    filter.setExcludeFlags(0);

    dtPolyRef ref = 0;
    float nearestPos[3];

    dtStatus findStatus = m_navQuery->findNearestPoly(p, ext, &filter, &ref, nearestPos);

    if (dtStatusFailed(findStatus)) {
        sprintf(buff, "%s(null);", callback.c_str());

    } else {

        sprintf(buff, "%s({ x:%f, y:%f, z:%f });", callback.c_str(), nearestPos[0], nearestPos[1], nearestPos[2]);
    }

    emscripten_run_script(buff);
}

void setPolyUnwalkable(float posX, float posY, float posZ, float extendX, float extendY, float extendZ, unsigned short flags)
{
    dtQueryFilter filter;
    filter.setIncludeFlags(3);
    filter.setExcludeFlags(0);

    const float ext[3] = {extendX, extendY, extendZ};
    float startPos[3] = { posX, posY, posZ };
    float nearestPos[3];
    dtPolyRef ref = 0;

    dtStatus findStatus;

    findStatus = m_navQuery->findNearestPoly(startPos, ext, &filter, &ref, nearestPos);

    if (dtStatusFailed(findStatus)) {
        printf("Cannot find a poly near: %f, %f, %f \n", posX, posY, posZ);

    } else {
        printf("Set poly %u as unwalkable \n", ref);
        m_navMesh->setPolyFlags(ref, flags);
    }
}


void findPath(float startPosX, float startPosY, float startPosZ,
                float endPosX, float endPosY, float endPosZ, int maxPath,
                std::string callback)
{
    emscripten_run_script("__tmp_recastjs_data = [];");
    char buff[512];

    float startPos[3] = { startPosX, startPosY, startPosZ };
    float endPos[3] = { endPosX, endPosY, endPosZ };

    const float ext[3] = {2,4,2};

    dtStatus findStatus;

    dtPolyRef path[maxPath+1];
    int pathCount;

    dtQueryFilter filter;
    filter.setIncludeFlags(3);
    filter.setExcludeFlags(0);

    // Change costs.
    // filter.setAreaCost(SAMPLE_POLYAREA_GROUND, 1.0f);
    // filter.setAreaCost(SAMPLE_POLYAREA_WATER, 10.0f);
    // filter.setAreaCost(SAMPLE_POLYAREA_ROAD, 1.0f);
    // filter.setAreaCost(SAMPLE_POLYAREA_DOOR, 1.0f);
    // filter.setAreaCost(SAMPLE_POLYAREA_GRASS, 2.0f);
    // filter.setAreaCost(SAMPLE_POLYAREA_JUMP, 1.5f);

    float nearestStartPos[3];
    dtPolyRef startRef = 0;
    m_navQuery->findNearestPoly(startPos, ext, &filter, &startRef, nearestStartPos);

    float nearestEndPos[3];
    dtPolyRef endRef = 0;
    m_navQuery->findNearestPoly(endPos, ext, &filter, &endRef, nearestEndPos);

    printf("Use %u , %u as start / end polyRefs \n", startRef, endRef);

    findStatus = m_navQuery->findPath(startRef, endRef, nearestStartPos, nearestEndPos, &filter, path, &pathCount, maxPath);

    if (dtStatusFailed(findStatus)) {
        printf("Cannot find a path: %u\n", findStatus);

    } else {
        printf("Found a %u polysteps path \n", pathCount);

        float straightPath[maxPath*3];
        unsigned char straightPathFlags[maxPath];
        dtPolyRef straightPathRefs[maxPath];
        int straightPathCount = 0;

        int maxStraightPath = maxPath;
        int options = 0;

        findStatus = m_navQuery->findStraightPath(nearestStartPos, nearestEndPos, path, pathCount, straightPath,
                                    straightPathFlags, straightPathRefs, &straightPathCount, maxStraightPath, options);

        if (dtStatusFailed(findStatus)) {
            printf("Cannot find a straight path: %u\n", findStatus);

        } else {
            printf("Found a %u steps path \n", straightPathCount);

            for (int i = 0; i < straightPathCount; ++i) {
                const float* v = &straightPath[i*3];

                // why ?
                if (!(fabs(v[0]) < 0.0000001f && fabs(v[1]) < 0.0000001f && fabs(v[2]) < 0.0000001f)) {
                    sprintf(buff, "__tmp_recastjs_data.push({x:%f, y:%f, z:%f});", v[0], v[1], v[2]);
                    emscripten_run_script(buff);
                } else {
                    sprintf(buff, "ignore %f, %f, %f", v[0], v[1], v[2]);
                    emscripten_log(buff);                   
                }
            }
        }
    }

    sprintf(buff, "%s(__tmp_recastjs_data);", callback.c_str());
    emscripten_run_script(buff);

    // free(buff);
}

void set_cellSize(float val){               m_cellSize = val;               }
void set_cellHeight(float val){             m_cellHeight = val;             }
void set_agentHeight(float val){            m_agentHeight = val;            }
void set_agentRadius(float val){            m_agentRadius = val;            }
void set_agentMaxClimb(float val){          m_agentMaxClimb = val;          }
void set_agentMaxSlope(float val){          m_agentMaxSlope = val;          }
void set_regionMinSize(float val){          m_regionMinSize = val;          }
void set_regionMergeSize(float val){        m_regionMergeSize = val;        }
void set_edgeMaxLen(float val){             m_edgeMaxLen = val;             }
void set_edgeMaxError(float val){           m_edgeMaxError = val;           }
void set_vertsPerPoly(float val){           m_vertsPerPoly = val;           }       
void set_detailSampleDist(float val){       m_detailSampleDist = val;       }
void set_detailSampleMaxError(float val){   m_detailSampleMaxError = val;   }
void set_monotonePartitioning(int val){     m_monotonePartitioning = !!val; }

/////////////////////////////
bool init()
{
    return getThreeJSMeshes();
}

bool initWithFile(std::string filename)
{
    printf("loading from file");
    m_geom = new InputGeom;
    if (!m_geom || !m_geom->loadMesh(m_ctx, filename.c_str()))
    {
        printf("cannot load OBJ file \n");
        return false;       
    }
    return true;
}

bool initWithFileContent(std::string contents)
{
    printf("loading from contents \n");
    // printf(contents.c_str());

    m_geom = new InputGeom;
    if (!m_geom || !m_geom->loadMeshFromContents(m_ctx, contents.c_str()))
    {
        printf("cannot load OBJ contents \n");
        return false;       
    }
    return true;
}

bool initCrowd(const int maxAgents, const float maxAgentRadius)
{
    m_crowd->init(maxAgents, maxAgentRadius, m_navMesh);

    return true;
}

struct agentUserData {
    int idx;
};

void updateCrowdAgentParameters(const int idx, float posX, float posY, float posZ, float radius, float height, 
                                                                float maxAcceleration, float maxSpeed, unsigned char updateFlags, float separationWeight)
{
    dtCrowdAgentParams ap;
    memset(&ap, 0, sizeof(ap));
    ap.radius = radius;
    ap.height = height;
    ap.maxAcceleration = maxAcceleration;
    ap.maxSpeed = maxSpeed;
    ap.collisionQueryRange = ap.radius * 12.0f;
    ap.pathOptimizationRange = ap.radius * 300.0f;
    ap.updateFlags = 0; 
    // if (m_toolParams.m_anticipateTurns)
    //  ap.updateFlags |= DT_CROWD_ANTICIPATE_TURNS;
    // if (m_toolParams.m_optimizeVis)
    //  ap.updateFlags |= DT_CROWD_OPTIMIZE_VIS;
    // if (m_toolParams.m_optimizeTopo)
    //  ap.updateFlags |= DT_CROWD_OPTIMIZE_TOPO;
    // if (m_toolParams.m_obstacleAvoidance)
    //  ap.updateFlags |= DT_CROWD_OBSTACLE_AVOIDANCE;
    // if (m_toolParams.m_separation)
    //  ap.updateFlags |= DT_CROWD_SEPARATION;
    ap.obstacleAvoidanceType = 3.0;
    ap.separationWeight = separationWeight;
    ap.userData = (void *)idx;

    float pos[3] = { posX, posY, posZ };

    m_crowd->updateAgentParameters(idx, &ap);
}

int addCrowdAgent(float posX, float posY, float posZ, float radius, float height, 
                                    float maxAcceleration, float maxSpeed, unsigned char updateFlags, float separationWeight)
{
    dtCrowdAgentParams ap;
    memset(&ap, 0, sizeof(ap));
    ap.radius = radius;
    ap.height = height;
    ap.maxAcceleration = maxAcceleration;
    ap.maxSpeed = maxSpeed;
    ap.collisionQueryRange = ap.radius * 12.0f;
    ap.pathOptimizationRange = ap.radius * 300.0f;
    ap.updateFlags = 0; 
    // if (m_toolParams.m_anticipateTurns)
    //  ap.updateFlags |= DT_CROWD_ANTICIPATE_TURNS;
    // if (m_toolParams.m_optimizeVis)
    //  ap.updateFlags |= DT_CROWD_OPTIMIZE_VIS;
    // if (m_toolParams.m_optimizeTopo)
    //  ap.updateFlags |= DT_CROWD_OPTIMIZE_TOPO;
    // if (m_toolParams.m_obstacleAvoidance)
    //  ap.updateFlags |= DT_CROWD_OBSTACLE_AVOIDANCE;
    // if (m_toolParams.m_separation)
    //  ap.updateFlags |= DT_CROWD_SEPARATION;
    ap.obstacleAvoidanceType = 3.0;
    ap.separationWeight = separationWeight;

    float pos[3] = { posX, posY, posZ };

    int idx = m_crowd->addAgent(pos, &ap);

    // agentUserData data;
    // memset(&data, 0, sizeof(data));
    // data.idx = 99;
    // ap.userData = (void *) &data;

    ap.userData = (void *)idx; /* FIXME: doesnt fucking work ://// */
    /* So we do this ?? */
    updateCrowdAgentParameters(idx, posX, posY, posZ, radius, height, maxAcceleration, maxSpeed, updateFlags, separationWeight);

    // char buff[512];
    // const dtCrowdAgent* ag = m_crowd->getAgent(idx);
    // const float* p = ag->npos;
    // const float r = ag->params.radius;
    // sprintf(buff, "debug('new agent', { idx:%d });", idx);
    // emscripten_run_script(buff);

    return idx;
}

void removeCrowdAgent(int idx)
{
    m_crowd->removeAgent(idx);
}

void requestMoveVelocity(int agentIdx, float velX, float velY, float velZ)
{
    float vel[3] = { velX, velY, velZ };
    m_crowd->requestMoveVelocity(agentIdx, vel);
}

bool crowdRequestMoveTarget(int agentIdx, float posX, float posY, float posZ)
{
    char buff[512];

    float pos[3] = { posX, posY, posZ };
    const float ext[3] = {2,4,2};

    dtPolyRef m_targetRef = 0;
    float m_targetPos[3];

    dtQueryFilter filter;
    filter.setIncludeFlags(3);
    filter.setExcludeFlags(0);

    dtStatus findStatus = m_navQuery->findNearestPoly(pos, ext, &filter, &m_targetRef, m_targetPos);
    if (dtStatusFailed(findStatus)) {
        // emscripten_run_script("debug('Cannot find a poly near specified position');");
        return false;
    } else {
        // emscripten_run_script("debug('MoveTarget adjusted');");
    }

    m_crowd->requestMoveTarget(agentIdx, m_targetRef, m_targetPos); 

    return true;
}

bool crowdUpdate(float dt)
{
    // char buff[512];

    // dtCrowdAgentDebugInfo m_agentDebug;
    // memset(&m_agentDebug, 0, sizeof(m_agentDebug));

    m_crowd->updateWithoutDebug(dt);

    return true;
}

bool crowdGetActiveAgents(std::string callback)
{
    int maxAgents = 100;

    dtCrowdAgent** agents = (dtCrowdAgent**)dtAlloc(sizeof(dtCrowdAgent*)*maxAgents, DT_ALLOC_PERM);
    int nagents = m_crowd->getActiveAgents(agents, maxAgents);

    // char buff[1024 * nagents];

/*
struct dtCrowdAgent
{
    /// 1 if the agent is active, or 0 if the agent is in an unused slot in the agent pool.
    unsigned char active;

    /// The type of mesh polygon the agent is traversing. (See: #CrowdAgentState)
    unsigned char state;

    /// The path corridor the agent is using.
    dtPathCorridor corridor;

    /// The local boundary data for the agent.
    dtLocalBoundary boundary;
    
    /// Time since the agent's path corridor was optimized.
    float topologyOptTime;
    
    /// The known neighbors of the agent.
    dtCrowdNeighbour neis[DT_CROWDAGENT_MAX_NEIGHBOURS];

    /// The number of neighbors.
    int nneis;
    
    /// The desired speed.
    float desiredSpeed;

    float npos[3];      ///< The current agent position. [(x, y, z)]
    float disp[3];
    float dvel[3];      ///< The desired velocity of the agent. [(x, y, z)]
    float nvel[3];
    float vel[3];       ///< The actual velocity of the agent. [(x, y, z)]

    /// The agent's configuration parameters.
    dtCrowdAgentParams params;

    /// The local path corridor corners for the agent. (Staight path.) [(x, y, z) * #ncorners]
    float cornerVerts[DT_CROWDAGENT_MAX_CORNERS*3];

    /// The local path corridor corner flags. (See: #dtStraightPathFlags) [(flags) * #ncorners]
    unsigned char cornerFlags[DT_CROWDAGENT_MAX_CORNERS];

    /// The reference id of the polygon being entered at the corner. [(polyRef) * #ncorners]
    dtPolyRef cornerPolys[DT_CROWDAGENT_MAX_CORNERS];

    /// The number of corners.
    int ncorners;
    
    unsigned char targetState;          ///< State of the movement request.
    dtPolyRef targetRef;                ///< Target polyref of the movement request.
    float targetPos[3];                 ///< Target position of the movement request (or velocity in case of DT_CROWDAGENT_TARGET_VELOCITY).
    dtPathQueueRef targetPathqRef;      ///< Path finder ref.
    bool targetReplan;                  ///< Flag indicating that the current path is being replanned.
    float targetReplanTime;             /// <Time since the agent's target was replanned.
};
 */

    // sprintf(buff, "__tmp_recastjs_crowd_data = []; var object;");

    for (int i = 0; i < nagents; i++) {
        dtCrowdAgent* ag = agents[i];
        const float* p = ag->npos;
        const float* v = ag->vel;
        const float r = ag->params.radius;
        int idx = (int) ag->params.userData;
        // agentUserData* data = (agentUserData*) ag->params.userData;

        // sprintf(buff, "debug({ position:{x:%f,y:%f,z:%f}, radius:%f, active:%d, state:%d });", p[0], p[1], p[2], r, ag->active, ag->state);
        // emscripten_run_script(buff);

        // sprintf(buff, "%s \n { \n var object = /* agentPool.get(); */ { position:{}, velocity:{} }; \n object.idx=%d; object.position.x=%f; object.position.y=%f; object.position.z=%f; object.velocity.x=%f; object.velocity.y=%f; object.velocity.z=%f; object.radius=%f; object.active=%d; object.state=%d; object.neighbors=%d; \n __tmp_recastjs_crowd_data.push(object);",
        //                buff,                                                                                      idx,                                 p[0],                              p[1],                               p[2],                               v[0],                           v[1],                               v[2],                           r,                              ag->active,          ag->state,                     ag->nneis);

        // sprintf(buff, "%s \n object = agentPool.get(%d,  %f,   %f,   %f,   %f,   %f,   %f,   %f, %d,         %d,        %d); \n __tmp_recastjs_crowd_data.push(object);",
        //                buff,                        idx, p[0], p[1], p[2], v[0], v[1], v[2], r,  ag->active, ag->state, ag->nneis);

        agentPool_get(idx, p[0], p[1], p[2], v[0], v[1], v[2], r, ag->active, ag->state, ag->nneis);
    }

    // sprintf(buff, "\n %s \n\n %s(__tmp_recastjs_crowd_data);", buff, callback.c_str());

    // emscripten_run_script(buff);
    // emscripten_run_script(buff);
    // emscripten_run_script(buff);
    flush_active_agents_callback();

    for (int i = 0; i < nagents; i++) {
        // sprintf(buff, "%s \n agentPool.add(__tmp_recastjs_crowd_data[%d]); \n", buff, i);
        agentPool_add(i);
    }

    agentPool_clear();

    // free(buff);

    return true;
}

bool build()
{
    dd = new DebugDrawGL;

    if (!m_geom || !m_geom->getMesh())
    {
        printf("buildNavigation: Input mesh is not specified.");
        m_ctx->log(RC_LOG_ERROR, "buildNavigation: Input mesh is not specified.");
        return false;
    }
    
    cleanup();

    const float* bmin = m_geom->getMeshBoundsMin();
    const float* bmax = m_geom->getMeshBoundsMax();
    const float* verts = m_geom->getMesh()->getVerts();
    const int nverts = m_geom->getMesh()->getVertCount();
    const int* tris = m_geom->getMesh()->getTris();
    const int ntris = m_geom->getMesh()->getTriCount();
    
    //
    // Step 1. Initialize build config.
    //
    
    // Init build configuration from GUI
    memset(&m_cfg, 0, sizeof(m_cfg));
    m_cfg.cs = m_cellSize;
    m_cfg.ch = m_cellHeight;
    m_cfg.walkableSlopeAngle = m_agentMaxSlope;
    m_cfg.walkableHeight = (int)ceilf(m_agentHeight / m_cfg.ch);
    m_cfg.walkableClimb = (int)floorf(m_agentMaxClimb / m_cfg.ch);
    m_cfg.walkableRadius = (int)ceilf(m_agentRadius / m_cfg.cs);
    m_cfg.maxEdgeLen = (int)(m_edgeMaxLen / m_cellSize);
    m_cfg.maxSimplificationError = m_edgeMaxError;
    m_cfg.minRegionArea = (int)rcSqr(m_regionMinSize);      // Note: area = size*size
    m_cfg.mergeRegionArea = (int)rcSqr(m_regionMergeSize);  // Note: area = size*size
    m_cfg.maxVertsPerPoly = (int)m_vertsPerPoly;
    m_cfg.detailSampleDist = m_detailSampleDist < 0.9f ? 0 : m_cellSize * m_detailSampleDist;
    m_cfg.detailSampleMaxError = m_cellHeight * m_detailSampleMaxError;
    
    // Set the area where the navigation will be build.
    // Here the bounds of the input mesh are used, but the
    // area could be specified by an user defined box, etc.
    rcVcopy(m_cfg.bmin, bmin);
    rcVcopy(m_cfg.bmax, bmax);
    rcCalcGridSize(m_cfg.bmin, m_cfg.bmax, m_cfg.cs, &m_cfg.width, &m_cfg.height);

    // Reset build times gathering.
    //m_ctx->resetTimers();

    printf("resetTimers \n");

    // Start the build process. 
    //m_ctx->startTimer(RC_TIMER_TOTAL);
    
    m_ctx->log(RC_LOG_PROGRESS, "Building navigation:");
    m_ctx->log(RC_LOG_PROGRESS, " - %d x %d cells", m_cfg.width, m_cfg.height);
    m_ctx->log(RC_LOG_PROGRESS, " - %.1fK verts, %.1fK tris", nverts/1000.0f, ntris/1000.0f);
    
    printf("Building navigation \n");

    //
    // Step 2. Rasterize input polygon soup.
    //
    
    // Allocate voxel heightfield where we rasterize our input data to.
    m_solid = rcAllocHeightfield();
    if (!m_solid)
    {
        printf("buildNavigation: Out of memory 'solid'. \n");
        m_ctx->log(RC_LOG_ERROR, "buildNavigation: Out of memory 'solid'.");
        return false;
    }
    if (!rcCreateHeightfield(m_ctx, *m_solid, m_cfg.width, m_cfg.height, m_cfg.bmin, m_cfg.bmax, m_cfg.cs, m_cfg.ch))
    {
        printf("buildNavigation: Could not create solid heightfield. \n");
        m_ctx->log(RC_LOG_ERROR, "buildNavigation: Could not create solid heightfield.");
        return false;
    }
    
    printf("Heightfield polygon soup \n");

    // Allocate array that can hold triangle area types.
    // If you have multiple meshes you need to process, allocate
    // and array which can hold the max number of triangles you need to process.
    m_triareas = new unsigned char[ntris];
    if (!m_triareas)
    {
        m_ctx->log(RC_LOG_ERROR, "buildNavigation: Out of memory 'm_triareas' (%d).", ntris);
        return false;
    }
    
    // Find triangles which are walkable based on their slope and rasterize them.
    // If your input data is multiple meshes, you can transform them here, calculate
    // the are type for each of the meshes and rasterize them.
    memset(m_triareas, 0, ntris*sizeof(unsigned char));
    rcMarkWalkableTriangles(m_ctx, m_cfg.walkableSlopeAngle, verts, nverts, tris, ntris, m_triareas);
    printf("%u Walkable Triangles\n", sizeof(m_triareas)/sizeof(unsigned char));

    rcRasterizeTriangles(m_ctx, verts, nverts, tris, m_triareas, ntris, *m_solid, m_cfg.walkableClimb);

    if (!m_keepInterResults)
    {
        delete [] m_triareas;
        m_triareas = 0;
    }
    
    //
    // Step 3. Filter walkables surfaces.
    //
    
    // Once all geoemtry is rasterized, we do initial pass of filtering to
    // remove unwanted overhangs caused by the conservative rasterization
    // as well as filter spans where the character cannot possibly stand.
    rcFilterLowHangingWalkableObstacles(m_ctx, m_cfg.walkableClimb, *m_solid);
    rcFilterLedgeSpans(m_ctx, m_cfg.walkableHeight, m_cfg.walkableClimb, *m_solid);
    rcFilterWalkableLowHeightSpans(m_ctx, m_cfg.walkableHeight, *m_solid);

    printf("filters \n");
    
    //
    // Step 4. Partition walkable surface to simple regions.
    //

    // Compact the heightfield so that it is faster to handle from now on.
    // This will result more cache coherent data as well as the neighbours
    // between walkable cells will be calculated.
    m_chf = rcAllocCompactHeightfield();
    if (!m_chf)
    {
        m_ctx->log(RC_LOG_ERROR, "buildNavigation: Out of memory 'chf'.");
        return false;
    }
    if (!rcBuildCompactHeightfield(m_ctx, m_cfg.walkableHeight, m_cfg.walkableClimb, *m_solid, *m_chf))
    {
        m_ctx->log(RC_LOG_ERROR, "buildNavigation: Could not build compact data.");
        return false;
    }
    
    if (!m_keepInterResults)
    {
        rcFreeHeightField(m_solid);
        m_solid = 0;
    }
        
    // Erode the walkable area by agent radius.
    if (!rcErodeWalkableArea(m_ctx, m_cfg.walkableRadius, *m_chf))
    {
        m_ctx->log(RC_LOG_ERROR, "buildNavigation: Could not erode.");
        return false;
    }

    // (Optional) Mark areas.
    const ConvexVolume* vols = m_geom->getConvexVolumes();
    for (int i  = 0; i < m_geom->getConvexVolumeCount(); ++i)
        rcMarkConvexPolyArea(m_ctx, vols[i].verts, vols[i].nverts, vols[i].hmin, vols[i].hmax, (unsigned char)vols[i].area, *m_chf);
    
    if (m_monotonePartitioning)
    {
        // Partition the walkable surface into simple regions without holes.
        // Monotone partitioning does not need distancefield.
        if (!rcBuildRegionsMonotone(m_ctx, *m_chf, 0, m_cfg.minRegionArea, m_cfg.mergeRegionArea))
        {
            m_ctx->log(RC_LOG_ERROR, "buildNavigation: Could not build regions.");
            return false;
        }
    }
    else
    {
        // Prepare for region partitioning, by calculating distance field along the walkable surface.
        if (!rcBuildDistanceField(m_ctx, *m_chf))
        {
            m_ctx->log(RC_LOG_ERROR, "buildNavigation: Could not build distance field.");
            return false;
        }

        // Partition the walkable surface into simple regions without holes.
        if (!rcBuildRegions(m_ctx, *m_chf, 0, m_cfg.minRegionArea, m_cfg.mergeRegionArea))
        {
            m_ctx->log(RC_LOG_ERROR, "buildNavigation: Could not build regions.");
            return false;
        }
    }

    printf("Partition walkable surface \n");

    //
    // Step 5. Trace and simplify region contours.
    //
    
    // Create contours.
    m_cset = rcAllocContourSet();
    if (!m_cset)
    {
        m_ctx->log(RC_LOG_ERROR, "buildNavigation: Out of memory 'cset'.");
        return false;
    }
    if (!rcBuildContours(m_ctx, *m_chf, m_cfg.maxSimplificationError, m_cfg.maxEdgeLen, *m_cset))
    {
        m_ctx->log(RC_LOG_ERROR, "buildNavigation: Could not create contours.");
        return false;
    }
    
    printf("Trace and simplify region contours: %u conts (maxSimplificationError= %f, maxEdgeLen= %u)\n", m_cset->nconts, m_cfg.maxSimplificationError, m_cfg.maxEdgeLen);

    //
    // Step 6. Build polygons mesh from contours.
    //
    
    // Build polygon navmesh from the contours.
    m_pmesh = rcAllocPolyMesh();
    if (!m_pmesh)
    {
        m_ctx->log(RC_LOG_ERROR, "buildNavigation: Out of memory 'pmesh'.");
        return false;
    }
    if (!rcBuildPolyMesh(m_ctx, *m_cset, m_cfg.maxVertsPerPoly, *m_pmesh))
    {
        m_ctx->log(RC_LOG_ERROR, "buildNavigation: Could not triangulate contours.");
        return false;
    }
    
    printf("Build polygons mesh from contours. \n");

    //
    // Step 7. Create detail mesh which allows to access approximate height on each polygon.
    //
    
    m_dmesh = rcAllocPolyMeshDetail();
    if (!m_dmesh)
    {
        m_ctx->log(RC_LOG_ERROR, "buildNavigation: Out of memory 'pmdtl'.");
        return false;
    }

    if (!rcBuildPolyMeshDetail(m_ctx, *m_pmesh, *m_chf, m_cfg.detailSampleDist, m_cfg.detailSampleMaxError, *m_dmesh))
    {
        m_ctx->log(RC_LOG_ERROR, "buildNavigation: Could not build detail mesh.");
        return false;
    }

    if (!m_keepInterResults)
    {
        rcFreeCompactHeightfield(m_chf);
        m_chf = 0;
        rcFreeContourSet(m_cset);
        m_cset = 0;
    }

    printf("Create detail mesh. \n");

    // At this point the navigation mesh data is ready, you can access it from m_pmesh.
    // See duDebugDrawPolyMesh or dtCreateNavMeshData as examples how to access the data.
    
    //
    // (Optional) Step 8. Create Detour data from Recast poly mesh.
    //
    
    // The GUI may allow more max points per polygon than Detour can handle.
    // Only build the detour navmesh if we do not exceed the limit.
    if (m_cfg.maxVertsPerPoly <= DT_VERTS_PER_POLYGON)
    {
        unsigned char* navData = 0;
        int navDataSize = 0;

        char buff[512];
        sprintf(buff, "Config.detailMeshPoly = %u;", m_pmesh->npolys);
        emscripten_run_script(buff);

        // Update poly flags from areas.
        for (int i = 0; i < m_pmesh->npolys; ++i)
        {
            //printf("Update poly flags from area %u \n", i);

            if (m_pmesh->areas[i] == RC_WALKABLE_AREA)
                m_pmesh->areas[i] = SAMPLE_POLYAREA_GROUND;
                
            if (m_pmesh->areas[i] == SAMPLE_POLYAREA_GROUND ||
                m_pmesh->areas[i] == SAMPLE_POLYAREA_GRASS ||
                m_pmesh->areas[i] == SAMPLE_POLYAREA_ROAD)
            {
                m_pmesh->flags[i] = SAMPLE_POLYFLAGS_WALK;
            }
            else if (m_pmesh->areas[i] == SAMPLE_POLYAREA_WATER)
            {
                m_pmesh->flags[i] = SAMPLE_POLYFLAGS_SWIM;
            }
            else if (m_pmesh->areas[i] == SAMPLE_POLYAREA_DOOR)
            {
                m_pmesh->flags[i] = SAMPLE_POLYFLAGS_WALK | SAMPLE_POLYFLAGS_DOOR;
            }
        }


        dtNavMeshCreateParams params;
        memset(&params, 0, sizeof(params));
        params.verts = m_pmesh->verts;
        params.vertCount = m_pmesh->nverts;
        params.polys = m_pmesh->polys;
        params.polyAreas = m_pmesh->areas;
        params.polyFlags = m_pmesh->flags;
        params.polyCount = m_pmesh->npolys;
        params.nvp = m_pmesh->nvp;
        params.detailMeshes = m_dmesh->meshes;
        params.detailVerts = m_dmesh->verts;
        params.detailVertsCount = m_dmesh->nverts;
        params.detailTris = m_dmesh->tris;
        params.detailTriCount = m_dmesh->ntris;
        params.offMeshConVerts = m_geom->getOffMeshConnectionVerts();
        params.offMeshConRad = m_geom->getOffMeshConnectionRads();
        params.offMeshConDir = m_geom->getOffMeshConnectionDirs();
        params.offMeshConAreas = m_geom->getOffMeshConnectionAreas();
        params.offMeshConFlags = m_geom->getOffMeshConnectionFlags();
        params.offMeshConUserID = m_geom->getOffMeshConnectionId();
        params.offMeshConCount = m_geom->getOffMeshConnectionCount();
        params.walkableHeight = m_agentHeight;
        params.walkableRadius = m_agentRadius;
        params.walkableClimb = m_agentMaxClimb;
        rcVcopy(params.bmin, m_pmesh->bmin);
        rcVcopy(params.bmax, m_pmesh->bmax);
        params.cs = m_cfg.cs;
        params.ch = m_cfg.ch;
        params.buildBvTree = true;

        // printf("dtNavMeshCreateParams %p \n", params);
        debugConfig();
        
        if (!dtCreateNavMeshData(&params, &navData, &navDataSize))
        {
            m_ctx->log(RC_LOG_ERROR, "Could not build Detour navmesh.");
            return false;
        }
        
        printf("Built Detour navdata. %p \n", navData);

        m_navMesh = dtAllocNavMesh();
        if (!m_navMesh)
        {
            dtFree(navData);
            m_ctx->log(RC_LOG_ERROR, "Could not create Detour navmesh");
            return false;
        }

        printf("Created Detour navmesh. %p \n", m_navMesh);

        dtStatus status;
        
        status = m_navMesh->init(navData, navDataSize, DT_TILE_FREE_DATA);
        if (dtStatusFailed(status))
        {
            dtFree(navData);
            printf("Could not init Detour navmesh (%u) \n", status);
            m_ctx->log(RC_LOG_ERROR, "Could not init Detour navmesh");
            return false;
        }
        
        printf("Init Detour navmesh. %p \n", navData);

        m_navQuery = dtAllocNavMeshQuery();
        status = m_navQuery->init(m_navMesh, 2048);
        if (dtStatusFailed(status))
        {
            printf("Could not init Detour navmesh query (%u) \n", status);
            m_ctx->log(RC_LOG_ERROR, "Could not init Detour navmesh query");
            return false;
        }
    }

    m_crowd = dtAllocCrowd();
    if (!m_crowd)
    {
        dtFree(m_crowd);
        m_ctx->log(RC_LOG_ERROR, "Could not create Detour Crowd");
        return false;
    }

    printf("m_navMesh=%p \n", m_navMesh);
    
    printf("m_navQuery=%p \n", m_navQuery);

    //m_ctx->stopTimer(RC_TIMER_TOTAL);

    // Show performance stats.
    //duLogBuildTimes(*m_ctx, m_ctx->getAccumulatedTime(RC_TIMER_TOTAL));
    //m_ctx->log(RC_LOG_PROGRESS, ">> Polymesh: %d vertices  %d polygons", m_pmesh->nverts, m_pmesh->npolys);
    
    //m_totalBuildTimeMs = m_ctx->getAccumulatedTime(RC_TIMER_TOTAL)/1000.0f;

    return true;
}



#include <emscripten/bind.h>

using namespace emscripten;

EMSCRIPTEN_BINDINGS(my_module) {
  function("init", &init);
  function("initWithFile", &initWithFile);
  function("initWithFileContent", &initWithFileContent);

  function("build", &build);

  function("bareGeomInit", &bareGeomInit);
    function("bareGeomAddVertex", &bareGeomAddVertex);
    function("bareGeomAddTriangle", &bareGeomAddTriangle);
    function("bareGeomValidate", &bareGeomValidate);

    function("getNavMeshVertices", &getNavMeshVertices);
    function("getNavHeightfieldRegions", &getNavHeightfieldRegions);

    function("findNearestPoly", &findNearestPoly);
    function("findNearestPoint", &findNearestPoint);
    function("findPath", &findPath);
    function("setPolyUnwalkable", &setPolyUnwalkable);
    function("getRandomPoint", &getRandomPoint);

  function("initCrowd", &initCrowd);
  function("addCrowdAgent", &addCrowdAgent);
  function("updateCrowdAgentParameters", &updateCrowdAgentParameters);
  function("removeCrowdAgent", &removeCrowdAgent);
  function("crowdRequestMoveTarget", &crowdRequestMoveTarget);
  function("crowdUpdate", &crowdUpdate);
  function("crowdGetActiveAgents", &crowdGetActiveAgents);
  function("requestMoveVelocity", &requestMoveVelocity);


    function("set_cellSize", &set_cellSize);
    function("set_cellHeight", &set_cellHeight);
    function("set_agentHeight", &set_agentHeight);
    function("set_agentRadius", &set_agentRadius);
    function("set_agentMaxClimb", &set_agentMaxClimb);
    function("set_agentMaxSlope", &set_agentMaxSlope);
    function("set_regionMinSize", &set_regionMinSize);
    function("set_regionMergeSize", &set_regionMergeSize);
    function("set_edgeMaxLen", &set_edgeMaxLen);
    function("set_edgeMaxError", &set_edgeMaxError);
    function("set_vertsPerPoly", &set_vertsPerPoly);
    function("set_detailSampleDist", &set_detailSampleDist);
    function("set_detailSampleMaxError", &set_detailSampleMaxError);


    function("debugDrawNavMesh", &debugDrawNavMesh);
    function("debugDrawNavMeshPortals", &debugDrawNavMeshPortals);
    function("debugDrawRegionConnections", &debugDrawRegionConnections);
    function("debugDrawRawContours", &debugDrawRawContours);
    function("debugDrawContours", &debugDrawContours);
    function("debugDrawHeightfieldSolid", &debugDrawHeightfieldSolid);
    function("debugDrawHeightfieldWalkable", &debugDrawHeightfieldWalkable);

}

