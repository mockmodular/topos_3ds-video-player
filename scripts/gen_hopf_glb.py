# gen_hopf_glb.py
# hopf_spiral_15deg.html -> .glb -> pycgfx -> CGFX
# 用法: python scripts/gen_hopf_glb.py

import math, struct, zlib, os, sys, json, base64, subprocess

PY = r"C:/Users/casts/AppData/Local/Programs/Python/Python312/python.exe"
PYCGFX = os.path.join(os.path.dirname(__file__), "pycgfx", "main.py")
OUT_DIR = os.path.join(os.path.dirname(__file__), "..", "resource", "banner_model")
os.makedirs(OUT_DIR, exist_ok=True)
GLB_PATH  = os.path.join(OUT_DIR, "hopf_spiral.glb")
CGFX_PATH = os.path.join(OUT_DIR, "hopf_spiral.cgfx")

# ── 参数 ─────────────────────────────────────────────────────────
N_FIBERS   = 10
TUBE_SEGS  = 24     # more segments -> smoother curves
TUBE_SIDES = 8      # more sides -> rounder tubes
TUBE_R     = 0.045  # slightly thinner to keep visual balance
SCALE      = 2.0
MODEL_SCALE = 2.7   # 3.0 * 0.9

# ── 数学 ─────────────────────────────────────────────────────────
def hopf_pt(theta, phi, t):
    eta = theta / 2.0
    c1 = math.cos(eta)*math.cos(t)
    c2 = math.cos(eta)*math.sin(t)
    c3 = math.sin(eta)*math.cos(phi+t)
    c4 = math.sin(eta)*math.sin(phi+t)
    d  = 1.0 - c4
    if abs(d) < 0.005: return None
    return (c1/d*SCALE, c2/d*SCALE, c3/d*SCALE)

def make_curve(theta, phi):
    th = max(0.04, min(math.pi-0.04, theta))
    pts = []
    for i in range(TUBE_SEGS+1):
        t = (i/TUBE_SEGS)*math.pi*2
        p = hopf_pt(th, phi, t)
        pts.append(p if p else (0.,0.,0.))
    return pts

def catmull(pts, t):
    n = len(pts)-1
    ft = t*n; i = int(ft)%n; u = ft-int(ft)
    p0=pts[(i-1)%n]; p1=pts[i]; p2=pts[(i+1)%n]; p3=pts[(i+2)%n]
    def cr(a,b,c,d,u):
        return 0.5*((-a+3*b-3*c+d)*u**3+(2*a-5*b+4*c-d)*u**2+(-a+c)*u+2*b)
    return tuple(cr(p0[k],p1[k],p2[k],p3[k],u) for k in range(3))

def normalize(v):
    l=math.sqrt(sum(x*x for x in v)); return tuple(x/(l or 1) for x in v)
def cross(a,b): return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])
def sub(a,b): return tuple(a[k]-b[k] for k in range(3))
def add3(a,b): return tuple(a[k]+b[k] for k in range(3))
def scale3(v,s): return tuple(x*s for x in v)

def apply_rot(p):            # root.rotation.x = -PI/2
    x,y,z = p
    return (x*MODEL_SCALE, z*MODEL_SCALE, -y*MODEL_SCALE)

def hsl_rgb(h,s,l):
    h%=1
    def h2r(p,q,t):
        if t<0: t+=1
        if t>1: t-=1
        if t<1/6: return p+(q-p)*6*t
        if t<1/2: return q
        if t<2/3: return p+(q-p)*(2/3-t)*6
        return p
    if s==0: return (l,l,l)
    q=l*(1+s) if l<0.5 else l+s-l*s; p=2*l-q
    return (h2r(p,q,h+1/3), h2r(p,q,h), h2r(p,q,h-1/3))

# ── 生成管道 ─────────────────────────────────────────────────────
def gen_tube(curve_pts):
    # 采样截面中心 + Parallel Transport Frame
    centers=[]; tangs=[]
    eps=1e-4
    for i in range(TUBE_SEGS):
        c = catmull(curve_pts, i/TUBE_SEGS)
        cn= catmull(curve_pts, (i+1)/TUBE_SEGS - eps)
        centers.append(c); tangs.append(normalize(sub(cn,c)))

    t0=tangs[0]
    init_n = normalize(cross(t0,(1,0,0)) if abs(t0[0])<0.9 else cross(t0,(0,1,0)))
    frames=[]; cur_n=init_n
    for tang in tangs:
        d=sum(cur_n[k]*tang[k] for k in range(3))
        n=normalize(sub(cur_n, scale3(tang,d))); b=cross(tang,n)
        frames.append((n,b)); cur_n=n

    astep = 2*math.pi/TUBE_SIDES
    pos_list=[]; nrm_list=[]; uv_list=[]
    for i,(center,(n,b)) in enumerate(zip(centers,frames)):
        for j in range(TUBE_SIDES):
            a=j*astep
            nrm=add3(scale3(n,math.cos(a)), scale3(b,math.sin(a)))
            p  =add3(center, scale3(nrm, TUBE_R))
            pos_list.append(apply_rot(p))
            nrm_list.append(apply_rot(nrm))
            uv_list.append((i/TUBE_SEGS, j/TUBE_SIDES))

    idx=[]
    for i in range(TUBE_SEGS):
        ni=(i+1)%TUBE_SEGS
        for j in range(TUBE_SIDES):
            nj=(j+1)%TUBE_SIDES
            v0=i*TUBE_SIDES+j; v1=i*TUBE_SIDES+nj
            v2=ni*TUBE_SIDES+nj; v3=ni*TUBE_SIDES+j
            idx+=[v0,v2,v1, v0,v3,v2]    # CCW
    return pos_list, nrm_list, uv_list, idx

# ── 写 1×1 PNG（无依赖）──────────────────────────────────────────
def make_1x1_png(r8,g8,b8):
    def chunk(tag,data):
        c=struct.pack(">I",len(data))+tag+data
        return c+struct.pack(">I",zlib.crc32(tag+data)&0xFFFFFFFF)
    sig=b"\x89PNG\r\n\x1a\n"
    ihdr=chunk(b"IHDR",struct.pack(">IIBBBBB",1,1,8,2,0,0,0))
    raw=b"\x00"+bytes([r8,g8,b8])
    idat=chunk(b"IDAT",zlib.compress(raw,9))
    iend=chunk(b"IEND",b"")
    return sig+ihdr+idat+iend

# ── 构建 GLB ─────────────────────────────────────────────────────
print("building Hopf GLB ...")

# 收集所有纤维的几何 + 材质
meshes_data = []    # (pos, nrm, uv, idx, color_rgb_float)
for fi in range(N_FIBERS):
    frac  = fi/(N_FIBERS-1)
    theta = 0.2 + frac*math.pi*0.6
    phi   = frac*math.pi*4.0
    hue   = frac*0.72+0.12
    r,g,b = hsl_rgb(hue, 0.88, 0.56)
    curve = make_curve(theta, phi)
    pos,nrm,uv,idx = gen_tube(curve)
    meshes_data.append((pos,nrm,uv,idx,(r,g,b)))
    print(f"  fiber {fi+1:02d}/{N_FIBERS}  verts={len(pos)} tris={len(idx)//3}")

# ── 组装 glTF JSON + binary buffer ───────────────────────────────
bin_chunks = []   # bytes
accessors  = []
buffer_views = []
bv_offset = 0

def add_data(data_bytes, target=None):
    global bv_offset
    # pad to 4 bytes
    pad = (4 - len(data_bytes)%4)%4
    data_bytes += b'\x00'*pad
    bv = {"buffer": 0, "byteOffset": bv_offset, "byteLength": len(data_bytes)}
    if target: bv["target"] = target
    buffer_views.append(bv)
    bin_chunks.append(data_bytes)
    bv_offset += len(data_bytes)
    return len(buffer_views)-1

gltf_meshes = []
gltf_materials = []
gltf_images = []
gltf_textures = []
nodes = []

ARRAY_BUFFER  = 34962
ELEMENT_ARRAY = 34963

for fi, (pos,nrm,uv,idx,(r,g,b)) in enumerate(meshes_data):
    # 位置 buffer
    pos_bytes = struct.pack(f"{len(pos)*3}f", *[x for p in pos for x in p])
    nrm_bytes = struct.pack(f"{len(nrm)*3}f", *[x for n in nrm for x in n])
    uv_bytes  = struct.pack(f"{len(uv)*2}f",  *[x for u in uv  for x in u])
    idx_bytes = struct.pack(f"{len(idx)}H", *idx)

    # min/max for position
    xs=[p[0] for p in pos]; ys=[p[1] for p in pos]; zs=[p[2] for p in pos]
    pmin=[min(xs),min(ys),min(zs)]; pmax=[max(xs),max(ys),max(zs)]

    bv_pos = add_data(pos_bytes, ARRAY_BUFFER)
    bv_nrm = add_data(nrm_bytes, ARRAY_BUFFER)
    bv_uv  = add_data(uv_bytes,  ARRAY_BUFFER)
    bv_idx = add_data(idx_bytes, ELEMENT_ARRAY)

    n_verts = len(pos); n_idx = len(idx)
    acc_pos = len(accessors)
    accessors.append({"bufferView":bv_pos,"componentType":5126,"count":n_verts,"type":"VEC3","min":pmin,"max":pmax})
    acc_nrm = len(accessors)
    accessors.append({"bufferView":bv_nrm,"componentType":5126,"count":n_verts,"type":"VEC3"})
    acc_uv  = len(accessors)
    accessors.append({"bufferView":bv_uv, "componentType":5126,"count":n_verts,"type":"VEC2"})
    acc_idx = len(accessors)
    accessors.append({"bufferView":bv_idx,"componentType":5123,"count":n_idx, "type":"SCALAR"})

    # 1×1 PNG 贴图
    png_bytes = make_1x1_png(int(r*255),int(g*255),int(b*255))
    bv_img = add_data(png_bytes)
    img_idx = len(gltf_images)
    gltf_images.append({"bufferView": bv_img, "mimeType": "image/png"})
    tex_idx = len(gltf_textures)
    gltf_textures.append({"source": img_idx})

    mat_idx = len(gltf_materials)
    gltf_materials.append({
        "name": f"fiber_{fi:02d}",
        "pbrMetallicRoughness": {
            "baseColorFactor": [r, g, b, 1.0],   # explicit color, no lighting dependency
            "baseColorTexture": {"index": tex_idx},
            "metallicFactor": 0.0,
            "roughnessFactor": 1.0
        },
        "doubleSided": True
    })

    mesh_idx = len(gltf_meshes)
    gltf_meshes.append({
        "name": f"fiber_{fi:02d}",
        "primitives": [{
            "attributes": {
                "POSITION": acc_pos,
                "NORMAL":   acc_nrm,
                "TEXCOORD_0": acc_uv
            },
            "indices":  acc_idx,
            "material": mat_idx,
            "mode": 4
        }]
    })
    nodes.append({"mesh": mesh_idx, "name": f"fiber_{fi:02d}"})

total_bin = b"".join(bin_chunks)

gltf_json = {
    "asset": {"version": "2.0", "generator": "gen_hopf_glb"},
    "scene": 0,
    "scenes": [{"nodes": list(range(len(nodes)))}],
    "nodes": nodes,
    "meshes": gltf_meshes,
    "materials": gltf_materials,
    "images": gltf_images,
    "textures": gltf_textures,
    "accessors": accessors,
    "bufferViews": buffer_views,
    "buffers": [{"byteLength": len(total_bin)}]
}

json_bytes = json.dumps(gltf_json, separators=(',',':')).encode("utf-8")
# pad JSON to 4 bytes
json_pad = (4 - len(json_bytes)%4)%4
json_bytes += b' '*json_pad

# GLB 结构: header(12) + JSON chunk(8+json) + BIN chunk(8+bin)
json_chunk = struct.pack("<II", len(json_bytes), 0x4E4F534A) + json_bytes
bin_chunk  = struct.pack("<II", len(total_bin),  0x004E4942) + total_bin
glb_len = 12 + len(json_chunk) + len(bin_chunk)
header  = struct.pack("<III", 0x46546C67, 2, glb_len)

with open(GLB_PATH, "wb") as f:
    f.write(header + json_chunk + bin_chunk)

sz = os.path.getsize(GLB_PATH)
print(f"\nGLB OK: {GLB_PATH} ({sz//1024} KB)")
print(f"   tris: {sum(len(m[3])//3 for m in meshes_data)}")

# ── 调用 pycgfx 转换 CGFX ────────────────────────────────────────
print("running pycgfx ...")
result = subprocess.run(
    [PY, PYCGFX, GLB_PATH],
    capture_output=True, text=True, encoding="utf-8", errors="replace"
)
print(result.stdout)
if result.returncode != 0:
    print("STDERR:", result.stderr)
    sys.exit(1)

# pycgfx 输出与输入同名，后缀改为 .cgfx
generated = GLB_PATH.replace(".glb", ".cgfx")
if os.path.exists(generated):
    if generated != CGFX_PATH:
        import shutil; shutil.move(generated, CGFX_PATH)
    sz2 = os.path.getsize(CGFX_PATH)
    print(f"CGFX OK: {CGFX_PATH} ({sz2//1024} KB)")
    if sz2 > 512*1024:
        print("WARNING: CGFX > 512KB, may fail on 3DS!")
else:
    print("ERROR: pycgfx output not found")
