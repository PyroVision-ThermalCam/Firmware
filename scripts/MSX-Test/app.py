"""
MSX (Multi-Spectral Dynamic Imaging) - FLIR-style edge overlay.

How FLIR MSX works (and how this differs from a plain dark-line overlay)
------------------------------------------------------------------------
A naive approach draws Canny edges as black lines on top of the thermal
image.  That looks artificial because the lines are binary (on/off) and
always black, regardless of the thermal background.

FLIR MSX uses a SIGNED Sobel-gradient emboss technique instead:
  1. Compute Sobel gradients Gx, Gy from the visible-light image.
  2. Project the gradient onto a simulated light direction:
         emboss = Gx * cos(angle) + Gy * sin(angle)
     Result: +1 on the "lit" slope of an edge, -1 on the "shadow" slope.
  3. Add the signed emboss to every channel of the thermal image:
         MSX = clip(thermal + strength * emboss)

Because the addition is signed and averages to zero across each edge,
the thermal palette is preserved.  Edges appear as a bright/dark relief
("engraving") rather than flat black lines.

Optional Canny mask: when Canny threshold > 0 the emboss is zeroed in
areas without real edges, reducing noise in smooth regions.

Camera geometry
---------------
  RGB camera  : HFOV = 72 deg
  Thermal cam : HFOV = 57 deg
  Baseline    : 11.2 mm (horizontal separation)

Controls (trackbars)
--------------------
  Distance (cm)  - estimated subject distance (drives parallax correction)
  H-offset fine  - manual horizontal fine-tuning [+- 40 px]
  V-offset fine  - manual vertical   fine-tuning [+- 40 px]
  Strength %     - overlay intensity (0 = off, 100 = +-80 intensity levels)
  Light angle    - emboss illumination direction [0-360 deg]
                   45 deg (top-left) is the classic FLIR-like look.
  Sigma x10      - Gaussian pre-blur sigma * 10 (e.g. 10 -> sigma=1.0)
                   Lower = finer texture; higher = only large edges.
  Canny thresh   - Canny low threshold; 0 disables the Canny mask entirely.

Keys
----
  S     - save msx_output.png
  Q/Esc - quit
"""

import math
import os

import cv2
import numpy as np

# ---------------------------------------------------------------------------
# Camera geometry
# ---------------------------------------------------------------------------
RGB_HFOV_DEG     = 72.0   # horizontal FOV of the visible camera [deg]
THERMAL_HFOV_DEG = 57.0   # horizontal FOV of the thermal camera [deg]
BASELINE_MM      = 11.2   # lateral camera separation [mm]

# ---------------------------------------------------------------------------
# File paths
# ---------------------------------------------------------------------------
SCRIPT_DIR   = os.path.dirname(os.path.abspath(__file__))
RGB_PATH     = os.path.join(SCRIPT_DIR, "images", "RGB.png")
THERMAL_PATH = os.path.join(SCRIPT_DIR, "images", "Thermal.png")
OUTPUT_PATH  = os.path.join(SCRIPT_DIR, "msx_output.png")

# ---------------------------------------------------------------------------
# Load images
# ---------------------------------------------------------------------------
_rgb_orig     = cv2.imread(RGB_PATH)
_thermal_orig = cv2.imread(THERMAL_PATH)

if _rgb_orig is None or _thermal_orig is None:
    raise FileNotFoundError("Could not load images from the 'images' folder.")

T_H, T_W = _thermal_orig.shape[:2]   # thermal resolution (reference size)
R_H, R_W = _rgb_orig.shape[:2]

# ---------------------------------------------------------------------------
# Focal lengths [px]:  f = W / (2 * tan(HFOV/2))
# ---------------------------------------------------------------------------
F_RGB     = R_W / (2.0 * math.tan(math.radians(RGB_HFOV_DEG     / 2.0)))
F_THERMAL = T_W / (2.0 * math.tan(math.radians(THERMAL_HFOV_DEG / 2.0)))

# ---------------------------------------------------------------------------
# Pre-warp: crop RGB to the angular extent of the thermal FOV, then rescale.
#   crop_w = 2 * F_RGB * tan(THERMAL_HFOV/2)
# ---------------------------------------------------------------------------
crop_w = int(round(2.0 * F_RGB * math.tan(math.radians(THERMAL_HFOV_DEG / 2.0))))
crop_h = int(round(crop_w * (T_H / T_W)))

crop_w = min(crop_w, R_W)
crop_h = min(crop_h, R_H)

cx0 = (R_W - crop_w) // 2
cy0 = (R_H - crop_h) // 2

_rgb_cropped = _rgb_orig[cy0 : cy0 + crop_h, cx0 : cx0 + crop_w]
_rgb_scaled  = cv2.resize(_rgb_cropped, (T_W, T_H), interpolation=cv2.INTER_AREA)

print("RGB     : {}x{}  f={:.1f} px".format(R_W, R_H, F_RGB))
print("Thermal : {}x{}  f={:.1f} px".format(T_W, T_H, F_THERMAL))
print("RGB crop for thermal FOV: {}x{}  -> rescaled to {}x{}".format(
      crop_w, crop_h, T_W, T_H))


# ---------------------------------------------------------------------------
# MSX core
# ---------------------------------------------------------------------------
def compute_msx(
    rgb_scaled,
    thermal,
    dist_mm,
    h_fine,
    v_fine,
    strength_pct,
    light_angle_deg,
    sigma10,
    canny_thresh,
):
    """
    Returns (msx_image, rgb_aligned, emboss_vis).

    Parallax model
    --------------
    Horizontal shift in thermal pixels for a baseline B and distance D:
        dx = B * F_thermal / D
    The sign assumes the RGB camera is to the right of the thermal camera.
    """
    h, w = thermal.shape[:2]

    # -- 1. Parallax-correct the pre-warped RGB --------------------------------
    parallax_px = (BASELINE_MM * F_THERMAL / dist_mm) if dist_mm > 0.0 else 0.0
    dx = int(round(parallax_px)) + h_fine
    dy = v_fine

    M = np.float32([[1, 0, dx], [0, 1, dy]])
    rgb_aligned = cv2.warpAffine(
        rgb_scaled, M, (w, h),
        flags=cv2.INTER_LINEAR,
        borderMode=cv2.BORDER_REPLICATE,
    )

    # -- 2. Grayscale + Gaussian pre-blur -------------------------------------
    sigma       = max(0.1, sigma10 / 10.0)
    gray        = cv2.cvtColor(rgb_aligned, cv2.COLOR_BGR2GRAY).astype(np.float32)
    gray_smooth = cv2.GaussianBlur(gray, (0, 0), sigma)

    # -- 3. Sobel gradients ---------------------------------------------------
    gx = cv2.Sobel(gray_smooth, cv2.CV_32F, 1, 0, ksize=3)
    gy = cv2.Sobel(gray_smooth, cv2.CV_32F, 0, 1, ksize=3)

    # -- 4. Project onto light direction (signed emboss) ----------------------
    #   emboss = Gx * cos(angle) + Gy * sin(angle)
    #   Positive on the "lit" slope, negative on the "shadow" slope.
    angle_rad = math.radians(light_angle_deg)
    emboss    = gx * math.cos(angle_rad) + gy * math.sin(angle_rad)

    # -- 5. Optional Canny mask -----------------------------------------------
    #   Zeroes the emboss in areas without real edges, reducing gradient noise
    #   in textured regions (fabric, wood grain, etc.).
    if canny_thresh > 0:
        gray_u8   = np.clip(gray_smooth, 0, 255).astype(np.uint8)
        edges     = cv2.Canny(gray_u8, canny_thresh, canny_thresh * 2)
        edges_dil = cv2.dilate(edges, np.ones((3, 3), np.uint8), iterations=1)
        emboss    = emboss * (edges_dil.astype(np.float32) / 255.0)

    # -- 6. Normalize using the 99th percentile of |emboss| -------------------
    #   Clips outlier gradients so the mean edge has close to full strength.
    p99 = float(np.percentile(np.abs(emboss), 99))
    if p99 > 1e-6:
        emboss = emboss / p99
    emboss = np.clip(emboss, -1.0, 1.0)

    # -- 7. Add signed emboss to thermal --------------------------------------
    #   strength_pct = 100 -> max +-80 intensity levels added to thermal.
    #   Signed addition preserves the mean thermal value per edge (it just
    #   adds contrast), unlike multiplicative darkening which shifts the mean.
    strength_px = strength_pct / 100.0 * 80.0
    thermal_f   = thermal.astype(np.float32)
    msx         = np.clip(
        thermal_f + strength_px * emboss[:, :, np.newaxis],
        0.0, 255.0,
    ).astype(np.uint8)

    # Visualise emboss: map [-1, 1] -> [0, 255] grey image for the debug panel
    emboss_vis = np.clip(emboss * 127.0 + 128.0, 0, 255).astype(np.uint8)
    emboss_vis = cv2.cvtColor(emboss_vis, cv2.COLOR_GRAY2BGR)

    return msx, rgb_aligned, emboss_vis


# ---------------------------------------------------------------------------
# GUI
# ---------------------------------------------------------------------------
WIN = "MSX - Q/Esc: quit | S: save"
cv2.namedWindow(WIN, cv2.WINDOW_NORMAL)
cv2.resizeWindow(WIN, T_W * 4, T_H + 120)

cv2.createTrackbar("Distance (cm)",  WIN,  50, 500, lambda _: None)

FINE_CENTRE = 40
cv2.createTrackbar("H-offset fine",  WIN, FINE_CENTRE, FINE_CENTRE * 2, lambda _: None)
cv2.createTrackbar("V-offset fine",  WIN, FINE_CENTRE, FINE_CENTRE * 2, lambda _: None)

cv2.createTrackbar("Strength %",     WIN,  60, 100, lambda _: None)
# 45 deg = top-left illumination (classic engraving look)
cv2.createTrackbar("Light angle",    WIN,  45, 360, lambda _: None)
# Sigma * 10: value 10 -> sigma=1.0 px
cv2.createTrackbar("Sigma x10",      WIN,  10,  50, lambda _: None)
# Canny low threshold; 0 = no Canny mask (pure gradient emboss)
cv2.createTrackbar("Canny thresh",   WIN,   0, 150, lambda _: None)


def get_tb(name):
    return cv2.getTrackbarPos(name, WIN)


FONT       = cv2.FONT_HERSHEY_SIMPLEX
LABEL_SCALE = 0.45
LABEL_Y    = T_H - 6


def put_label(img, text, x):
    cv2.putText(img, text, (x + 4, LABEL_Y), FONT, LABEL_SCALE, (0, 0, 0), 2)
    cv2.putText(img, text, (x + 4, LABEL_Y), FONT, LABEL_SCALE, (220, 220, 220), 1)


while True:
    dist_cm         = max(1, get_tb("Distance (cm)"))
    h_fine          = get_tb("H-offset fine") - FINE_CENTRE
    v_fine          = get_tb("V-offset fine") - FINE_CENTRE
    strength_pct    = get_tb("Strength %")
    light_angle_deg = get_tb("Light angle")
    sigma10         = max(1, get_tb("Sigma x10"))
    canny_thresh    = get_tb("Canny thresh")

    msx, rgb_aligned, emboss_vis = compute_msx(
        _rgb_scaled, _thermal_orig,
        dist_mm         = dist_cm * 10.0,
        h_fine          = h_fine,
        v_fine          = v_fine,
        strength_pct    = strength_pct,
        light_angle_deg = light_angle_deg,
        sigma10         = sigma10,
        canny_thresh    = canny_thresh,
    )

    display = np.hstack([rgb_aligned, _thermal_orig, emboss_vis, msx])
    put_label(display, "RGB (aligned)", 0)
    put_label(display, "Thermal",       T_W)
    put_label(display, "Emboss map",    T_W * 2)
    put_label(display, "MSX",           T_W * 3)

    parallax_px = int(round(BASELINE_MM * F_THERMAL / (dist_cm * 10.0))) + h_fine
    info = "dist={}cm  parallax={:+d}px  angle={}deg  sigma={:.1f}  canny={}".format(
        dist_cm, parallax_px, light_angle_deg, sigma10 / 10.0, canny_thresh
    )
    cv2.putText(display, info, (4, 14), FONT, 0.4, (0, 0, 0), 2)
    cv2.putText(display, info, (4, 14), FONT, 0.4, (200, 255, 200), 1)

    cv2.imshow(WIN, display)

    key = cv2.waitKey(30) & 0xFF
    if key in (ord('q'), 27):
        break
    elif key == ord('s'):
        cv2.imwrite(OUTPUT_PATH, msx)
        print("Saved -> {}".format(OUTPUT_PATH))

cv2.destroyAllWindows()
