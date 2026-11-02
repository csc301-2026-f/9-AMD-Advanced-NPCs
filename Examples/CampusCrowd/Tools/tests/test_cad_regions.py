"""Regression checks for the ACIS faces omitted by the initial campus import."""
import sys, unittest
from pathlib import Path
from shapely.geometry import Polygon
from shapely.ops import unary_union
from ezdxf.acis.sab import Token, Tags
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from cad_regions import _TypedTransformLoader, triangulate_rings, region_mesh


class RegionTests(unittest.TestCase):
    def test_new_binary_transform_encoding(self):
        rows = [(1.,0.,0.),(0.,1.,0.),(0.,0.,1.),(10.,20.,30.)]
        loader = _TypedTransformLoader([Token(Tags.DIRECTION_VEC, row) for row in rows], 22300)
        self.assertEqual(loader.read_transform(), [value for row in rows for value in row])

    def test_legacy_transform_encoding(self):
        values = '1 0 0 0 1 0 0 0 1 10 20 30 1 no_rotate no_reflect no_shear '
        loader = _TypedTransformLoader([Token(Tags.LITERAL_STR, values)], 700)
        self.assertEqual(loader.read_transform(), [1,0,0,0,1,0,0,0,1,10,20,30])

    def test_concave_roof_does_not_fill_its_notch(self):
        ring = [(0,0,5),(4,0,5),(4,1,5),(1,1,5),(1,4,5),(0,4,5)]
        triangles = triangulate_rings([ring])
        actual = unary_union([Polygon([(x,y) for x,y,z in t]) for t in triangles])
        self.assertAlmostEqual(actual.area, 7)
        self.assertAlmostEqual(actual.symmetric_difference(Polygon([(x,y) for x,y,z in ring])).area, 0)

    def test_vertical_face_preserves_window_hole(self):
        outside = [(2,0,0),(2,4,0),(2,4,4),(2,0,4)]
        inside = [(2,1,1),(2,3,1),(2,3,3),(2,1,3)]
        triangles = triangulate_rings([outside, inside])
        actual = unary_union([Polygon([(y,z) for x,y,z in t]) for t in triangles])
        self.assertAlmostEqual(actual.area, 12)
        self.assertAlmostEqual(actual.intersection(Polygon([(1,1),(3,1),(3,3),(1,3)])).area, 0)
        self.assertTrue(all(abs(x-2)<1e-8 for t in triangles for x,y,z in t))

    def test_university_college_source_region(self):
        data = (Path(__file__).parent/'fixtures/university-college-roof.sab').read_bytes()
        vertices, faces = region_mesh(data)
        self.assertGreater(len(faces), 0)
        triangles = [Polygon([(vertices[i][0],vertices[i][1]) for i in face]) for face in faces]
        self.assertAlmostEqual(unary_union(triangles).area, 228.7734235, places=4)
        self.assertTrue(all(abs(p[2]-5.4929)<1e-5 for p in vertices))


if __name__ == '__main__': unittest.main()
