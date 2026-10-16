#include <bloxide/SurfaceHierarchy.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <cstring>
#include <map>
#include <bloxide/Trace.hpp>
#include <unordered_map>
#include <unordered_set>
namespace bloxide {
namespace {
using V = std::array<double, 3>;
V add(V a, V b) {
    for (int i = 0; i < 3; ++i)
        a[i] += b[i];
    return a;
}
V sub(V a, V b) {
    for (int i = 0; i < 3; ++i)
        a[i] -= b[i];
    return a;
}
V mul(V a, double b) {
    for (auto& v : a)
        v *= b;
    return a;
}
double dot(V a, V b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
V cross(V a, V b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
V position(const SurfaceVertex& v) {
    return {v.x, v.y, v.z};
}
struct Quadric {
    std::array<double, 16> q{};
    void plane(V n, double d) {
        double p[]{n[0], n[1], n[2], d};
        for (int a = 0; a < 4; ++a)
            for (int b = 0; b < 4; ++b)
                q[a * 4 + b] += p[a] * p[b];
    }
    Quadric operator+(const Quadric& o) const {
        Quadric sum;
        for (int i = 0; i < 16; ++i)
            sum.q[i] = q[i] + o.q[i];
        return sum;
    }
    double cost(V v) const {
        double p[]{v[0], v[1], v[2], 1}, value = 0;
        for (int a = 0; a < 4; ++a)
            for (int b = 0; b < 4; ++b)
                value += p[a] * q[a * 4 + b] * p[b];
        return std::max(0.0, value);
    }
    bool solve(V& out) const {
        double a[3][4]{};
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c)
                a[r][c] = q[r * 4 + c];
            a[r][3] = -q[r * 4 + 3];
        }
        for (int c = 0; c < 3; ++c) {
            int pivot = c;
            for (int r = c + 1; r < 3; ++r)
                if (std::abs(a[r][c]) > std::abs(a[pivot][c]))
                    pivot = r;
            if (std::abs(a[pivot][c]) < 1e-10)
                return false;
            for (int k = c; k < 4; ++k)
                std::swap(a[pivot][k], a[c][k]);
            double scale = a[c][c];
            for (int k = c; k < 4; ++k)
                a[c][k] /= scale;
            for (int r = 0; r < 3; ++r)
                if (r != c) {
                    double factor = a[r][c];
                    for (int k = c; k < 4; ++k)
                        a[r][k] -= factor * a[c][k];
                }
        }
        for (int i = 0; i < 3; ++i)
            out[i] = a[i][3];
        return true;
    }
};
struct Node {
    V original{}, p{};
    Quadric q;
    unsigned parent = 0, layer = 0;
    bool locked = false;
    std::vector<unsigned> triangles;
};
struct Triangle {
    std::array<unsigned, 3> v;
    bool removed = false;
};
unsigned root(std::vector<Node>& nodes, unsigned v) {
    while (nodes[v].parent != v) {
        nodes[v].parent = nodes[nodes[v].parent].parent;
        v = nodes[v].parent;
    }
    return v;
}
bool sameCell(V a, V b, unsigned step) {
    for (int i = 0; i < 3; ++i)
        if (std::floor(a[i] / step) != std::floor(b[i] / step))
            return false;
    return true;
}
bool segmentTriangle(V a, V b, V p, V q, V r) {
    auto direction = sub(b, a), edge1 = sub(q, p), edge2 = sub(r, p), h = cross(direction, edge2);
    double determinant = dot(edge1, h);
    if (std::abs(determinant) < 1e-10)
        return false;
    double inv = 1 / determinant;
    auto s = sub(a, p);
    double u = inv * dot(s, h);
    if (u <= 1e-6 || u >= 1 - 1e-6)
        return false;
    auto k = cross(s, edge1);
    double v = inv * dot(direction, k);
    if (v <= 1e-6 || u + v >= 1 - 1e-6)
        return false;
    double t = inv * dot(edge2, k);
    return t > 1e-6 && t < 1 - 1e-6;
}
bool intersects(const std::array<V, 3>& a, const std::array<V, 3>& b) {
    for (int d = 0; d < 3; ++d) {
        double al = std::min({a[0][d], a[1][d], a[2][d]}),
               ah = std::max({a[0][d], a[1][d], a[2][d]});
        double bl = std::min({b[0][d], b[1][d], b[2][d]}),
               bh = std::max({b[0][d], b[1][d], b[2][d]});
        if (ah < bl - 1e-6 || bh < al - 1e-6)
            return false;
    }
    for (int i = 0; i < 3; ++i)
        if (segmentTriangle(a[i], a[(i + 1) % 3], b[0], b[1], b[2]) ||
            segmentTriangle(b[i], b[(i + 1) % 3], a[0], a[1], a[2]))
            return true;
    auto n = cross(sub(a[1], a[0]), sub(a[2], a[0]));
    double length = std::sqrt(dot(n, n));
    if (length > 1e-10 && std::abs(dot(n, sub(b[0], a[0]))) < 1e-8 * length &&
        std::abs(dot(n, sub(b[1], a[0]))) < 1e-8 * length &&
        std::abs(dot(n, sub(b[2], a[0]))) < 1e-8 * length) {
        int drop = 0;
        for (int d = 1; d < 3; ++d)
            if (std::abs(n[d]) > std::abs(n[drop]))
                drop = d;
        int x = (drop + 1) % 3, y = (drop + 2) % 3;
        auto orient = [&](V p, V q, V r) {
            return (q[x] - p[x]) * (r[y] - p[y]) - (q[y] - p[y]) * (r[x] - p[x]);
        };
        auto inside = [&](V p, const std::array<V, 3>& t) {
            double u = orient(t[0], t[1], p), v = orient(t[1], t[2], p), w = orient(t[2], t[0], p);
            return (u > 1e-8 && v > 1e-8 && w > 1e-8) || (u < -1e-8 && v < -1e-8 && w < -1e-8);
        };
        for (int i = 0; i < 3; ++i) {
            if (inside(a[i], b) || inside(b[i], a))
                return true;
            for (int j = 0; j < 3; ++j)
                if (orient(a[i], a[(i + 1) % 3], b[j]) *
                            orient(a[i], a[(i + 1) % 3], b[(j + 1) % 3]) <
                        -1e-12 &&
                    orient(b[j], b[(j + 1) % 3], a[i]) *
                            orient(b[j], b[(j + 1) % 3], a[(i + 1) % 3]) <
                        -1e-12)
                    return true;
        }
    }
    return false;
}
void decode(const std::vector<ChunkMeshVertex>& input, std::vector<SurfaceVertex>& out) {
    out.clear();
    out.reserve(input.size());
    for (auto v : input) {
        auto p = v.packed;
        float x = float(p & 31), y = float((p >> 5) & 511), z = float((p >> 14) & 31);
        out.push_back({x, y, z, x, y, z, (p >> 19) & 127, (p >> 26) & 7, (p >> 29) & 3,
                       SurfaceHierarchy::anchor(x, y, z)});
    }
}
} // namespace
std::uint32_t SurfaceHierarchy::anchor(float x, float y, float z) noexcept {
    return (unsigned(std::lround(x * 8)) & 255) | ((unsigned(std::lround(y * 8)) & 4095) << 8) |
           ((unsigned(std::lround(z * 8)) & 255) << 20);
}
bool SurfaceHierarchy::validMorph(const ChunkMesh& mesh) {
    if (std::none_of(mesh.surfaceVertices.begin(), mesh.surfaceVertices.end(),
                     [](const SurfaceVertex& v) {
                         return v.x != v.fromX || v.y != v.fromY || v.z != v.fromZ;
                     }))
        return true;
    for (std::size_t i = 0; i + 2 < mesh.surfaceIndices.size(); i += 3) {
        std::array<SurfaceVertex, 3> v{mesh.surfaceVertices[mesh.surfaceIndices[i]],
                                       mesh.surfaceVertices[mesh.surfaceIndices[i + 1]],
                                       mesh.surfaceVertices[mesh.surfaceIndices[i + 2]]};
        std::array<V, 3> original{};
        for (int k = 0; k < 3; ++k)
            original[k] = {v[k].fromX, v[k].fromY, v[k].fromZ};
        auto normal = cross(sub(original[1], original[0]), sub(original[2], original[0]));
        // A refinement can begin with collapsed triangles; use the nondegenerate endpoint instead.
        if (dot(normal, normal) < 1e-12)
            normal =
                cross(sub(position(v[1]), position(v[0])), sub(position(v[2]), position(v[0])));
        // The oriented area is quadratic in t. Check its exact minimum on [0,1],
        // rather than five samples which can miss a brief triangle inversion.
        auto e1 = sub(original[1], original[0]), e2 = sub(original[2], original[0]);
        auto d1 = sub(sub(position(v[1]), position(v[0])), e1);
        auto d2 = sub(sub(position(v[2]), position(v[0])), e2);
        double a = dot(cross(d1, d2), normal), b = dot(add(cross(d1, e2), cross(e1, d2)), normal),
               c = dot(cross(e1, e2), normal);
        auto area = [&](double t) { return (a * t + b) * t + c; };
        if (area(0) < -1e-8 || area(1) < -1e-8)
            return false;
        if (a > 0) {
            double t = -b / (2 * a);
            if (t > 0 && t < 1 && area(t) < -1e-8)
                return false;
        }
    }
    // Conservative continuous collision check. Each moving triangle stays inside
    // the convex hull of its six endpoint vertices. A fixed separating axis
    // proves the complete trajectories disjoint; uncertain non-adjacent pairs
    // keep the tile at finer resolution instead of trusting temporal samples.
    struct Sweep {
        std::array<V, 6> p;
        std::array<unsigned, 3> anchors;
        V lo{}, hi{};
        bool moved = false;
    };
    std::vector<Sweep> sweeps;
    sweeps.reserve(mesh.surfaceIndices.size() / 3);
    bool anyMoved = false;
    for (std::size_t i = 0; i + 2 < mesh.surfaceIndices.size(); i += 3) {
        Sweep s;
        for (unsigned k = 0; k < 3; ++k) {
            auto& v = mesh.surfaceVertices[mesh.surfaceIndices[i + k]];
            s.p[k] = {v.fromX, v.fromY, v.fromZ};
            s.p[k + 3] = position(v);
            s.anchors[k] = v.anchor;
            s.moved |= s.p[k] != s.p[k + 3];
        }
        s.lo = s.hi = s.p[0];
        for (auto& p : s.p)
            for (unsigned d = 0; d < 3; ++d) {
                s.lo[d] = std::min(s.lo[d], p[d]);
                s.hi[d] = std::max(s.hi[d], p[d]);
            }
        anyMoved |= s.moved;
        sweeps.push_back(s);
    }
    if (!anyMoved)
        return true;
    auto key = [](int x, int y, int z) {
        return (std::uint64_t(unsigned(x + 2)) << 40) | (std::uint64_t(unsigned(y + 2)) << 20) |
               unsigned(z + 2);
    };
    std::unordered_map<std::uint64_t, std::vector<unsigned>> bins;
    for (unsigned i = 0; i < sweeps.size(); ++i) {
        auto& s = sweeps[i];
        for (int z = int(std::floor(s.lo[2] / 8)); z <= int(std::floor(s.hi[2] / 8)); ++z)
            for (int y = int(std::floor(s.lo[1] / 8)); y <= int(std::floor(s.hi[1] / 8)); ++y)
                for (int x = int(std::floor(s.lo[0] / 8)); x <= int(std::floor(s.hi[0] / 8)); ++x)
                    bins[key(x, y, z)].push_back(i);
    }
    auto separated = [](const Sweep& a, const Sweep& b, V axis) {
        double length = std::sqrt(dot(axis, axis));
        if (length < 1e-10)
            return false;
        double al = dot(a.p[0], axis), ah = al, bl = dot(b.p[0], axis), bh = bl;
        for (auto& p : a.p) {
            al = std::min(al, dot(p, axis));
            ah = std::max(ah, dot(p, axis));
        }
        for (auto& p : b.p) {
            bl = std::min(bl, dot(p, axis));
            bh = std::max(bh, dot(p, axis));
        }
        if (ah - al < 1e-8 * length && bh - bl < 1e-8 * length && std::abs(al - bl) < 1e-8 * length)
            return false;
        return ah <= bl + 1e-8 * length || bh <= al + 1e-8 * length;
    };
    for (unsigned i = 0; i < sweeps.size(); ++i) {
        auto& a = sweeps[i];
        if (!a.moved)
            continue;
        std::unordered_set<unsigned> nearby;
        for (int z = int(std::floor(a.lo[2] / 8)); z <= int(std::floor(a.hi[2] / 8)); ++z)
            for (int y = int(std::floor(a.lo[1] / 8)); y <= int(std::floor(a.hi[1] / 8)); ++y)
                for (int x = int(std::floor(a.lo[0] / 8)); x <= int(std::floor(a.hi[0] / 8)); ++x)
                    for (auto id : bins[key(x, y, z)])
                        nearby.insert(id);
        for (auto j : nearby) {
            if (j == i || (j < i && sweeps[j].moved))
                continue;
            auto& b = sweeps[j];
            bool adjacent = false;
            for (auto va : a.anchors)
                for (auto vb : b.anchors)
                    adjacent |= va == vb;
            if (adjacent)
                continue;
            bool safe = false;
            for (unsigned d = 0; d < 3; ++d) {
                V axis{};
                axis[d] = 1;
                safe |= separated(a, b, axis);
            }
            if (safe)
                continue;
            for (unsigned end : {0U, 3U}) {
                auto an = cross(sub(a.p[end + 1], a.p[end]), sub(a.p[end + 2], a.p[end]));
                auto bn = cross(sub(b.p[end + 1], b.p[end]), sub(b.p[end + 2], b.p[end]));
                safe |= separated(a, b, an);
                safe |= separated(a, b, bn);
                for (unsigned e = 0; e < 3 && !safe; ++e) {
                    safe |= separated(a, b, cross(an, sub(a.p[end + (e + 1) % 3], a.p[end + e])));
                    safe |= separated(a, b, cross(bn, sub(b.p[end + (e + 1) % 3], b.p[end + e])));
                }
                for (unsigned e = 0; e < 3 && !safe; ++e)
                    for (unsigned f = 0; f < 3 && !safe; ++f)
                        safe |= separated(a, b,
                                          cross(sub(a.p[end + (e + 1) % 3], a.p[end + e]),
                                                sub(b.p[end + (f + 1) % 3], b.p[end + f])));
            }
            if (!safe)
                return false;
        }
    }
    return true;
}
std::shared_ptr<ChunkMesh> SurfaceHierarchy::build(const ChunkMesh& input, unsigned step,
                                                   const std::shared_ptr<const ChunkMesh>& previous,
                                                   float previousMorph,
                                                   const std::atomic<bool>* cancel) {
    if (step != 2 && step != 4 && step != 8)
        throw std::invalid_argument("surface hierarchy cell step");
    TraceScope trace(TracePhase::Simplification, input.coord);
    auto mesh = std::make_shared<ChunkMesh>(input);
    mesh->heightmap = false;
    mesh->surface3D = true;
    mesh->lod = std::uint8_t(step);
    decode(input.vertices, mesh->surfaceVertices);
    mesh->surfaceIndices = input.indices;
    decode(input.cutoutVertices, mesh->plantVertices);
    // Existing flower vertices follow decoded cutout cubes in the same double-sided pass.
    auto prefix = std::uint32_t(input.cutoutVertices.size());
    for (auto v : input.plantVertices) {
        v.anchor = anchor(v.x, v.y, v.z);
        mesh->plantVertices.push_back(v);
    }
    mesh->plantIndices = input.cutoutIndices;
    for (auto i : input.plantIndices)
        mesh->plantIndices.push_back(i + prefix);
    for (auto& v : mesh->translucentVertices)
        v.anchor = anchor(v.x, v.y, v.z);
    mesh->vertices.clear();
    mesh->indices.clear();
    mesh->cutoutVertices.clear();
    mesh->cutoutIndices.clear();
    mesh->heightVertices.clear();
    std::unordered_map<unsigned, unsigned> byAnchor;
    std::vector<Node> nodes;
    std::vector<unsigned> renderToNode;
    for (auto& v : mesh->surfaceVertices) {
        auto [it, inserted] = byAnchor.emplace(v.anchor, unsigned(nodes.size()));
        if (inserted) {
            Node n;
            n.original = n.p = position(v);
            n.parent = unsigned(nodes.size());
            n.layer = v.layer;
            n.locked = v.x <= 1 || v.x >= 15 || v.z <= 1 || v.z >= 15 || v.y <= 3 || v.y >= 248;
            nodes.push_back(n);
        } else if (nodes[it->second].layer != v.layer)
            nodes[it->second].locked = true;
        renderToNode.push_back(it->second);
    }
    for (auto& v : mesh->plantVertices)
        if (auto it = byAnchor.find(v.anchor); it != byAnchor.end())
            nodes[it->second].locked = true;
    for (auto& v : mesh->translucentVertices)
        if (auto it = byAnchor.find(v.anchor); it != byAnchor.end())
            nodes[it->second].locked = true;
    std::vector<Triangle> triangles;
    for (std::size_t i = 0; i + 2 < mesh->surfaceIndices.size(); i += 3) {
        Triangle t{{renderToNode[mesh->surfaceIndices[i]],
                    renderToNode[mesh->surfaceIndices[i + 1]],
                    renderToNode[mesh->surfaceIndices[i + 2]]}};
        auto n =
            cross(sub(nodes[t.v[1]].p, nodes[t.v[0]].p), sub(nodes[t.v[2]].p, nodes[t.v[0]].p));
        double length = std::sqrt(dot(n, n));
        if (length > 1e-8) {
            n = mul(n, 1 / length);
            double d = -dot(n, nodes[t.v[0]].p);
            for (auto id : t.v)
                nodes[id].q.plane(n, d);
        }
        for (auto id : t.v)
            nodes[id].triangles.push_back(unsigned(triangles.size()));
        triangles.push_back(t);
    }
    auto bucket = [](int x, int y, int z) {
        return unsigned(x + 4) | (unsigned(y + 4) << 8) | (unsigned(z + 4) << 16);
    };
    std::unordered_map<unsigned, std::vector<unsigned>> spatial;
    // Original triangle bounds expanded by the maximum parent-cell displacement.
    for (unsigned i = 0; i < triangles.size(); ++i) {
        auto& t = triangles[i];
        V lo = nodes[t.v[0]].original, hi = lo;
        for (auto id : t.v)
            for (int a = 0; a < 3; ++a) {
                lo[a] = std::min(lo[a], nodes[id].original[a]);
                hi[a] = std::max(hi[a], nodes[id].original[a]);
            }
        for (int z = int(std::floor((lo[2] - 8) / 8)); z <= int(std::floor((hi[2] + 8) / 8)); ++z)
            for (int y = int(std::floor((lo[1] - 8) / 8)); y <= int(std::floor((hi[1] + 8) / 8));
                 ++y)
                for (int x = int(std::floor((lo[0] - 8) / 8));
                     x <= int(std::floor((hi[0] + 8) / 8)); ++x)
                    spatial[bucket(x, y, z)].push_back(i);
    }
    // Freeze all endpoints and interior points at greedy T-junctions.
    for (auto& t : triangles)
        for (int e = 0; e < 3; ++e) {
            auto a = t.v[e], b = t.v[(e + 1) % 3];
            auto delta = sub(nodes[b].p, nodes[a].p);
            int count = std::gcd(std::gcd(std::abs(int(delta[0])), std::abs(int(delta[1]))),
                                 std::abs(int(delta[2])));
            for (int k = 1; k < count; ++k) {
                auto p = add(nodes[a].p, mul(delta, double(k) / count));
                auto key = anchor(float(p[0]), float(p[1]), float(p[2]));
                if (auto it = byAnchor.find(key); it != byAnchor.end()) {
                    nodes[a].locked = nodes[b].locked = nodes[it->second].locked = true;
                }
            }
        }
    // A closed manifold vertex has a single cyclic link. Ambiguous/boundary fans stay fine.
    for (unsigned id = 0; id < nodes.size(); ++id)
        if (!nodes[id].locked) {
            std::unordered_map<unsigned, std::vector<unsigned>> link;
            for (auto tri : nodes[id].triangles) {
                auto v = triangles[tri].v;
                std::vector<unsigned> others;
                for (auto n : v)
                    if (n != id)
                        others.push_back(n);
                if (others.size() == 2) {
                    link[others[0]].push_back(others[1]);
                    link[others[1]].push_back(others[0]);
                }
            }
            bool cyclic = !link.empty();
            for (auto& [n, neighbors] : link)
                cyclic &= neighbors.size() == 2;
            std::unordered_set<unsigned> reached;
            if (cyclic) {
                std::vector<unsigned> todo{link.begin()->first};
                while (!todo.empty()) {
                    auto n = todo.back();
                    todo.pop_back();
                    if (!reached.insert(n).second)
                        continue;
                    for (auto other : link[n])
                        todo.push_back(other);
                }
            }
            if (!cyclic || reached.size() != link.size())
                nodes[id].locked = true;
        }
    struct Edge {
        unsigned a, b;
        double cost;
        V target;
    };
    struct Collapse {
        unsigned a, b;
        V target;
    };
    std::vector<Collapse> history;
    for (int pass = 0; pass < 4; ++pass) {
        if (cancel && *cancel)
            return {};
        std::vector<Edge> edges;
        std::unordered_set<std::uint64_t> seen;
        for (auto& t : triangles)
            if (!t.removed)
                for (int e = 0; e < 3; ++e) {
                    auto a = root(nodes, t.v[e]), b = root(nodes, t.v[(e + 1) % 3]);
                    if (a > b)
                        std::swap(a, b);
                    if (a == b || nodes[a].locked || nodes[b].locked ||
                        nodes[a].layer != nodes[b].layer || !sameCell(nodes[a].p, nodes[b].p, step))
                        continue;
                    if (!seen.insert((std::uint64_t(a) << 32) | b).second)
                        continue;
                    auto q = nodes[a].q + nodes[b].q;
                    V target = nodes[a].p;
                    double cost = q.cost(target);
                    for (auto p : {nodes[b].p, mul(add(nodes[a].p, nodes[b].p), 0.5)})
                        if (q.cost(p) < cost) {
                            target = p;
                            cost = q.cost(p);
                        }
                    V solved{};
                    if (q.solve(solved) && sameCell(solved, nodes[a].p, step) &&
                        q.cost(solved) < cost) {
                        target = solved;
                        cost = q.cost(solved);
                    }
                    if (cost <= 0.25 * step * step)
                        edges.push_back({a, b, cost, target});
                }
        std::sort(edges.begin(), edges.end(), [](auto& a, auto& b) {
            return a.cost != b.cost ? a.cost < b.cost : a.a != b.a ? a.a < b.a : a.b < b.b;
        });
        unsigned accepted = 0;
        for (auto edge : edges) {
            auto a = root(nodes, edge.a), b = root(nodes, edge.b);
            if (a != edge.a || b != edge.b || nodes[a].locked || nodes[b].locked)
                continue;
            std::unordered_set<unsigned> na, nb, opposite, affected;
            unsigned edgeFaces = 0;
            for (auto id : nodes[a].triangles)
                affected.insert(id);
            for (auto id : nodes[b].triangles)
                affected.insert(id);
            for (auto id : affected) {
                auto& t = triangles[id];
                if (t.removed)
                    continue;
                std::array<unsigned, 3> v;
                for (int i = 0; i < 3; ++i)
                    v[i] = root(nodes, t.v[i]);
                bool hasA = std::find(v.begin(), v.end(), a) != v.end(),
                     hasB = std::find(v.begin(), v.end(), b) != v.end();
                if (hasA)
                    for (auto n : v)
                        if (n != a)
                            na.insert(n);
                if (hasB)
                    for (auto n : v)
                        if (n != b)
                            nb.insert(n);
                if (hasA && hasB) {
                    ++edgeFaces;
                    for (auto n : v)
                        if (n != a && n != b)
                            opposite.insert(n);
                }
            }
            std::unordered_set<unsigned> common;
            for (auto n : na)
                if (nb.count(n))
                    common.insert(n);
            bool safe = edgeFaces == 2 && common == opposite;
            std::unordered_set<std::uint64_t> surviving;
            for (auto id : affected)
                if (safe && !triangles[id].removed) {
                    auto t = triangles[id].v;
                    for (auto& v : t) {
                        v = root(nodes, v);
                        if (v == b)
                            v = a;
                    }
                    if (t[0] == t[1] || t[1] == t[2] || t[0] == t[2])
                        continue;
                    std::sort(t.begin(), t.end());
                    auto key = (std::uint64_t(t[0]) << 42) | (std::uint64_t(t[1]) << 21) | t[2];
                    if (!surviving.insert(key).second)
                        safe = false;
                }
            auto q = nodes[a].q + nodes[b].q;
            V target = edge.target;
            safe &= sameCell(target, nodes[a].p, step) && sameCell(target, nodes[b].p, step) &&
                    q.cost(target) <= 0.25 * step * step;
            std::vector<std::array<V, 3>> changed;
            for (auto id : affected)
                if (safe) {
                    auto& t = triangles[id];
                    if (t.removed)
                        continue;
                    std::array<unsigned, 3> v;
                    std::array<V, 3> old{}, now{};
                    for (int k = 0; k < 3; ++k) {
                        v[k] = root(nodes, t.v[k]);
                        old[k] = nodes[v[k]].p;
                        now[k] = (v[k] == a || v[k] == b) ? target : old[k];
                    }
                    if (std::find(v.begin(), v.end(), a) != v.end() &&
                        std::find(v.begin(), v.end(), b) != v.end())
                        continue;
                    auto n = cross(sub(old[1], old[0]), sub(old[2], old[0])),
                         nn = cross(sub(now[1], now[0]), sub(now[2], now[0]));
                    if (dot(nn, nn) < 1e-12 || dot(n, nn) <= 0)
                        safe = false;
                    changed.push_back(now);
                }
            // Reject local new intersections with triangles not sharing the collapse fan.
            std::unordered_set<unsigned> nearby;
            if (safe)
                for (auto& c : changed) {
                    V lo = c[0], hi = c[0];
                    for (auto& p : c)
                        for (int a = 0; a < 3; ++a) {
                            lo[a] = std::min(lo[a], p[a]);
                            hi[a] = std::max(hi[a], p[a]);
                        }
                    for (int z = int(std::floor(lo[2] / 8)); z <= int(std::floor(hi[2] / 8)); ++z)
                        for (int y = int(std::floor(lo[1] / 8)); y <= int(std::floor(hi[1] / 8));
                             ++y)
                            for (int x = int(std::floor(lo[0] / 8));
                                 x <= int(std::floor(hi[0] / 8)); ++x)
                                if (auto it = spatial.find(bucket(x, y, z)); it != spatial.end())
                                    nearby.insert(it->second.begin(), it->second.end());
                }
            if (safe)
                for (auto id : nearby)
                    if (safe && !affected.count(id) && !triangles[id].removed) {
                        auto& t = triangles[id];
                        std::array<V, 3> p;
                        bool shares = false;
                        for (int k = 0; k < 3; ++k) {
                            auto r = root(nodes, t.v[k]);
                            p[k] = nodes[r].p;
                            shares |= na.count(r) || nb.count(r);
                        }
                        if (shares)
                            continue;
                        for (auto& c : changed)
                            if (intersects(c, p)) {
                                safe = false;
                                break;
                            }
                    }
            if (!safe) {
                ++mesh->collapseRejected;
                continue;
            }
            history.push_back({a, b, target});
            nodes[b].parent = a;
            nodes[a].p = target;
            nodes[a].q = q;
            nodes[a].triangles.insert(nodes[a].triangles.end(), nodes[b].triangles.begin(),
                                      nodes[b].triangles.end());
            for (auto id : affected) {
                auto& t = triangles[id];
                for (auto& v : t.v)
                    v = root(nodes, v);
                if (t.v[0] == t.v[1] || t.v[1] == t.v[2] || t.v[0] == t.v[2])
                    t.removed = true;
            }
            ++mesh->collapseAccepted;
            ++accepted;
        }
        if (!accepted)
            break;
    }
    std::unordered_map<unsigned, V> previousPositions;
    if (previous && previous->surface3D && previous->sourceVersion == mesh->sourceVersion)
        for (auto& v : previous->surfaceVertices)
            previousPositions[v.anchor] = {v.fromX + (v.x - v.fromX) * previousMorph,
                                           v.fromY + (v.y - v.fromY) * previousMorph,
                                           v.fromZ + (v.z - v.fromZ) * previousMorph};
    for (unsigned i = 0; i < mesh->surfaceVertices.size(); ++i) {
        auto& v = mesh->surfaceVertices[i];
        auto id = root(nodes, renderToNode[i]);
        auto p = nodes[id].p;
        v.x = float(p[0]);
        v.y = float(p[1]);
        v.z = float(p[2]);
        if (auto it = previousPositions.find(v.anchor); it != previousPositions.end()) {
            v.fromX = float(it->second[0]);
            v.fromY = float(it->second[1]);
            v.fromZ = float(it->second[2]);
        }
    }
    auto acceptedCount = history.size();
    while (!validMorph(*mesh) && acceptedCount) {
        acceptedCount /= 2;
        for (unsigned i = 0; i < nodes.size(); ++i) {
            nodes[i].parent = i;
            nodes[i].p = nodes[i].original;
        }
        for (std::size_t i = 0; i < acceptedCount; ++i) {
            auto& c = history[i];
            nodes[c.b].parent = c.a;
            nodes[c.a].p = c.target;
        }
        for (unsigned i = 0; i < mesh->surfaceVertices.size(); ++i) {
            auto& v = mesh->surfaceVertices[i];
            auto p = nodes[root(nodes, renderToNode[i])].p;
            v.x = float(p[0]);
            v.y = float(p[1]);
            v.z = float(p[2]);
        }
    }
    mesh->collapseRejected += history.size() - acceptedCount;
    mesh->collapseAccepted = std::uint32_t(acceptedCount);
    if (!validMorph(*mesh)) {
        // An interrupted refinement may be uncertain: retain the geometry actually
        // displayed, not an instantaneous jump to its original fine endpoints.
        mesh->collapseRejected += mesh->collapseAccepted;
        mesh->collapseAccepted = 0;
        for (auto& v : mesh->surfaceVertices) {
            v.x = v.fromX;
            v.y = v.fromY;
            v.z = v.fromZ;
        }
    }
    mesh->coarseIndices.clear();
    for (std::size_t i = 0; i + 2 < mesh->surfaceIndices.size(); i += 3) {
        auto a = position(mesh->surfaceVertices[mesh->surfaceIndices[i]]),
             b = position(mesh->surfaceVertices[mesh->surfaceIndices[i + 1]]),
             c = position(mesh->surfaceVertices[mesh->surfaceIndices[i + 2]]);
        auto n = cross(sub(b, a), sub(c, a));
        if (dot(n, n) > 1e-12)
            mesh->coarseIndices.insert(mesh->coarseIndices.end(), mesh->surfaceIndices.begin() + i,
                                       mesh->surfaceIndices.begin() + i + 3);
    }
    for (auto& n : nodes)
        mesh->refinedVertices += n.locked;
    std::map<std::array<std::uint32_t, 6>, std::uint32_t> compact;
    for (auto i : mesh->coarseIndices) {
        auto vertex = mesh->surfaceVertices[i];
        std::array<std::uint32_t, 6> key{};
        std::memcpy(&key[0], &vertex.x, 4);
        std::memcpy(&key[1], &vertex.y, 4);
        std::memcpy(&key[2], &vertex.z, 4);
        key[3] = vertex.layer;
        key[4] = vertex.face;
        key[5] = vertex.ao;
        auto [it, inserted] = compact.emplace(key, std::uint32_t(mesh->compactVertices.size()));
        if (inserted) {
            vertex.fromX = vertex.x;
            vertex.fromY = vertex.y;
            vertex.fromZ = vertex.z;
            mesh->compactVertices.push_back(vertex);
        }
        mesh->compactIndices.push_back(it->second);
    }
    std::vector<ChunkMeshVertex>().swap(mesh->vertices);
    std::vector<ChunkMeshVertex>().swap(mesh->cutoutVertices);
    std::vector<std::uint32_t>().swap(mesh->indices);
    std::vector<std::uint32_t>().swap(mesh->cutoutIndices);
    return mesh;
}
} // namespace bloxide
