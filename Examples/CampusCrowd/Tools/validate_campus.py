"""Validate CAD geometry, source hash, and three-metre building clearance for every route."""
import json, math, hashlib
from pathlib import Path
from shapely.geometry import Polygon,LineString,Point,box
from shapely.ops import unary_union
from shapely.prepared import prep
project=Path(__file__).resolve().parents[1]
data=json.loads((project/'Content/Campus/campus.json').read_text())
assert data['source_sha256']==hashlib.sha256((project.parents[1]/'50H_NORTH.dwg').read_bytes()).hexdigest()
triangles=0; roof_polys=[]
for part in data['sections']:
 v=part['vertices']; t=part['triangles']
 assert len(t)%3==0 and min(t)>=0 and max(t)<len(v)
 assert all(len(p)==3 and all(math.isfinite(c) for c in p) for p in v)
 triangles+=len(t)//3
 if part['name'] not in ('walls','roofs'):continue
 for a,b,c in zip(t[::3],t[1::3],t[2::3]):
  points=[v[a],v[b],v[c]]
  if max(p[2] for p in points)<=200:continue
  poly=Polygon([(p[0]/100,p[1]/100) for p in points])
  if poly.area>.02:roof_polys.append(poly)
buildings=unary_union(roof_polys); obstacles=prep(buildings.buffer(3.0))
audit=data['entity_audit']
assert audit['decoded_regions']==audit['source_regions']==19526
# Source-CAD bounds around University College. This fails on the initial mesh-only import.
uc_area=buildings.intersection(box(-85,163,65,285)).area
assert uc_area>4000, ('University College roof footprint is incomplete', uc_area)
nodes=data['nodes']; edges=0
for i,node in enumerate(nodes):
 assert node['links'] and all(0<=j<len(nodes) and i in nodes[j]['links'] for j in node['links'])
 a=[c/100 for c in node['position']]
 assert not obstacles.intersects(Point(a)),('node inside building clearance',i)
 for j in node['links']:
  if j<=i:continue
  b=[c/100 for c in nodes[j]['position']]
  assert not obstacles.intersects(LineString([a,b])),('edge intersects building clearance',i,j)
  edges+=1
result={'source_hash_matches':True,'geometry_finite_and_indices_valid':True,'triangles':triangles,'route_nodes':len(nodes),'route_edges':edges,'decoded_source_regions':audit['decoded_regions'],'university_college_projected_roof_area_m2':round(uc_area,2),'all_routes_clear_of_cad_buildings_by_metres':3.0,'all_links_reciprocal_and_nonisolated':True}
(project/'Evidence/geometry-validation.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
