#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成 UI 图标资源（一次性脚本，产物已提交，构建时不需要运行）。

做两件事：
  1. 从 Lucide 官方包（ISC 协议）挑选本工程需要的图标：
       - 拷贝原始 SVG 到 resources/icons/<name>.svg（保留出处与许可，见 README.md）
       - 把 stroke="currentColor" 替换成白色后，用 rsvg-convert 渲染成
         resources/icons/<name>.png（96x96 白色描边 + 透明底）。
         运行时由 IconProvider 用 QPainter 的 SourceIn 合成上色，
         因此每种图标只需要一份白色 PNG，双主题都能用。
  2. 自绘应用图标（超分/放大主题，橙色品牌色），输出
       resources/app.png（256x256）与 resources/app.ico（16/32/48/256 多尺寸）。

依赖：rsvg-convert（MSYS2: pacman -S mingw-w64-ucrt-x86_64-librsvg）、Pillow。
用法：python resources/icons/generate_icons.py [--lucide <lucide icons 目录>]
"""
import argparse
import os
import shutil
import subprocess
import sys

from PIL import Image, ImageDraw

# 需要的图标（文件名 = Lucide 图标名）
ICON_NAMES = [
	"folder-open", "image", "video", "play", "pause", "square",
	"refresh-cw", "settings", "sun", "moon", "monitor", "plus",
	"trash-2", "external-link", "chevron-down", "chevron-right", "check",
	"x", "alert-triangle", "cpu", "zap", "info", "minus", "maximize",
	"minimize", "panel-left-close", "panel-left-open", "rotate-ccw",
	"activity", "sparkles", "github", "book-open", "keyboard", "images",
	"film",
]

# 渲染尺寸：UI 里最大显示 24px，2x 屏 48px，取 96 保证缩放下依然锐利
RENDER_PX = 96


def generate_ui_icons(lucide_dir, out_dir, rsvg):
	for name in ICON_NAMES:
		src = os.path.join(lucide_dir, name + ".svg")
		if not os.path.exists(src):
			print("[skip] 缺少 Lucide 图标: %s" % name)
			continue
		dst_svg = os.path.join(out_dir, name + ".svg")
		shutil.copyfile(src, dst_svg)

		with open(src, "r", encoding="utf-8") as f:
			content = f.read()
		# Lucide 用 currentColor 描边；渲染成白色以便运行时任意上色
		content = content.replace('stroke="currentColor"', 'stroke="#FFFFFF"')
		white_svg = os.path.join(out_dir, "_white_" + name + ".svg")
		with open(white_svg, "w", encoding="utf-8") as f:
			f.write(content)

		dst_png = os.path.join(out_dir, name + ".png")
		subprocess.run(
			[rsvg, "-w", str(RENDER_PX), "-h", str(RENDER_PX), white_svg,
			 "-o", dst_png],
			check=True)
		os.remove(white_svg)
		print("[ok] %s" % name)


def _rounded_mask(size, radius):
	mask = Image.new("L", (size, size), 0)
	ImageDraw.Draw(mask).rounded_rectangle(
		[0, 0, size - 1, size - 1], radius=radius, fill=255)
	return mask


def _gradient(size, top, bottom):
	img = Image.new("RGB", (size, size), top)
	draw = ImageDraw.Draw(img)
	for y in range(size):
		t = y / float(size - 1)
		color = tuple(int(top[i] + (bottom[i] - top[i]) * t) for i in range(3))
		draw.line([(0, y), (size, y)], fill=color)
	return img


def generate_app_icon(out_dir):
	"""自绘应用图标：橙色圆角底 + 白色「放大镜 + 上箭头」超分符号 + 星光。"""
	scale = 4  # 4 倍超采样后缩小，得到干净抗锯齿边缘
	s = 256 * scale

	# 1) 橙色渐变圆角底
	bg = _gradient(s, (0xFB, 0x92, 0x3C), (0xEA, 0x58, 0x0C)).convert("RGBA")
	bg.putalpha(_rounded_mask(s, radius=int(56 * scale)))
	img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
	img.alpha_composite(bg)

	draw = ImageDraw.Draw(img)
	white = (255, 255, 255, 255)

	# 2) 放大镜：圆环 + 手柄（左下方），象征「放大/超分」
	cx, cy, r = int(118 * scale), int(112 * scale), int(58 * scale)
	stroke = int(17 * scale)
	draw.ellipse([cx - r, cy - r, cx + r, cy + r],
				 outline=white, width=stroke)
	hx0, hy0 = cx + int(r * 0.72), cy + int(r * 0.72)
	hx1, hy1 = hx0 + int(44 * scale), hy0 + int(44 * scale)
	draw.line([(hx0, hy0), (hx1, hy1)], fill=white, width=stroke)
	# 手柄末端补一个圆头
	draw.ellipse([hx1 - stroke // 2, hy1 - stroke // 2,
				  hx1 + stroke // 2, hy1 + stroke // 2], fill=white)

	# 3) 镜内向上箭头：表示「放大 4 倍/超分」
	ax = cx
	head_top = cy - int(30 * scale)
	stem_bottom = cy + int(30 * scale)
	draw.line([(ax, stem_bottom), (ax, head_top)], fill=white,
			  width=int(13 * scale))
	draw.line([(ax - int(19 * scale), cy - int(6 * scale)),
			   (ax, head_top)], fill=white, width=int(13 * scale))
	draw.line([(ax + int(19 * scale), cy - int(6 * scale)),
			   (ax, head_top)], fill=white, width=int(13 * scale))

	# 4) 右上角四角星光（细节/清晰度）
	sx, sy, sr = int(197 * scale), int(62 * scale), int(15 * scale)
	arm = int(6 * scale)
	draw.line([(sx - sr, sy), (sx + sr, sy)], fill=white, width=arm)
	draw.line([(sx, sy - sr), (sx, sy + sr)], fill=white, width=arm)
	draw.ellipse([sx - arm // 2, sy - arm // 2, sx + arm // 2,
				  sy + arm // 2], fill=white)

	# 缩小到目标尺寸
	small = img.resize((256, 256), Image.LANCZOS)
	png_path = os.path.join(out_dir, "app.png")
	small.save(png_path)
	print("[ok] %s" % png_path)

	ico_path = os.path.join(out_dir, "app.ico")
	small.save(ico_path, format="ICO",
			   sizes=[(16, 16), (32, 32), (48, 48), (256, 256)])
	print("[ok] %s" % ico_path)


def _tint_white_png(src_png, dst_png, color):
	"""把白色图标染色（乘法混合保持抗锯齿 alpha），供 QSS image 直接使用。"""
	img = Image.open(src_png).convert("RGBA")
	pixels = img.load()
	for y in range(img.height):
		for x in range(img.width):
			r, g, b, a = pixels[x, y]
			pixels[x, y] = (color[0], color[1], color[2], a)
	img.save(dst_png)


def generate_qss_icons(out_dir):
	"""QSS 的 image:url() 无法上色，且按原始像素尺寸绘制，故生成定尺寸彩色位图：
	   - chevron-down.png      12px #52525B（亮色主题的次级文字色）
	   - chevron-down-dark.png 12px #ABABB3（暗色主题的次级文字色）
	   - chevron-right.png / check.png 12px 白色（选中态/树形展开，白色由 QSS 场景决定）
	"""
	tmp = os.path.join(out_dir, "_tmp12.png")
	img = Image.open(os.path.join(out_dir, "chevron-down.png")).convert("RGBA")
	img.resize((12, 12), Image.LANCZOS).save(tmp)
	_tint_white_png(tmp, os.path.join(out_dir, "chevron-down-light.png"),
					(0x52, 0x52, 0x5B))
	_tint_white_png(tmp, os.path.join(out_dir, "chevron-down-dark.png"),
					(0xAB, 0xAB, 0xB3))
	os.remove(tmp)

	# QSS 复选框对勾：白色 12px（画在橙色底上）
	img = Image.open(os.path.join(out_dir, "check.png")).convert("RGBA")
	img.resize((12, 12), Image.LANCZOS).save(
		os.path.join(out_dir, "check-qss.png"))
	print("[ok] QSS 位图（chevron/check）")


def main():
	parser = argparse.ArgumentParser()
	parser.add_argument(
		"--lucide",
		default=r"C:\Users\yxl\AppData\Local\Temp\icons\package\icons",
		help="Lucide 图标目录（含 *.svg）")
	parser.add_argument("--rsvg", default="rsvg-convert")
	args = parser.parse_args()

	here = os.path.dirname(os.path.abspath(__file__))
	resources_dir = os.path.dirname(here)  # resources/
	if not os.path.isdir(args.lucide):
		print("找不到 Lucide 目录: %s" % args.lucide)
		return 1

	generate_ui_icons(args.lucide, here, args.rsvg)
	generate_qss_icons(here)
	generate_app_icon(resources_dir)
	return 0


if __name__ == "__main__":
	sys.exit(main())
