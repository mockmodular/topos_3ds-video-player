"""
gen_hopf_banner_obj.py
======================
把 hopf_spiral_15deg.html 的几何体精确复刻成 OBJ + MTL，
用于后续 EveryFileExplorer -> CGFX -> bannertool -> banner.bnr 流程。

输出:
  resource/banner_model/hopf_spiral.obj
  resource/banner_model/hopf_spiral.mtl

设计决策（最高运行效率优先）：
- 颜色烘焙为每个管材质（20种），不用顶点色，OBJ/MTL 兼容性最好
- 管截面 5 边（3DS banner 多边形预算有限；5×72×20=7200 三角形，很安全）
- 段数 72（与原 HTML 一致）
- 输出 UV（沿管长 u，沿截面 v），确保 CGFX 材质正确绑定
- 法线逐顶点计算，确保 Phong 光照正确
- 模型整体缩放到约 ±1 单位，适配 3DS banner 固定相机视野
"""

import math
import os
import struct
import zlib

# ── 参数 ─────────────────────────────────────────────────────────
# 3DS banner 实际可用：几百个三角形，目标 ≤ 500 tris
# N_FIBERS=10, TUBE_SEGS=12, TUBE_SIDES=4 → 10*12*4*2 = 960 tris  略多
# N_FIBERS=8,  TUBE_SEGS=12, TUBE_SIDES=4 → 8*12*4*2  = 768 tris  OK
# N_FIBERS=8,  TUBE_SEGS=10, TUBE_SIDES=3 → 8*10*3*2  = 480 tris  最安全
# 选 N=10, SEGS=12, SIDES=4 → 960 tris，保留足够的螺旋感，同时安全
N_FIBERS   = 10      # 纤维数（原20，降到10）
TUBE_SEGS  = 12      # 沿曲线段数（原72，降到12）
TUBE_SIDES = 4       # 管截面多边形边数（原5，降到4）
TUBE_R     = 0.055   # 管半径略加粗，低精度下更可见
SCALE      = 2.0     # Hopf 投影缩放

# 3DS banner 相机视野约 ±1 单位，模型 bbox 约 ±6，需要缩小
MODEL_SCALE = 0.9

# root.rotation.x = -PI/2  → Y-up 转 Z-up 已经在坐标变换里处理
# 我们直接输出 "旋转后" 的坐标（Y向上），方便建模软件查看
# 同时乘以 MODEL_SCALE 缩放到 banner 相机视野内

OUT_DIR  = os.path.join(os.path.dirname(__file__), "..", "resource", "banner_model")
TEX_DIR  = os.path.join(OUT_DIR, "tex")
os.makedirs(OUT_DIR, exist_ok=True)
os.makedirs(TEX_DIR, exist_ok=True)
OBJ_PATH = os.path.join(OUT_DIR, "hopf_spiral.obj")
MTL_PATH = os.path.join(OUT_DIR, "hopf_spiral.mtl")

# ── 写 1×1 纯色 PNG（无依赖，纯 struct/zlib）────────────────────
def write_1x1_png(path, r8, g8, b8):
    """写一个 1×1 RGB PNG 文件，r/g/b 为 0-255 整数。"""
    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    sig   = b"\x89PNG\r\n\x1a\n"
    ihdr  = chunk(b"IHDR", struct.pack(">IIBBBBB", 1, 1, 8, 2, 0, 0, 0))
    raw   = b"\x00" + bytes([r8, g8, b8])          # filter byte + RGB
    idat  = chunk(b"IDAT", zlib.compress(raw, 9))
    iend  = chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(sig + ihdr + idat + iend)

# ── 数学函数 ─────────────────────────────────────────────────────
def hopf_pt(theta, phi, t, scale=SCALE):
    eta = theta / 2.0
    c1  = math.cos(eta) * math.cos(t)
    c2  = math.cos(eta) * math.sin(t)
    c3  = math.sin(eta) * math.cos(phi + t)
    c4  = math.sin(eta) * math.sin(phi + t)
    d   = 1.0 - c4
    if abs(d) < 0.005:
        return None
    return (c1/d*scale, c2/d*scale, c3/d*scale)

def make_curve(theta, phi, segs=TUBE_SEGS):
    th = max(0.04, min(math.pi - 0.04, theta))
    pts = []
    for i in range(segs + 1):
        t = (i / segs) * math.pi * 2.0
        p = hopf_pt(th, phi, t)
        pts.append(p if p else (0.0, 0.0, 0.0))
    return pts  # segs+1 个点，首尾闭合

def catmull_rom(pts, t):
    """CatmullRom 插值，pts 为列表，t in [0,1)"""
    n = len(pts) - 1  # 闭合曲线 pts[0]==pts[n]
    ft = t * n
    i  = int(ft) % n
    u  = ft - int(ft)
    p0 = pts[(i - 1) % n]
    p1 = pts[i]
    p2 = pts[(i + 1) % n]
    p3 = pts[(i + 2) % n]
    def cr(a, b, c, d, u):
        return 0.5*((-a+3*b-3*c+d)*u*u*u + (2*a-5*b+4*c-d)*u*u + (-a+c)*u + 2*b)
    return tuple(cr(p0[k], p1[k], p2[k], p3[k], u) for k in range(3))

def normalize(v):
    l = math.sqrt(sum(x*x for x in v))
    if l < 1e-10:
        return (0.0, 1.0, 0.0)
    return tuple(x/l for x in v)

def cross(a, b):
    return (
        a[1]*b[2] - a[2]*b[1],
        a[2]*b[0] - a[0]*b[2],
        a[0]*b[1] - a[1]*b[0],
    )

def sub(a, b):
    return tuple(a[k]-b[k] for k in range(3))

def add_v(a, b):
    return tuple(a[k]+b[k] for k in range(3))

def scale_v(v, s):
    return tuple(x*s for x in v)

# ── HSL → RGB（与 Three.js setHSL 一致）─────────────────────────
def hsl_to_rgb(h, s, l):
    h = h % 1.0
    if s == 0:
        return (l, l, l)
    def hue2rgb(p, q, t):
        if t < 0: t += 1
        if t > 1: t -= 1
        if t < 1/6: return p + (q-p)*6*t
        if t < 1/2: return q
        if t < 2/3: return p + (q-p)*(2/3-t)*6
        return p
    q = l*(1+s) if l < 0.5 else l+s-l*s
    p = 2*l - q
    return (hue2rgb(p,q,h+1/3), hue2rgb(p,q,h), hue2rgb(p,q,h-1/3))

# root.rotation.x = -PI/2 变换：(x,y,z) -> (x,z,-y)，再乘缩放
def apply_root_rot(p):
    x, y, z = p
    s = MODEL_SCALE
    return (x*s, z*s, -y*s)

# ── 生成管道网格 ─────────────────────────────────────────────────
def gen_tube(curve_pts, radius, sides, segs):
    """
    返回 (verts, normals, faces)
    verts/normals: list of (x,y,z)
    faces: list of (v0,v1,v2,v3) 四边形索引（1-based 相对本管）
    """
    # 沿曲线均匀采样 segs 个截面（闭合）
    ring_centers = []
    ring_tangents = []
    eps = 1e-4
    for i in range(segs):
        t0 = i / segs
        t1 = (i + 1) / segs
        c  = catmull_rom(curve_pts, t0)
        cn = catmull_rom(curve_pts, min(t1, 1.0 - eps))
        tang = normalize(sub(cn, c))
        ring_centers.append(c)
        ring_tangents.append(tang)

    # Parallel Transport Frame（防止扭转）
    def make_frame(tang, prev_n):
        # 用 prev_n 投影到切线垂直面
        d = sum(prev_n[k]*tang[k] for k in range(3))
        n = normalize(sub(prev_n, scale_v(tang, d)))
        b = cross(tang, n)
        return n, b

    # 初始法向量：找一个不平行于 tang[0] 的轴
    t0 = ring_tangents[0]
    if abs(t0[0]) < 0.9:
        init_n = normalize(cross(t0, (1,0,0)))
    else:
        init_n = normalize(cross(t0, (0,1,0)))

    frames = []
    cur_n = init_n
    for tang in ring_tangents:
        n, b = make_frame(tang, cur_n)
        frames.append((n, b))
        cur_n = n

    # 生成顶点、法线、UV
    verts   = []
    normals = []
    uvs     = []
    angle_step = 2*math.pi / sides
    for i, (center, (n, b)) in enumerate(zip(ring_centers, frames)):
        u = i / segs  # 沿管长方向 UV.u
        for j in range(sides):
            a   = j * angle_step
            v_coord = j / sides  # 沿截面方向 UV.v
            nrm = add_v(scale_v(n, math.cos(a)), scale_v(b, math.sin(a)))
            pos = add_v(center, scale_v(nrm, radius))
            verts.append(apply_root_rot(pos))
            normals.append(apply_root_rot(nrm))
            uvs.append((u, v_coord))

    # 面（四边形，每个四边形存为两个三角形）
    quads = []
    for i in range(segs):
        ni = (i + 1) % segs
        for j in range(sides):
            nj = (j + 1) % sides
            v0 = i  * sides + j
            v1 = i  * sides + nj
            v2 = ni * sides + nj
            v3 = ni * sides + j
            quads.append((v0, v1, v2, v3))

    return verts, normals, uvs, quads

# ── 主程序 ───────────────────────────────────────────────────────
print("生成 Hopf spiral 模型...")

all_verts   = []
all_normals = []
all_uvs     = []
all_faces   = []  # list of (mat_idx, tri of (vi,uvi,ni) 1-based global)
materials   = []  # list of (name, r, g, b)

vert_offset = 0
norm_offset = 0
uv_offset   = 0

for fi in range(N_FIBERS):
    frac  = fi / (N_FIBERS - 1)
    theta = 0.2 + frac * math.pi * 0.6
    phi   = frac * math.pi * 4.0

    curve_pts = make_curve(theta, phi, TUBE_SEGS)

    hue = frac * 0.72 + 0.12
    r, g, b = hsl_to_rgb(hue, 0.88, 0.56)
    mat_name = f"fiber_{fi:02d}"
    tex_name = f"fiber_{fi:02d}.png"
    # 生成 1×1 PNG 贴图
    write_1x1_png(os.path.join(TEX_DIR, tex_name),
                  int(r*255), int(g*255), int(b*255))
    materials.append((mat_name, r, g, b, tex_name))

    verts, normals, uvs, quads = gen_tube(curve_pts, TUBE_R, TUBE_SIDES, TUBE_SEGS)

    all_verts.extend(verts)
    all_normals.extend(normals)
    all_uvs.extend(uvs)

    for (v0, v1, v2, v3) in quads:
        g0 = vert_offset + v0 + 1
        g1 = vert_offset + v1 + 1
        g2 = vert_offset + v2 + 1
        g3 = vert_offset + v3 + 1
        n0 = norm_offset + v0 + 1
        n1 = norm_offset + v1 + 1
        n2 = norm_offset + v2 + 1
        n3 = norm_offset + v3 + 1
        t0 = uv_offset + v0 + 1
        t1 = uv_offset + v1 + 1
        t2 = uv_offset + v2 + 1
        t3 = uv_offset + v3 + 1
        # 两个三角形：格式 (vi, uvi, ni)
        all_faces.append((fi, (g0,t0,n0), (g2,t2,n2), (g1,t1,n1)))
        all_faces.append((fi, (g0,t0,n0), (g3,t3,n3), (g2,t2,n2)))

    vert_offset += len(verts)
    norm_offset += len(normals)
    uv_offset   += len(uvs)

    print(f"  fiber {fi+1:02d}/{N_FIBERS}  verts={len(verts)}")

# ── 写 MTL ──────────────────────────────────────────────────────
with open(MTL_PATH, "w", encoding="utf-8") as f:
    f.write("# Hopf spiral banner model - material file\n")
    for (name, r, g, b, tex_name) in materials:
        f.write(f"\nnewmtl {name}\n")
        f.write(f"Kd {r:.6f} {g:.6f} {b:.6f}\n")
        f.write(f"Ka {r*0.15:.6f} {g*0.15:.6f} {b*0.15:.6f}\n")
        f.write(f"Ks 0.400000 0.350000 0.600000\n")
        f.write(f"Ns 80.0\n")
        f.write(f"illum 2\n")
        # 关键：map_Kd 让 EveryFileExplorer 识别并嵌入贴图到 CGFX
        f.write(f"map_Kd tex/{tex_name}\n")
print(f"MTL 写入: {MTL_PATH}")

# ── 写 OBJ ──────────────────────────────────────────────────────
with open(OBJ_PATH, "w", encoding="utf-8") as f:
    f.write("# Hopf spiral banner model\n")
    f.write("# 20 fibers x 72 segs x 5 sides = 7200 tris\n")
    f.write(f"mtllib hopf_spiral.mtl\n\n")

    # 顶点
    for (x, y, z) in all_verts:
        f.write(f"v {x:.6f} {y:.6f} {z:.6f}\n")
    f.write("\n")

    # UV
    for (u, v) in all_uvs:
        f.write(f"vt {u:.6f} {v:.6f}\n")
    f.write("\n")

    # 法线
    for (x, y, z) in all_normals:
        f.write(f"vn {x:.6f} {y:.6f} {z:.6f}\n")
    f.write("\n")

    # 面（按材质分组），格式 v/vt/vn
    cur_mat = -1
    for (mat_idx, vtn0, vtn1, vtn2) in all_faces:
        if mat_idx != cur_mat:
            mat_name = materials[mat_idx][0]
            f.write(f"\ng {mat_name}\n")
            f.write(f"usemtl {mat_name}\n")
            cur_mat = mat_idx
        def fmt(vtn):
            return f"{vtn[0]}/{vtn[1]}/{vtn[2]}"
        f.write(f"f {fmt(vtn0)} {fmt(vtn1)} {fmt(vtn2)}\n")

print(f"OBJ 写入: {OBJ_PATH}")
print(f"\n统计：")
print(f"  顶点数  : {len(all_verts)}")
print(f"  法线数  : {len(all_normals)}")
print(f"  三角形数: {len(all_faces)}")
print(f"  材质数  : {len(materials)}")
print(f"\n✅ 完成。文件位于: {os.path.abspath(OUT_DIR)}")
