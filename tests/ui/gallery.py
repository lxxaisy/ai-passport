"""Package actual LVGL host renders and their build evidence as standalone HTML."""

import argparse
import base64
import html
import json
from pathlib import Path

from PIL import Image, ImageDraw


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("directory", type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--build-log", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    manifest = json.loads(args.manifest.read_text())
    build_log = args.build_log.read_text()
    assert "Host tests: PASS" in build_log and "Firmware build: PASS" in build_log
    verification = (args.directory / "verification.log").read_text()
    assert "Layout navigation-soak-200:" in verification
    frames = sorted(args.directory.glob("*.ppm"))
    figures = []
    for source in frames:
        png = source.with_suffix(".png")
        with Image.open(source) as image:
            assert image.size == (240, 320)
            image.save(png)
        data = base64.b64encode(png.read_bytes()).decode("ascii")
        figures.append(
            '<figure><img width="240" height="320" alt="' + html.escape(source.stem)
            + '" src="data:image/png;base64,' + data + '"><figcaption>'
            + html.escape(source.stem) + '</figcaption></figure>'
        )
    names = ["overview", "tasks", "task-detail", "task-tokens", "alerts-pending",
             "quotas", "quota-unavailable-source", "settings-muted-pending", "offline"]
    sheet = Image.new("RGB", (792, 1068), (241, 244, 244))
    drawing = ImageDraw.Draw(sheet)
    for index, name in enumerate(names):
        x, y = index % 3 * 264 + 12, index // 3 * 356
        with Image.open(args.directory / (name + ".png")) as frame:
            sheet.paste(frame, (x, y + 24))
        drawing.text((x, y + 6), name, fill=(23, 33, 38))
    sheet.save(args.directory / "contact-sheet.png")
    version = html.escape(manifest["app_descriptor"]["version"])
    full_hash = html.escape(manifest["full_bin_sha256"])
    elf_hash = html.escape(manifest["app_elf_sha256"])
    report = '''<!doctype html>
<html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>AI Passport UI 验收</title><style>
*{box-sizing:border-box}:root{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;color:#182426;background:#fff;letter-spacing:0}
body{margin:0}main{max-width:1120px;margin:auto;padding:28px 20px 48px}h1{font-size:26px;margin:0 0 12px}h2{font-size:18px;margin:28px 0 10px}p{line-height:1.7;margin:10px 0}code{overflow-wrap:anywhere;font-family:ui-monospace,monospace;font-size:12px}.status{border-top:1px solid #d6dfdf;border-bottom:1px solid #d6dfdf;padding:14px 0;color:#21624b}.gallery{display:grid;grid-template-columns:repeat(auto-fit,minmax(240px,1fr));gap:20px 12px}figure{margin:0;text-align:center}img{display:block;max-width:100%;height:auto;margin:auto;background:#10191c;border-radius:4px}figcaption{font-size:13px;margin:8px 0;color:#526467}pre{font-size:11px;background:#f2f5f5;padding:14px;white-space:pre-wrap;overflow-wrap:anywhere;line-height:1.5}.note{color:#526467;font-size:13px}
@media(max-width:600px){main{padding:22px 16px}h1{font-size:22px}}
</style><main>
'''
    report += '<h1>AI Passport ' + version + ' 界面验收</h1>'
    report += '<p class="status">Build: PASS · Host tests: PASS · Device tests: NOT RUN</p>'
    report += '<p>总览保留全局任务统计；近期任务分为标题、客户端状态、项目三行。上下选择、确定进入、长按确定返回并保持焦点；详情和额度来源支持切页。提醒区分未读、已读待同步、已读。</p>'
    report += '<p>以下 ' + str(len(frames)) + ' 张图由实际 LVGL、应用代码、中文字库及 BSP 圆角生成，数值来自合成测试样例。已检查字形覆盖、240×320 边界、文字容器互不重叠、null/零值、长中文及 200 次切页，沿用 24 KiB LVGL 池。</p>'
    report += '<p class="note">尚未烧录此版本。真机中文、三键、音频、USB、BLE 和运行内存仍待单独验收；之前版本的实机结果不能替代本镜像的验证。</p>'
    report += '<h2>实际渲染</h2><div class="gallery">' + ''.join(figures) + '</div>'
    report += '<h2>构建身份</h2><p>版本：<code>' + version + '</code>；完整镜像偏移：<code>0x0</code>。</p>'
    report += '<p>Full SHA-256：<code>' + full_hash + '</code></p><p>ELF SHA-256：<code>' + elf_hash + '</code></p>'
    report += '<p class="note">完整镜像可能重置 NVS 设置；本次未操作串口、烧录或整片擦除。</p>'
    report += '<h2>主机渲染证据</h2><pre>' + html.escape(verification) + '</pre></main></html>\n'
    args.output.write_text(report)
    print(json.dumps({"report": str(args.output), "frames": len(frames), "version": manifest["app_descriptor"]["version"]}))


if __name__ == "__main__":
    main()
