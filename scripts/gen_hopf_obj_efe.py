# gen_hopf_obj_efe.py
# Hopf spiral as RIBBON strips (not cylinders)
# Single material "COMMON", single UV gradient texture
# For EveryFileExplorer OBJ->CGFX import
#
# Usage: python scripts/gen_hopf_obj_efe.py

import math, os, struct, zlib

OUT_DIR  = os.path.join(os.path.dirname(__file__), "..", "resource", "banner_model")
os.makedirs(OUT_DIR, exist_ok=True)
OBJ_PATH = os.path.join(OUT_DIR, "hopf_spiral.obj")
MTL_PATH = os.path.join(OUT_DIR, "hopf_spiral.mtl")
TEX_PATH = os.path.join(OUT_DIR, "hopf_tex.png")

# ── Parameters ──────────────────────────────────────────────────
N_FIBERS    = 20    # restored to original 20 fibers (ribbon is cheap enough)
RIBBON_SEGS = 36    # segments along each fiber curve
RIBBON_W    = 0.06  # half-width of the ribbon strip
SCALE       = 2.0
MODEL_SCALE = 2.7

TEX_W = 128
TEX_H = 8

# ── Math ────────────────────────────────────────────────────────
def hopf_pt(theta, phi, t):
    eta = theta / 2.0
    c1  = math.cos(eta) * math.cos(t)
    c2  = math.cos(eta) * math.sin(t)
    c3  = math.sin(eta) * math.cos(phi + t)
    c4  = math.sin(eta) * math.sin(phi + t)
    d   = 1.0 - c4
    if abs(d) < 0.005: return None
    return (c1/d*SCALE, c2/d*SCALE, c3/d*SCALE)

def make_curve(theta, phi):
    th = max(0.04, min(math.pi - 0.04, theta))
    pts = []
    for i in range(RIBBON_SEGS + 1):
        t = (i / RIBBON_SEGS) * math.pi * 2
        p = hopf_pt(th, phi, t)
        pts.append(p if p else (0., 0., 0.))
    return pts

def catmull(pts, t):
    n = len(pts) - 1
    ft = t * n; i = int(ft) % n; u = ft - int(ft)
    p0=pts[(i-1)%n]; p1=pts[i]; p2=pts[(i+1)%n]; p3=pts[(i+2)%n]
    def cr(a,b,c,d,u):
        return 0.5*((-a+3*b-3*c+d)*u**3+(2*a-5*b+4*c-d)*u**2+(-a+c)*u+2*b)
    return tuple(cr(p0[k],p1[k],p2[k],p3[k],u) for k in range(3))

def normalize(v):
    l = math.sqrt(sum(x*x for x in v)); return tuple(x/(l or 1) for x in v)
def cross(a,b): return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])
def sub(a,b):   return tuple(a[k]-b[k] for k in range(3))
def add3(a,b):  return tuple(a[k]+b[k] for k in range(3))
def scale3(v,s):return tuple(x*s for x in v)

def apply_rot(p):
    x, y, z = p
    return (x*MODEL_SCALE, z*MODEL_SCALE, -y*MODEL_SCALE)

def hsl_rgb(h, s, l):
    h %= 1
    def h2r(p,q,t):
        if t<0: t+=1
        if t>1: t-=1
        if t<1/6: return p+(q-p)*6*t
        if t<1/2: return q
        if t<2/3: return p+(q-p)*(2/3-t)*6
        return p
    if s == 0: return (l, l, l)
    q = l*(1+s) if l < 0.5 else l+s-l*s; p = 2*l - q
    return (h2r(p,q,h+1/3), h2r(p,q,h), h2r(p,q,h-1/3))

# ── Gradient texture ─────────────────────────────────────────────
# Horizontal = fiber color gradient (left=fiber0, right=fiberN)
# Vertical   = ribbon edge shading (top/bottom darker, center bright)
def make_gradient_png(w, h):
    pixels = []
    for row in range(h):
        shade = 0.65 + 0.35 * (1.0 - abs((row / (h-1)) * 2 - 1))
        row_px = []
        for col in range(w):
            frac = col / (w - 1)
            hue  = frac * 0.72 + 0.12
            r, g, b = hsl_rgb(hue, 0.88, 0.56)
            row_px += [min(255,int(r*shade*255)), min(255,int(g*shade*255)), min(255,int(b*shade*255))]
        pixels.append(row_px)

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag+data) & 0xFFFFFFFF)
    sig  = b"\x89PNG\r\n\x1a\n"
    ihdr = chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    raw  = b"".join(b"\x00" + bytes(row) for row in pixels)
    idat = chunk(b"IDAT", zlib.compress(raw, 9))
    iend = chunk(b"IEND", b"")
    return sig + ihdr + idat + iend

with open(TEX_PATH, "wb") as f:
    f.write(make_gradient_png(TEX_W, TEX_H))
print(f"TEX OK: {TEX_PATH} ({TEX_W}x{TEX_H})")

# ── Ribbon geometry ──────────────────────────────────────────────
# Each fiber = a flat ribbon strip: 2 edge vertices per segment
# Normal = cross(tangent, up_world) to keep ribbon facing camera-ish
# Both faces rendered (doubleSided via two opposite-winding quads)

all_pos=[]; all_nrm=[]; all_uv=[]; all_faces=[]
v_offset = 0

for fi in range(N_FIBERS):
    frac  = fi / (N_FIBERS - 1)
    theta = 0.2 + frac * math.pi * 0.6
    phi   = frac * math.pi * 4.0

    # U coordinate in texture: map this fiber to its column strip
    u_tex = frac   # 0..1 across gradient texture

    curve = make_curve(theta, phi)

    # Frenet-Serret frame:
    #   T = tangent (along curve)
    #   N = principal normal = direction curve bends toward = toward curvature center
    #   B = binormal = T x N
    #
    # The ribbon is a narrow cylinder strip:
    #   - edges spread along T x N_frenet (= B, the binormal)  -> ribbon width direction
    #   - face normal = N_frenet  -> points toward the curvature center (radially inward)
    #
    # N_frenet = normalize(dT/ds) = normalize(T(i+1) - T(i))

    eps = 1e-4
    centers=[]; tangs=[]
    for i in range(RIBBON_SEGS):
        c  = catmull(curve, i / RIBBON_SEGS)
        cn = catmull(curve, (i+1)/RIBBON_SEGS - eps)
        centers.append(c)
        tangs.append(normalize(sub(cn, c)))

    frames = []  # (N_frenet, B_frenet) per segment
    for i in range(RIBBON_SEGS):
        T  = tangs[i]
        Tn = tangs[(i+1) % RIBBON_SEGS]
        dT = sub(Tn, T)
        if math.sqrt(sum(x*x for x in dT)) < 1e-8:
            # fallback when nearly straight: use previous frame
            if frames:
                frames.append(frames[-1])
            else:
                N = normalize(cross(T,(0,1,0)) if abs(T[1])<0.9 else cross(T,(1,0,0)))
                frames.append((N, cross(T, N)))
        else:
            N = normalize(dT)          # principal normal = toward curvature center
            B = normalize(cross(T, N)) # binormal = T x N
            frames.append((N, B))

    # ribbon edges spread along B; face normal = N (toward curvature center)
    seg_verts = []
    for i in range(RIBBON_SEGS):
        c    = centers[i]
        N, B = frames[i]
        left  = add3(c, scale3(B,  RIBBON_W))
        right = add3(c, scale3(B, -RIBBON_W))
        seg_verts.append((apply_rot(left), apply_rot(right), apply_rot(N)))

    # store verts: for each seg, left then right
    local_start = v_offset
    for i, (lp, rp, nrm) in enumerate(seg_verts):
        seg_u = i / (RIBBON_SEGS - 1)   # 0..1 not used for color, color from u_tex
        all_pos.append(lp);  all_nrm.append(nrm); all_uv.append((u_tex, 0.1))
        all_pos.append(rp);  all_nrm.append(nrm); all_uv.append((u_tex, 0.9))

    # quads: connect seg i to seg i+1 (wrap around for closed loop)
    for i in range(RIBBON_SEGS):
        ni = (i + 1) % RIBBON_SEGS
        vL0 = local_start + i*2;     vR0 = local_start + i*2 + 1
        vL1 = local_start + ni*2;    vR1 = local_start + ni*2 + 1
        # front face (CCW)
        all_faces.append((vL0, vR0, vL1))
        all_faces.append((vR0, vR1, vL1))
        # back face (reverse winding for double-sided)
        all_faces.append((vL0, vL1, vR0))
        all_faces.append((vR0, vL1, vR1))

    v_offset += RIBBON_SEGS * 2
    tris_this = RIBBON_SEGS * 4
    print(f"  fiber {fi+1:02d}/{N_FIBERS}  verts={RIBBON_SEGS*2}  tris={tris_this}")

# ── Write MTL ────────────────────────────────────────────────────
with open(MTL_PATH, "w") as f:
    f.write("newmtl COMMON\n")
    f.write("Ka 1.0 1.0 1.0\n")
    f.write("Kd 1.0 1.0 1.0\n")
    f.write("Ks 0.0 0.0 0.0\n")
    f.write("Ns 0\n")
    f.write("map_Kd hopf_tex.png\n")
print(f"MTL OK: {MTL_PATH}")

# ── Write OBJ ────────────────────────────────────────────────────
with open(OBJ_PATH, "w") as f:
    f.write("# hopf_spiral ribbons - single material COMMON\n")
    f.write("mtllib hopf_spiral.mtl\n\n")
    f.write("o COMMON\n\n")
    for (x,y,z) in all_pos:
        f.write(f"v {x:.6f} {y:.6f} {z:.6f}\n")
    f.write("\n")
    for (u,v) in all_uv:
        f.write(f"vt {u:.6f} {v:.6f}\n")
    f.write("\n")
    for (x,y,z) in all_nrm:
        f.write(f"vn {x:.6f} {y:.6f} {z:.6f}\n")
    f.write("\n")
    f.write("usemtl COMMON\n")
    f.write("g COMMON\n")
    for (a,b,c) in all_faces:
        i1=a+1; i2=b+1; i3=c+1
        f.write(f"f {i1}/{i1}/{i1} {i2}/{i2}/{i2} {i3}/{i3}/{i3}\n")

total_tris = len(all_faces)
print(f"OBJ OK: {OBJ_PATH}")
print(f"  total verts={len(all_pos)}  tris={total_tris}")
print(f"  budget: {total_tris} / ~2000 recommended")
