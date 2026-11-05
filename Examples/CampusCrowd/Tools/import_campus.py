#!/usr/bin/env python3
"""Extract CAD polyface/planar REGION massings and landscape from an ODA ASCII DXF.
Usage: python import_campus.py /path/to/50H_NORTH.dxf
Dependencies: see requirements.txt. The original DWG is never changed.
"""
import argparse, collections, hashlib, json, math
from pathlib import Path
from shapely.geometry import Polygon, Point, LineString, box
from shapely.ops import unary_union
from shapely.prepared import prep
from cad_regions import dxf_records, region_mesh

PROJECT = Path(__file__).resolve().parents[1]
# Toronto MTM zone 10 metres, centred on the St George campus.
ORIGIN = (313210.0, 4835890.0)
CROP = (-650., -700., 650., 700.)
COLORS = {'ground': (.10,.13,.15), 'Curbs': (.47,.49,.47), 'Parks': (.15,.28,.16),
          'walls': (.62,.52,.39), 'roofs': (.23,.29,.31), 'paths': (.64,.58,.43),
          'trunks': (.24,.17,.10), 'leaves': (.09,.22,.12), 'markings': (.78,.70,.46)}

def entities(path):
    for section, kind, attrs in dxf_records(path):
        if section == 'ENTITIES': yield kind, attrs


def meshes(path, audit):
    verts = []; faces = []; layer = None; regions = {}
    for section, kind, attrs in dxf_records(path):
        d = dict(attrs)
        if section == 'ACDSDATA' and kind == 'ACDSRECORD':
            handle = d.get(320)
            if handle in regions:
                payload = bytes.fromhex(''.join(v for c,v in attrs if c == 310))
                try:
                    points, triangles = region_mesh(payload, audit)
                except Exception as error:
                    raise ValueError(f'Could not tessellate REGION {handle}: {error}') from error
                audit['decoded_regions'] += 1
                yield regions[handle], points, triangles
            continue
        if section != 'ENTITIES': continue
        if kind == 'REGION' and d.get(8) == 'Massings':
            regions[d[5]] = d[8]; audit['source_regions'] += 1
        elif kind == 'POLYLINE':
            verts, faces = [], []
            layer = d.get(8) if int(d.get(70, 0)) & 64 else None
        elif kind == 'VERTEX' and layer:
            if int(d.get(70,0)) == 128:
                face = [abs(int(d[k]))-1 for k in (71,72,73,74) if k in d and int(d[k])]
                if len(face) >= 3: faces.append(face)
            else: verts.append(tuple(float(d.get(k,0)) for k in (10,20,30)))
        elif kind == 'SEQEND' and layer:
            yield layer, verts, faces
            layer = None
        elif kind == '3DFACE':
            vs = [tuple(float(d.get(k+i,0)) for k in (10,20,30)) for i in range(4)]
            if vs[2] == vs[3]: vs.pop()
            yield d.get(8), vs, [list(range(len(vs)))]
    if audit['decoded_regions'] != audit['source_regions']:
        raise ValueError('Some source REGION records have no decoded ACIS payload')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('dxf', type=Path)
    args = parser.parse_args()
    sections = {k: {'name': k, 'color': c, 'vertices': [], 'triangles': []} for k,c in COLORS.items()}
    roofs = []; sidewalks = []; parks = []; counts = collections.Counter(); audit = collections.Counter()
    def add_triangle(key, points):
        sec = sections[key]; i = len(sec['vertices'])
        # Reflect north into Unreal -Y. Reverse winding to retain outward faces.
        sec['vertices'].extend([[round(p[0]*100,2),round(-p[1]*100,2),round(p[2]*100,2)] for p in points])
        sec['triangles'].extend([i,i+2,i+1])
    def quad(key, a,b,c,d):
        add_triangle(key,[a,b,c]); add_triangle(key,[a,c,d])
    quad('ground',(-720,-770,-.15),(720,-770,-.15),(720,770,-.15),(-720,770,-.15))
    for layer, verts, faces in meshes(args.dxf, audit):
        if layer not in ('Massings','Curbs','Parks') or not verts: continue
        local = [(x-ORIGIN[0],y-ORIGIN[1],z) for x,y,z in verts]
        xs = [p[0] for p in local]; ys = [p[1] for p in local]
        if max(xs)<CROP[0] or min(xs)>CROP[2] or max(ys)<CROP[1] or min(ys)>CROP[3]: continue
        counts[layer] += 1
        for face in faces:
            for j in range(1,len(face)-1):
                if max(face) >= len(local): continue
                tri = [local[face[k]] for k in (0,j,j+1)]
                a,b,c = tri
                cross = ((b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]))
                projected = abs(cross)*.5
                if layer == 'Massings':
                    key = 'roofs' if projected > .001 and max(p[2] for p in tri)-min(p[2] for p in tri)<2.0 else 'walls'
                    if projected > .02 and max(p[2] for p in tri)>2: roofs.append(Polygon([(p[0],p[1]) for p in tri]))
                else:
                    key=layer
                    # Keep landscape above the road plane.
                    tri=[(x,y,.03 if layer=='Curbs' else .05) for x,y,z in tri]
                    if projected > .02:
                        (sidewalks if layer=='Curbs' else parks).append(Polygon([(p[0],p[1]) for p in tri]))
                add_triangle(key,tri)
    print('CAD mesh objects:', dict(counts), flush=True)
    buildings = unary_union(roofs).buffer(.08).buffer(-.08)
    if buildings.geom_type == 'Polygon': building_parts=[buildings]
    else: building_parts=list(buildings.geoms)
    building_parts=[p for p in building_parts if p.area > 12]
    buildings=unary_union(building_parts)
    sidewalk=unary_union(sidewalks).buffer(.2)
    park=unary_union(parks)
    # A medial network follows the centres of free campus surfaces rather than filling
    # lawns with a navigation grid. Road-separated precincts remain independent.
    import numpy as np
    import shapely
    from skimage.morphology import medial_axis
    resolution=2.
    xs=np.arange(-600.,602.,resolution); ys=np.arange(-650.,652.,resolution)
    xx,yy=np.meshgrid(xs,ys)
    free=sidewalk.union(park).buffer(.2).difference(buildings.buffer(3.5))
    mask=shapely.contains_xy(free,xx,yy)
    skeleton=medial_axis(mask,rng=50)
    pixels={tuple(map(int,p)) for p in np.argwhere(skeleton)}
    neighbours={}
    for row,col in pixels:
        ns=[]
        for dr,dc in [(-1,0),(1,0),(0,-1),(0,1),(-1,-1),(-1,1),(1,-1),(1,1)]:
            n=(row+dr,col+dc)
            if n not in pixels: continue
            # Suppress diagonal shortcuts around an existing orthogonal connection.
            if dr and dc and ((row+dr,col) in pixels or (row,col+dc) in pixels):continue
            ns.append(n)
        neighbours[(row,col)]=ns
    # Compress pixel chains, preserving junctions and a point every ~8 metres.
    anchors={p for p,ns in neighbours.items() if len(ns)!=2}
    visited=set(); routes=[]
    for start in sorted(anchors):
        for nxt in neighbours[start]:
            if (start,nxt) in visited:continue
            chain=[start];previous=start;current=nxt
            while True:
                visited.add((previous,current));visited.add((current,previous));chain.append(current)
                if current in anchors:break
                opts=[p for p in neighbours[current] if p!=previous]
                if not opts:break
                previous,current=current,opts[0]
            if len(chain)>=2:routes.append(chain)
    route_obstacles=prep(buildings.buffer(3.2))
    positions={};links=collections.defaultdict(set)
    def node(pixel):
        if pixel not in positions: positions[pixel]=len(positions)
        return positions[pixel]
    def xy(pixel):return (float(xs[pixel[1]]),float(ys[pixel[0]]))
    for chain in routes:
        # Short terminal spurs are CAD edge noise, not useful destinations.
        if len(chain)<7 and (len(neighbours[chain[0]])==1 or len(neighbours[chain[-1]])==1):continue
        selected=chain[::4]
        if selected[-1]!=chain[-1]:selected.append(chain[-1])
        for a,b in zip(selected,selected[1:]):
            if route_obstacles.intersects(LineString([xy(a),xy(b)])):continue
            ia,ib=node(a),node(b);links[ia].add(ib);links[ib].add(ia)
    unseen=set(links);groups=[]
    while unseen:
        stack=[unseen.pop()];group=[]
        while stack:
            n=stack.pop();group.append(n)
            for n2 in links[n]:
                if n2 in unseen:unseen.remove(n2);stack.append(n2)
        groups.append(group)
    kept={n for group in groups if len(group)>=8 for n in group}
    assert kept,'No substantial pedestrian precinct found'
    remap={old:i for i,old in enumerate(sorted(kept))}
    reverse={v:xy(k) for k,v in positions.items()}
    nodes=[{'position':[reverse[old][0]*100,-reverse[old][1]*100],
            'links':[remap[n] for n in sorted(links[old]) if n in kept]} for old in sorted(kept)]
    route_lines=[LineString([reverse[old],reverse[n]]) for old in kept for n in links[old] if n>old and n in kept]
    route_clearance=prep(unary_union(route_lines).buffer(3.5))
    # Modest procedural landscaping; locations must be within CAD park surfaces.
    import random
    rng=random.Random(50); park_test=prep(park); tree_count=0
    for _ in range(1100):
        x,y=rng.uniform(-600,600),rng.uniform(-650,650)
        if not park_test.contains(Point(x,y)) or route_obstacles.intersects(Point(x,y)):continue
        # Avoid the pedestrian routes, allowing clear views of moving students.
        if route_clearance.intersects(Point(x,y)):continue
        r=rng.uniform(2.5,4.5);h=rng.uniform(7,11);tree_count+=1
        for j in range(6):
            a=j*math.tau/6;b=(j+1)*math.tau/6
            add_triangle('leaves',[(x+math.cos(a)*r,y+math.sin(a)*r,h*.5),(x+math.cos(b)*r,y+math.sin(b)*r,h*.5),(x,y,h)])
            quad('trunks',(x+math.cos(a)*.22,y+math.sin(a)*.22,0),(x+math.cos(b)*.22,y+math.sin(b)*.22,0),(x+math.cos(b)*.22,y+math.sin(b)*.22,h*.6),(x+math.cos(a)*.22,y+math.sin(a)*.22,h*.6))
    source_dwg=PROJECT.parents[1]/'50H_NORTH.dwg'
    result={'source': '50H_NORTH.dwg', 'source_sha256': hashlib.sha256(source_dwg.read_bytes()).hexdigest(),
            'units':'Unreal centimetres', 'cad_origin_m':ORIGIN,'cad_crop_m':CROP,
            'transform':'X=(CAD Easting-313210)*100; Y=-(CAD Northing-4835890)*100; Z=CAD Z*100',
            'extent':85000,'building_count':len(building_parts),'cad_mesh_counts':dict(counts),
            'entity_audit':dict(audit),'tree_count':tree_count,'nodes':nodes,'sections':[s for s in sections.values() if s['triangles']]}
    output=PROJECT/'Content/Campus';output.mkdir(parents=True,exist_ok=True)
    (output/'campus.json').write_text(json.dumps(result,separators=(',',':'))+'\n')
    print('Buildings:',len(building_parts),'Pedestrian nodes:',len(nodes),'Trees:',tree_count,flush=True)
    # Export plan for checking the imported geometry and pedestrian network.
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    from matplotlib.collections import PatchCollection,LineCollection
    from matplotlib.patches import Polygon as PlotPolygon
    fig,ax=plt.subplots(figsize=(13,14),facecolor='#141d22');ax.set_facecolor('#222e31')
    for polys,color in [(sidewalks,'#9b9c90'),(parks,'#48694d'),(building_parts,'#d5b98c')]:
        ax.add_collection(PatchCollection([PlotPolygon(p.exterior.coords) for p in polys],facecolor=color,edgecolor='none'))
    segments=[]
    for i,node in enumerate(nodes):
        a=node['position']
        for j in node['links']:
            if j>i:
                b=nodes[j]['position'];segments.append([(a[0]/100,-a[1]/100),(b[0]/100,-b[1]/100)])
    ax.add_collection(LineCollection(segments,colors='#6fe3d4',linewidths=.5,alpha=.7))
    ax.set(xlim=(-680,680),ylim=(-730,730),aspect='equal',xlabel='East / metres from CAD origin',ylabel='North / metres from CAD origin')
    ax.set_title('50H NORTH / ST GEORGE CAMPUS\nActual CAD massings, sidewalks and park surfaces · pedestrian demo network',color='white',pad=20)
    ax.tick_params(colors='#bbc7cc');ax.xaxis.label.set_color('#bbc7cc');ax.yaxis.label.set_color('#bbc7cc')
    fig.tight_layout();fig.savefig(output/'campus-plan.png',dpi=140);plt.close(fig)
    metadata={k:v for k,v in result.items() if k not in ('sections','nodes')}
    metadata['route_nodes']=len(nodes);metadata['triangles']={s['name']:len(s['triangles'])//3 for s in result['sections']}
    (output/'import-report.json').write_text(json.dumps(metadata,indent=2)+'\n')

if __name__=='__main__':main()
