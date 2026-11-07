"""Read planar ACIS REGION faces, including their holes, from an ASCII DXF.

AutoCAD's newer binary transforms use typed vectors instead of the literal
matrix strings supported by ezdxf 1.4.5. This adapter supports both encodings.
It changes no installed library files and refuses unsupported surface types.
"""
from collections import Counter
from ezdxf.acis import entities as acis, sab
from ezdxf.math import Vec3
from shapely import constrained_delaunay_triangles
from shapely.geometry import Polygon


def dxf_records(path):
    """Yield (section, entity type, tags) without loading the whole drawing."""
    with open(path, encoding='utf8', errors='replace') as stream:
        section = ''; kind = ''; attrs = []
        while True:
            code = stream.readline()
            if not code: break
            value = stream.readline().strip()
            code = int(code)
            if code == 0:
                if kind == 'SECTION': section = dict(attrs).get(2, '')
                if kind: yield section, kind, attrs
                kind, attrs = value, []
            else: attrs.append((code, value))
        if kind: yield section, kind, attrs


class _TypedTransformLoader(sab.SabDataLoader):
    def read_transform(self):
        if self.data[self.index].tag in (19, 20):
            return [value for _ in range(4) for value in self.read_vec3()]
        return super().read_transform()


class _BodyLoader(acis.SabLoader):
    def make_data_loader(self, data):
        return _TypedTransformLoader(data, self.version)


def _chain(first, attribute):
    seen = set(); current = first
    while not current.is_none:
        if id(current) in seen: raise ValueError(f'Broken ACIS {attribute} chain')
        seen.add(id(current)); yield current
        current = getattr(current, attribute)


def triangulate_rings(rings):
    """Triangulate a planar 3D face with concavities and inner loops intact."""
    if not rings or len(rings[0]) < 3: raise ValueError('Missing ACIS face boundary')
    origin = Vec3(rings[0][0]); direction = None; normal = None
    for value in rings[0][1:]:
        edge = Vec3(value) - origin
        if edge.magnitude < 1e-8: continue
        if direction is None: direction = edge.normalize()
        cross = direction.cross(edge)
        if cross.magnitude > 1e-8: normal = cross.normalize(); break
    if normal is None: return []  # a zero-area seam
    side = normal.cross(direction).normalize()
    surface = Polygon()
    for ring in rings:
        points = [(direction.dot(Vec3(p)-origin), side.dot(Vec3(p)-origin)) for p in ring]
        polygon = Polygon(points)
        if polygon.area < 1e-8: continue
        if not polygon.is_valid: raise ValueError('Invalid ACIS boundary polygon')
        # The even/odd rule supports holes and disconnected outer boundaries.
        surface = surface.symmetric_difference(polygon)
    triangles = []
    for tri in constrained_delaunay_triangles(surface).geoms:
        triangles.append([tuple(origin + direction*x + side*y) for x,y in list(tri.exterior.coords)[:3]])
    return triangles


def region_mesh(data, stats=None):
    if stats is None: stats = Counter()
    vertices = []; faces = []
    for body in _BodyLoader.load(data):
        matrix = None if body.transform.is_none else body.transform.matrix
        for lump in _chain(body.lump, 'next_lump'):
            for shell in _chain(lump.shell, 'next_shell'):
                if not shell.subshell.is_none: raise ValueError('Unsupported ACIS subshell')
                for face in _chain(shell.face, 'next_face'):
                    if face.surface.type != 'plane-surface':
                        raise ValueError(f'Unsupported ACIS surface {face.surface.type}')
                    rings = []
                    for loop in _chain(face.loop, 'next_loop'):
                        coedges = []; seen = set(); coedge = loop.coedge
                        while not coedge.is_none and id(coedge) not in seen:
                            seen.add(id(coedge)); coedges.append(coedge); coedge = coedge.next_coedge
                        if coedge is not loop.coedge: raise ValueError('Open ACIS face boundary')
                        ring = []
                        for item in coedges:
                            edge = item.edge
                            if edge.curve.type != 'straight-curve':
                                raise ValueError(f'Unsupported ACIS edge {edge.curve.type}')
                            vertex = edge.end_vertex if item.sense else edge.start_vertex
                            ring.append(tuple(vertex.point.location))
                        if matrix is not None: ring = [tuple(v) for v in matrix.transform_vertices(ring)]
                        rings.append(ring)
                    stats['region_planar_faces'] += 1
                    stats['region_inner_loops'] += max(0, len(rings)-1)
                    for triangle in triangulate_rings(rings):
                        index = len(vertices); vertices.extend(triangle); faces.append([index,index+1,index+2])
    stats['region_triangles'] += len(faces)
    return vertices, faces
