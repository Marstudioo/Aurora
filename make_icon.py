# Aurora 应用图标生成：深夜极光 + 音符，256/100/80 三尺寸
from PIL import Image, ImageDraw, ImageFont
import math, os

SIZE = 256
img = Image.new("RGB", (SIZE, SIZE), (10, 12, 24))
d = ImageDraw.Draw(img)

# 背景：深空渐变
for y in range(SIZE):
    t = y / SIZE
    r = int(8 + 10 * t)
    g = int(12 + 18 * t)
    b = int(28 + 40 * t)
    d.line([(0, y), (SIZE, y)], fill=(r, g, b))

# 极光带：三条半透明正弦飘带
def aurora_band(offset, height, color, alpha):
    layer = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    ld = ImageDraw.Draw(layer)
    pts_top = []
    pts_bot = []
    for x in range(0, SIZE + 1, 4):
        phase = x / SIZE * 2.6 * math.pi
        y_mid = offset + height * 0.5 * math.sin(phase + x / 90.0)
        pts_top.append((x, y_mid - height * 0.5))
        pts_bot.append((x, y_mid + height * 0.5))
    poly = pts_top + pts_bot[::-1]
    ld.polygon(poly, fill=color + (alpha,))
    # 高斯般的柔化：多次轻微缩放叠加
    return layer

bands = [
    (60, 46, (60, 230, 170), 90),
    (110, 40, (80, 200, 255), 75),
    (155, 34, (150, 120, 255), 60),
]
for off, h, col, a in bands:
    img.paste(aurora_band(off, h, col, a), (0, 0), aurora_band(off, h, col, a))

# 音符（双八分音符），居中偏下
note = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
nd = ImageDraw.Draw(note)
white = (245, 247, 255, 235)
# 符头
nd.ellipse([96, 158, 140, 190], fill=white)
nd.ellipse([162, 146, 206, 178], fill=white)
# 符干
nd.rectangle([132, 78, 139, 174], fill=white)
nd.rectangle([198, 66, 205, 162], fill=white)
# 符尾（横梁）
nd.polygon([(132, 78), (205, 62), (205, 84), (132, 100)], fill=white)
img.paste(note, (0, 0), note)

# 官方规范：不建议自带圆角/边框（提交指南 :16），出干净方形 PNG
out = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
out.paste(img, (0, 0))

root = os.path.dirname(os.path.abspath(__file__))
dst = os.path.join(root, "assets", "images")
out.save(os.path.join(dst, "aurora.png"))
out.resize((100, 100), Image.LANCZOS).save(os.path.join(dst, "aurora_100.png"))
out.resize((80, 80), Image.LANCZOS).save(os.path.join(dst, "aurora_80.png"))
print("icons written to", dst)
