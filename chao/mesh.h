#ifndef CHAO_MESH_H
#define CHAO_MESH_H

// Building Model3D meshes in code.
//
// Winding: front faces are counter-clockwise when seen from outside. Back faces are culled,
// so for something thin that must be visible from both sides (a sheet of paper, a flag),
// add each triangle twice with opposite windings.

#include <stdlib.h>
#include "types.h"

// Frees the model and its geometry. The texture is not freed, since textures are often shared.
void model3d_free(Model3D* model) {
    if (model == NULL) return;
    free(model->vertices);
    free(model->normals);
    free(model->uvs);
    free(model->tris);
    free(model->tri_uvs);
    free(model->tri_normals);
    free(model);
}

// Allocates an empty model; the caller fills in every array.
Model3D* model3d_create(int vertex_count, int normal_count, int uv_count, int tris_count) {
    Model3D* m = (Model3D*)malloc(sizeof(Model3D));
    m->vertices    = (Vector3*)malloc(sizeof(Vector3) * (vertex_count > 0 ? vertex_count : 1));
    m->normals     = (Vector3*)malloc(sizeof(Vector3) * (normal_count > 0 ? normal_count : 1));
    m->uvs         = (Vector2*)malloc(sizeof(Vector2) * (uv_count > 0 ? uv_count : 1));
    m->tris        = (Vector3Int*)malloc(sizeof(Vector3Int) * (tris_count > 0 ? tris_count : 1));
    m->tri_uvs     = (Vector3Int*)malloc(sizeof(Vector3Int) * (tris_count > 0 ? tris_count : 1));
    m->tri_normals = (Vector3Int*)malloc(sizeof(Vector3Int) * (tris_count > 0 ? tris_count : 1));
    m->tris_count  = tris_count;
    m->texture     = NULL;
    m->color       = 0xFFFFFFFF;
    return m;
}

// A flat-shaded model from a list of triangles: positions holds 3 corners per triangle.
// Every triangle gets its own face normal; UVs are all (0, 0), so use model->color to colour it.
Model3D* model3d_create_from_triangles(const Vector3* positions, int tris_count) {
    Model3D* m = model3d_create(tris_count * 3, tris_count, 1, tris_count);
    m->uvs[0] = VECTOR2_ZERO;
    for (int i = 0; i < tris_count; ++i) {
        Vector3 a = positions[i * 3], b = positions[i * 3 + 1], c = positions[i * 3 + 2];
        m->vertices[i * 3]     = a;
        m->vertices[i * 3 + 1] = b;
        m->vertices[i * 3 + 2] = c;
        m->normals[i] = vector3_get_normalized(vector3_cross(vector3_subtract(b, a), vector3_subtract(c, a)));
        m->tris[i] = (Vector3Int){ i * 3, i * 3 + 1, i * 3 + 2 };
        m->tri_normals[i] = (Vector3Int){ i, i, i };
        m->tri_uvs[i] = (Vector3Int){ 0, 0, 0 };
    }
    return m;
}

// Box centred on the origin.
Model3D* model3d_create_box(Vector3 size) {
    float x = size.x * 0.5f, y = size.y * 0.5f, z = size.z * 0.5f;
    Vector3 p[8] = {
        {-x, -y, -z}, { x, -y, -z}, { x,  y, -z}, {-x,  y, -z},  // back (z-)
        {-x, -y,  z}, { x, -y,  z}, { x,  y,  z}, {-x,  y,  z},  // front (z+)
    };
    // two triangles per face, counter-clockwise seen from outside
    int quads[6][4] = {
        {4, 5, 6, 7},  // +z
        {1, 0, 3, 2},  // -z
        {5, 1, 2, 6},  // +x
        {0, 4, 7, 3},  // -x
        {7, 6, 2, 3},  // +y
        {0, 1, 5, 4},  // -y
    };
    Vector3 tris[36];
    for (int f = 0; f < 6; ++f) {
        int* q = quads[f];
        tris[f * 6 + 0] = p[q[0]]; tris[f * 6 + 1] = p[q[1]]; tris[f * 6 + 2] = p[q[2]];
        tris[f * 6 + 3] = p[q[0]]; tris[f * 6 + 4] = p[q[2]]; tris[f * 6 + 5] = p[q[3]];
    }
    return model3d_create_from_triangles(tris, 12);
}

// Cylinder standing on the origin, from y = 0 to y = height, with both caps.
Model3D* model3d_create_cylinder(float radius, float height, int segments) {
    if (segments < 3) segments = 3;
    Vector3* tris = (Vector3*)malloc(sizeof(Vector3) * segments * 12);
    int n = 0;
    Vector3 bottom = { 0, 0, 0 }, top = { 0, height, 0 };
    for (int i = 0; i < segments; ++i) {
        float a0 = (float)i / segments * 2.0f * (float)M_PI;
        float a1 = (float)(i + 1) / segments * 2.0f * (float)M_PI;
        Vector3 b0 = { cosf(a0) * radius, 0, sinf(a0) * radius };
        Vector3 b1 = { cosf(a1) * radius, 0, sinf(a1) * radius };
        Vector3 t0 = { b0.x, height, b0.z };
        Vector3 t1 = { b1.x, height, b1.z };
        // side
        tris[n++] = b0; tris[n++] = t1; tris[n++] = b1;
        tris[n++] = b0; tris[n++] = t0; tris[n++] = t1;
        // caps
        tris[n++] = top; tris[n++] = t1; tris[n++] = t0;
        tris[n++] = bottom; tris[n++] = b0; tris[n++] = b1;
    }
    Model3D* m = model3d_create_from_triangles(tris, n / 3);
    free(tris);
    return m;
}

// Cone standing on the origin: base circle at y = 0, tip at y = height.
Model3D* model3d_create_cone(float radius, float height, int segments) {
    if (segments < 3) segments = 3;
    Vector3* tris = (Vector3*)malloc(sizeof(Vector3) * segments * 6);
    int n = 0;
    Vector3 bottom = { 0, 0, 0 }, tip = { 0, height, 0 };
    for (int i = 0; i < segments; ++i) {
        float a0 = (float)i / segments * 2.0f * (float)M_PI;
        float a1 = (float)(i + 1) / segments * 2.0f * (float)M_PI;
        Vector3 b0 = { cosf(a0) * radius, 0, sinf(a0) * radius };
        Vector3 b1 = { cosf(a1) * radius, 0, sinf(a1) * radius };
        tris[n++] = b0; tris[n++] = tip; tris[n++] = b1;
        tris[n++] = bottom; tris[n++] = b0; tris[n++] = b1;
    }
    Model3D* m = model3d_create_from_triangles(tris, n / 3);
    free(tris);
    return m;
}

typedef float (*HeightmapFunc)(float x, float z, void* user);

// Square heightmap grid of cells x cells, centred on the origin, with heights from height_at().
// Normals are smooth (central differences of height_at), UVs span 0..1 over the whole grid,
// so a texture painted for the same area lines up with the terrain.
Model3D* model3d_create_heightmap(int cells, float cell_size, HeightmapFunc height_at, void* user) {
    const int side = cells + 1;
    const int verts = side * side;
    const float half = cells * cell_size * 0.5f;
    Model3D* m = model3d_create(verts, verts, verts, cells * cells * 2);

    for (int j = 0; j < side; ++j) {
        for (int i = 0; i < side; ++i) {
            float x = i * cell_size - half;
            float z = j * cell_size - half;
            int idx = j * side + i;
            m->vertices[idx] = (Vector3){ x, height_at(x, z, user), z };
            m->uvs[idx] = (Vector2){ (float)i / cells, (float)j / cells };
            float dx = height_at(x + cell_size, z, user) - height_at(x - cell_size, z, user);
            float dz = height_at(x, z + cell_size, user) - height_at(x, z - cell_size, user);
            m->normals[idx] = vector3_get_normalized((Vector3){ -dx, 2.0f * cell_size, -dz });
        }
    }

    // two counter-clockwise (seen from above) triangles per cell
    int t = 0;
    for (int j = 0; j < cells; ++j) {
        for (int i = 0; i < cells; ++i) {
            int a = j * side + i;  // (i,   j)
            int b = a + 1;         // (i+1, j)
            int c = a + side;      // (i,   j+1)
            int d = c + 1;         // (i+1, j+1)
            Vector3Int t0 = { a, c, b };
            Vector3Int t1 = { b, c, d };
            m->tris[t] = m->tri_uvs[t] = m->tri_normals[t] = t0; ++t;
            m->tris[t] = m->tri_uvs[t] = m->tri_normals[t] = t1; ++t;
        }
    }
    return m;
}

// Flat quad in the XY plane, centred on the origin.
Model3D* model3d_create_quad(Vector2 size) {
    Model3D* m = model3d_create(4, 4, 4, 2);

    float hw = size.x * 0.5f;
    float hh = size.y * 0.5f;

    m->vertices[0] = (Vector3){ -hw, -hh, 0 };
    m->vertices[1] = (Vector3){  hw, -hh, 0 };
    m->vertices[2] = (Vector3){  hw,  hh, 0 };
    m->vertices[3] = (Vector3){ -hw,  hh, 0 };

    m->uvs[0] = (Vector2){0,1};
    m->uvs[1] = (Vector2){1,1};
    m->uvs[2] = (Vector2){1,0};
    m->uvs[3] = (Vector2){0,0};

    for (int i = 0; i < 4; i++) {
        m->normals[i] = (Vector3){0,0,1};  // counter-clockwise seen from +z, so it faces +z
    }

    m->tris[0] = (Vector3Int){0, 1, 2};
    m->tris[1] = (Vector3Int){0, 2, 3};
    m->tri_uvs[0] = (Vector3Int){0,1,2};
    m->tri_uvs[1] = (Vector3Int){0,2,3};
    m->tri_normals[0] = (Vector3Int){0,1,2};
    m->tri_normals[1] = (Vector3Int){0,2,3};

    return m;
}

#endif // CHAO_MESH_H
